#include "video_extractors.hpp"

#include <dwe/core/yaml.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/video/mdec.hpp>
#include <dwe/formats/video/media_files.hpp>
#include <dwe/formats/video/movie.hpp>
#include <dwe/formats/video/restore.hpp>
#include <dwe/formats/video/str.hpp>
#include <dwe/formats/video/xa_adpcm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "asset_names.hpp"

namespace dwe::dwx {

namespace {

using formats::disc::IsoFile;
using formats::video::MovieEntry;
using formats::video::MovieInfo;
using formats::video::StrFrame;
using formats::video::XaCoding;

// mov.c: MOV_startStream reads the stream at double speed (CdlModeSpeed), 150 sectors a second. The
// audio agrees: one 37.8 kHz stereo sector (2016 sample frames) every 8 sectors is 37800 sample
// frames a second.
constexpr std::uint32_t SectorsPerSecond = 150;
// A frame period this close to a whole number of sectors is taken as that number.
constexpr double WholePeriodTolerance = 0.05;
// A damaged file could give one problem per sector; the report keeps the first ones and counts the
// rest.
constexpr std::size_t MaxUnknowns = 32;

// The unknowns of one movie, the first MaxUnknowns of them in full.
class Problems {
public:
  void add(std::string message) {
    if (messages_.size() < MaxUnknowns) {
      messages_.push_back(std::move(message));
    } else {
      ++dropped_;
    }
  }

  void moveTo(Extraction& extraction) {
    if (dropped_ > 0) {
      messages_.push_back(std::format("{} more problems like the ones above", dropped_));
    }
    std::ranges::move(messages_, std::back_inserter(extraction.unknowns));
  }

private:
  std::vector<std::string> messages_;
  std::size_t dropped_ = 0;
};

// The audio stream of a movie: the file, channel and coding of its first audio sector, which every
// other one must share.
struct AudioStream {
  std::uint8_t file;
  std::uint8_t channel;
  XaCoding coding;
};

// What reading a movie's sectors gives.
struct Demux {
  MovieInfo info;
  std::vector<StrFrame> frames;
  std::vector<std::uint32_t> frameStarts; // the sector each frame starts at
  std::set<std::uint16_t> versions;
  std::vector<std::int16_t> samples;
  std::optional<AudioStream> audio;
  std::size_t parameterMismatches = 0;
};

void addFrame(Demux& demux, StrFrame frame, std::uint32_t start, Problems& problems) {
  // The size is checked here, not only when the video is written: a frame the writer cannot decode
  // would stop the pack halfway through the file.
  const auto macroblocks = formats::video::countMacroblocks(frame.width, frame.height);
  if (!macroblocks) {
    problems.add(
        std::format("frame {} is left out: {}", frame.number, macroblocks.error().message));
    return;
  }
  MovieInfo& info = demux.info;
  if (demux.frames.empty()) {
    info.width = frame.width;
    info.height = frame.height;
    info.firstFrame = frame.number;
  } else if (frame.width != info.width || frame.height != info.height) {
    problems.add(std::format("frame {} is {}x{}, the movie {}x{}: it is left out", frame.number,
                             frame.width, frame.height, info.width, info.height));
    return;
  } else if (frame.number != demux.frames.back().number + 1) {
    problems.add(
        std::format("frame {} follows frame {}", frame.number, demux.frames.back().number));
  }
  const auto codes = formats::video::decodeBitstream(frame.bitstream, *macroblocks);
  if (!codes) {
    problems.add(std::format("frame {} does not decode and is left out: {}", frame.number,
                             codes.error().message));
    return;
  }
  demux.versions.insert(formats::video::readBitstreamHeader(frame.bitstream)->version);
  demux.frames.push_back(std::move(frame));
  demux.frameStarts.push_back(start);
}

void addAudio(Demux& demux, const formats::video::StrAudioSector& sector,
              formats::video::XaDecoder& decoder, std::uint32_t index, Problems& problems) {
  if (!demux.audio) {
    demux.audio =
        AudioStream{.file = sector.file, .channel = sector.channel, .coding = sector.coding};
  } else if (sector.file != demux.audio->file || sector.channel != demux.audio->channel ||
             sector.coding != demux.audio->coding) {
    problems.add(std::format(
        "audio sector {} (file {}, channel {}) is not part of the movie's audio stream (file "
        "{}, channel {})",
        index, sector.file, sector.channel, demux.audio->file, demux.audio->channel));
    return;
  }
  const auto mismatches = decoder.decodeSector(sector.data, sector.coding, demux.samples);
  if (!mismatches) {
    problems.add(std::format("audio sector {}: {}", index, mismatches.error().message));
    return;
  }
  demux.parameterMismatches += *mismatches;
}

core::Result<Demux> demuxMovie(const Disc& disc, const IsoFile& file, Problems& problems) {
  Demux demux;
  demux.info.source = file.path;
  formats::video::StrFrameAssembler assembler;
  formats::video::XaDecoder decoder;
  std::uint32_t frameStart = 0;
  const std::uint32_t sectors = formats::disc::sectorSpan(file);
  for (std::uint32_t index = 0; index < sectors; ++index) {
    const auto data = disc.source().readSector(file.lba + index);
    if (!data) {
      return std::unexpected{data.error()};
    }
    if (!data->subheader) {
      return core::fail(
          "a movie needs a raw image of the disc: a 2048-byte image has lost its audio sectors "
          "and the sector types");
    }
    ++demux.info.sectors;
    const auto sector = formats::video::readStrSector(*data);
    if (!sector) {
      problems.add(std::format("sector {}: {}", index, sector.error().message));
      continue;
    }
    if (const auto* audio = std::get_if<formats::video::StrAudioSector>(&*sector)) {
      ++demux.info.audioSectors;
      addAudio(demux, *audio, decoder, index, problems);
      continue;
    }
    const auto* video = std::get_if<formats::video::StrVideoSector>(&*sector);
    if (video == nullptr) {
      ++demux.info.paddingSectors;
      continue;
    }
    ++demux.info.videoSectors;
    if (video->sectorInFrame == 0) {
      frameStart = index;
    }
    auto frame = assembler.add(*video);
    if (!frame) {
      problems.add(std::format("sector {}: {}", index, frame.error().message));
      continue;
    }
    if (frame->has_value()) {
      addFrame(demux, std::move(**frame), frameStart, problems);
    }
  }
  if (const auto pending = assembler.pendingFrame()) {
    problems.add(std::format("frame {} is missing its last sectors", *pending));
  }
  if (demux.parameterMismatches > 0) {
    problems.add(
        std::format("{} audio sound parameters differ from their copies (the first copy is used)",
                    demux.parameterMismatches));
  }
  return demux;
}

// The frame rate: the stream's sector rate over the average number of sectors from one frame's
// start to the next. With one frame, or frames that all start at the same sector, the whole file is
// taken as the frames' time.
void setFrameRate(MovieInfo& info, const std::vector<std::uint32_t>& frameStarts) {
  if (frameStarts.empty()) {
    return;
  }
  std::uint64_t numerator = 0;
  std::uint64_t denominator = 0;
  if (frameStarts.size() >= 2) {
    numerator = std::uint64_t{SectorsPerSecond} * (frameStarts.size() - 1);
    denominator = frameStarts.back() - frameStarts.front();
  }
  if (denominator == 0) {
    numerator = std::uint64_t{SectorsPerSecond} * frameStarts.size();
    denominator = std::max<std::uint64_t>(info.sectors, 1);
  }
  const double period =
      static_cast<double>(denominator) / static_cast<double>(numerator) * SectorsPerSecond;
  const double whole = std::round(period);
  if (whole >= 1 && std::abs(period - whole) < WholePeriodTolerance) {
    numerator = SectorsPerSecond;
    denominator = static_cast<std::uint64_t>(whole);
  }
  const std::uint64_t divisor = std::gcd(numerator, denominator);
  info.fpsNumerator = static_cast<std::uint32_t>(numerator / divisor);
  info.fpsDenominator = static_cast<std::uint32_t>(denominator / divisor);
}

// Finds a file by name in whatever folder it is, as the original opens its overlays (file.c:
// lookupFileTable).
const IsoFile* findByName(const Disc& disc, std::string_view name) {
  const auto& files = disc.volume().files;
  const auto found = std::ranges::find_if(files, [name](const IsoFile& file) {
    return file.path == name ||
           (file.path.ends_with(name) && file.path[file.path.size() - name.size() - 1] == '/');
  });
  return found == files.end() ? nullptr : &*found;
}

// Looks the movie up in MOV_MOVIES. Leaves the info without an entry, with a note, when the version
// is not known.
void findTableEntry(const Disc& disc, MovieInfo& info, Extraction& extraction, Problems& problems) {
  const auto address = formats::video::movieTableAddress(disc.info().version);
  if (!address) {
    extraction.notes.emplace_back(
        "the executable's version is not known: the movie's MOV_MOVIES entry is left out");
    return;
  }
  const IsoFile* overlay = findByName(disc, formats::video::MovieOverlayPath);
  if (overlay == nullptr) {
    problems.add(std::format("{} is not on the disc, so MOV_MOVIES cannot be read",
                             formats::video::MovieOverlayPath));
    return;
  }
  const auto bytes = formats::disc::readIsoFile(disc.source(), *overlay);
  if (!bytes) {
    problems.add(std::format("{}: {}", overlay->path, bytes.error().message));
    return;
  }
  const auto image =
      formats::exe::MemoryImage::fromOverlay(*bytes, formats::video::MovieOverlayLoadAddress);
  const auto entries = formats::video::readMovieTable(image, *address);
  if (!entries) {
    problems.add(std::format("MOV_MOVIES: {}", entries.error().message));
    return;
  }
  for (std::size_t index = 0; index < entries->size(); ++index) {
    if (formats::video::movieVolumePath((*entries)[index].path) == info.source) {
      info.tableIndex = index;
      info.tableEntry = (*entries)[index];
      return;
    }
  }
  extraction.notes.emplace_back("MOV_MOVIES does not name this file: the game never plays it");
}

// The decoded video, written frame by frame: the frames' bitstreams stay in memory, their planes do
// not. Each frame is decoded precisely and cleaned up (formats/video restore.hpp) into 10 bits.
OutputStream videoStream(std::shared_ptr<const std::vector<StrFrame>> frames,
                         const formats::video::Y4mFormat& format) {
  return [frames = std::move(frames), format](const ByteSink& sink) -> core::Result<void> {
    const std::string header = formats::video::y4mHeader(format);
    if (auto written = sink(std::as_bytes(std::span{header})); !written) {
      return written;
    }
    std::vector<std::byte> buffer;
    for (const StrFrame& frame : *frames) {
      auto planes =
          formats::video::decodePreciseFrame(frame.bitstream, format.width, format.height);
      if (!planes) {
        return core::fail(std::format("frame {}: {}", frame.number, planes.error().message));
      }
      formats::video::deblock(*planes);
      formats::video::dering(*planes);
      buffer.clear();
      if (auto appended = formats::video::appendY4mFrame(format, *planes, buffer); !appended) {
        return appended;
      }
      if (auto written = sink(buffer); !written) {
        return written;
      }
    }
    return {};
  };
}

} // namespace

bool handlesMovie(std::string_view path) {
  return path.starts_with("MOVIE/") && path.ends_with(".STR");
}

core::Result<Extraction> extractMovie(const Disc& disc, const IsoFile& file) {
  Extraction extraction;
  Problems problems;
  auto demux = demuxMovie(disc, file, problems);
  if (!demux) {
    return std::unexpected{demux.error()};
  }
  MovieInfo& info = demux->info;
  info.frames = static_cast<std::uint32_t>(demux->frames.size());
  setFrameRate(info, demux->frameStarts);
  if (!demux->versions.empty()) {
    info.bitstreamVersion = *demux->versions.begin();
  }
  if (demux->versions.size() > 1) {
    extraction.notes.push_back(std::format("frames use bitstream versions {} to {}",
                                           *demux->versions.begin(), *demux->versions.rbegin()));
  }
  if (demux->audio) {
    info.audio = demux->audio->coding;
    info.audioSampleFrames = demux->samples.size() / demux->audio->coding.channels;
  }
  findTableEntry(disc, info, extraction, problems);

  const auto video = names::movieAsset(file.path, "video");
  const auto audio = names::movieAsset(file.path, "audio");
  const auto document = names::movieAsset(file.path, "movie");
  if (!video || !audio || !document) {
    return core::fail(std::format("{} has no valid asset id", file.path));
  }
  if (demux->frames.empty()) {
    problems.add("the file has no frames");
  } else {
    const formats::video::Y4mFormat format{.width = info.width,
                                           .height = info.height,
                                           .fpsNumerator = info.fpsNumerator,
                                           .fpsDenominator = info.fpsDenominator,
                                           .bitDepth = 10};
    auto frames = std::make_shared<const std::vector<StrFrame>>(std::move(demux->frames));
    extraction.outputs.push_back(OutputFile{.id = *video,
                                            .extension = "y4m",
                                            .bytes = {},
                                            .stream = videoStream(std::move(frames), format)});
  }
  if (demux->audio) {
    auto wav = formats::video::encodeWav(demux->samples, demux->audio->coding.channels,
                                         demux->audio->coding.sampleRate);
    if (!wav) {
      return std::unexpected{wav.error()};
    }
    extraction.outputs.push_back(
        OutputFile{.id = *audio, .extension = "wav", .bytes = std::move(*wav), .stream = {}});
  }
  const auto text = core::emitYaml(formats::video::movieToYaml(info));
  if (!text) {
    return std::unexpected{text.error()};
  }
  const auto bytes = std::as_bytes(std::span{*text});
  extraction.outputs.push_back(
      OutputFile{.id = *document,
                 .extension = "yaml",
                 .bytes = std::vector<std::byte>(bytes.begin(), bytes.end()),
                 .stream = {}});
  problems.moveTo(extraction);
  return extraction;
}

} // namespace dwe::dwx
