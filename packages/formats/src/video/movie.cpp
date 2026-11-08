#include <dwe/formats/video/movie.hpp>

#include <array>
#include <format>
#include <utility>

#include "../yaml/yaml_values.hpp"

namespace dwe::formats::video {

namespace {

using core::YamlNode;
using yaml::null;
using yaml::number;
using yaml::quoted;

constexpr std::size_t EntrySize = 0x28;
constexpr std::size_t PathSize = 0x20;

struct VersionAddress {
  std::string_view version;
  std::uint32_t address;
};

constexpr std::array MovieTables = {
    VersionAddress{.version = "us", .address = 0x8001094C},
    VersionAddress{.version = "eu", .address = 0x80010B64},
    VersionAddress{.version = "jp", .address = 0x80010B70},
    VersionAddress{.version = "jp_rev1", .address = 0x80010B70},
};

} // namespace

std::optional<std::uint32_t> movieTableAddress(std::string_view version) {
  for (const auto& table : MovieTables) {
    if (table.version == version) {
      return table.address;
    }
  }
  return std::nullopt;
}

core::Result<std::vector<MovieEntry>> readMovieTable(const exe::MemoryImage& overlay,
                                                     std::uint32_t address) {
  std::vector<MovieEntry> entries;
  for (std::size_t i = 0; i < MovieCount; ++i) {
    const auto entryAddress = static_cast<std::uint32_t>(address + (i * EntrySize));
    auto path = overlay.stringAt(entryAddress, PathSize - 1);
    if (!path) {
      return core::fail(std::format("MOV_MOVIES[{}]: {}", i, path.error().message));
    }
    const auto numbers = overlay.bytesAt(entryAddress + PathSize, 8);
    if (!numbers) {
      return core::fail(std::format("MOV_MOVIES[{}]: {}", i, numbers.error().message));
    }
    const auto field = [&](std::size_t offset) {
      std::uint32_t value = 0;
      for (std::size_t byte = 0; byte < 4; ++byte) {
        value |= std::to_integer<std::uint32_t>((*numbers)[offset + byte]) << (8 * byte);
      }
      return static_cast<std::int32_t>(value);
    };
    entries.push_back(
        MovieEntry{.path = std::move(*path), .startFrame = field(0), .frameCount = field(4)});
  }
  return entries;
}

std::string movieVolumePath(std::string_view gamePath) {
  std::string path;
  for (const char c : gamePath.substr(0, gamePath.find(';'))) {
    if (c == '\\') {
      if (!path.empty()) {
        path += '/';
      }
    } else {
      path += c;
    }
  }
  return path;
}

YamlNode movieToYaml(const MovieInfo& info) {
  YamlNode fps = YamlNode::mapping();
  fps.set("numerator", number(info.fpsNumerator));
  fps.set("denominator", number(info.fpsDenominator));

  YamlNode video = YamlNode::mapping();
  video.set("width", number(info.width));
  video.set("height", number(info.height));
  video.set("frames", number(info.frames));
  video.set("first_frame", number(info.firstFrame));
  video.set("fps", std::move(fps));
  video.set("bitstream_version", number(info.bitstreamVersion));
  video.set("chroma", YamlNode::scalar("420p10"));
  video.set("range", YamlNode::scalar("full"));

  YamlNode audio = null();
  if (info.audio) {
    audio = YamlNode::mapping();
    audio.set("sample_rate", number(info.audio->sampleRate));
    audio.set("channels", number(info.audio->channels));
    audio.set("sample_frames", number(info.audioSampleFrames));
    audio.set("source_bits", number(info.audio->bitsPerSample));
  }

  YamlNode sectors = YamlNode::mapping();
  sectors.set("total", number(info.sectors));
  sectors.set("video", number(info.videoSectors));
  sectors.set("audio", number(info.audioSectors));
  sectors.set("padding", number(info.paddingSectors));

  YamlNode playback = null();
  if (info.tableIndex && info.tableEntry) {
    playback = YamlNode::mapping();
    playback.set("table_index", number(*info.tableIndex));
    playback.set("path", quoted(info.tableEntry->path));
    playback.set("start_frame", number(info.tableEntry->startFrame));
    playback.set("frame_count", number(info.tableEntry->frameCount));
  }

  YamlNode root = YamlNode::mapping();
  root.set("source", quoted(info.source));
  root.set("video", std::move(video));
  root.set("audio", std::move(audio));
  root.set("sectors", std::move(sectors));
  root.set("playback", std::move(playback));
  return root;
}

} // namespace dwe::formats::video
