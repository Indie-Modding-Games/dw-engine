#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/video/movie_player.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <variant>

namespace dwe::video {

namespace {

// The files of a movie in the packs: movie/<name>/<file>.
core::Result<assets::AssetId> movieAsset(std::string_view name, std::string_view file) {
  return assets::AssetId::parse(std::format("movie/{}/{}", name, file));
}

// The frames shown, from the video's frames and the table's stop frame (playback.frame_count,
// counted from video.first_frame).
std::uint32_t shownFrames(std::uint32_t frames, std::uint32_t firstFrame,
                          std::optional<std::uint32_t> stopFrame) {
  if (!stopFrame || *stopFrame < firstFrame) {
    return frames;
  }
  return std::min(frames, *stopFrame - firstFrame + 1);
}

core::Result<MovieInfo> parseMovieInfo(std::string_view text, std::string_view name) {
  const std::string context = std::format("the movie '{}'", name);
  auto tree = core::parseYaml(text);
  if (!tree) {
    return core::fail(std::format("{}: {}", context, tree.error().message));
  }
  assets::YamlReader reader;
  const core::YamlNode& video = reader.field(*tree, "video");
  const core::YamlNode& fps = reader.field(video, "fps");
  MovieInfo info{.width = reader.integer<std::uint32_t>(reader.field(video, "width")),
                 .height = reader.integer<std::uint32_t>(reader.field(video, "height")),
                 .fpsNumerator = reader.integer<std::uint32_t>(reader.field(fps, "numerator")),
                 .fpsDenominator = reader.integer<std::uint32_t>(reader.field(fps, "denominator")),
                 .shownFrames = 0,
                 .firstFrame = 0,
                 .stopFrame = std::nullopt,
                 .hasSound = false};
  const auto frames = reader.integer<std::uint32_t>(reader.field(video, "frames"));
  const auto firstFrame = reader.integer<std::uint32_t>(reader.field(video, "first_frame"));
  std::optional<std::uint32_t> stopFrame;
  if (const core::YamlNode* playback = tree->find("playback");
      playback != nullptr && !assets::YamlReader::isNull(*playback)) {
    stopFrame = reader.integer<std::uint32_t>(reader.field(*playback, "frame_count"));
  }
  info.shownFrames = shownFrames(frames, firstFrame, stopFrame);
  info.firstFrame = firstFrame;
  info.stopFrame = stopFrame;
  const core::YamlNode* audio = tree->find("audio");
  info.hasSound = audio != nullptr && !assets::YamlReader::isNull(*audio);
  if (reader.ok() && (info.fpsNumerator == 0 || info.fpsDenominator == 0)) {
    return core::fail(std::format("{}: its frame rate is not a rate", context));
  }
  return reader.finish(info, context);
}

} // namespace

core::Result<MovieInfo> readMovieInfo(const assets::VirtualFileSystem& files,
                                      std::string_view name) {
  auto id = movieAsset(name, "movie");
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto bytes = files.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return parseMovieInfo(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()}, name);
}

core::Result<MoviePlayer> MoviePlayer::open(const assets::VirtualFileSystem& files,
                                            std::string_view name) {
  auto info = readMovieInfo(files, name);
  if (!info) {
    return std::unexpected{info.error()};
  }
  auto id = movieAsset(name, "video");
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto file = files.open(*id);
  if (!file) {
    return std::unexpected{file.error()};
  }
  const assets::AssetSource* source = files.resolve(*id);
  auto reader = source != nullptr && source->file.extension() == ".ivf"
                    ? Av1Reader::open(std::move(*file), id->str()).transform([](Av1Reader opened) {
                        return Reader{std::move(opened)};
                      })
                    : Y4mReader::open(std::move(*file), id->str()).transform([](Y4mReader opened) {
                        return Reader{std::move(opened)};
                      });
  if (!reader) {
    return std::unexpected{reader.error()};
  }
  const Y4mStream stream = std::visit([](const auto& opened) { return opened.stream(); }, *reader);
  if (stream.width != info->width || stream.height != info->height) {
    return core::fail(std::format("the movie '{}' is {}x{}, but its video is {}x{}", name,
                                  info->width, info->height, stream.width, stream.height));
  }
  core::logDebug("[port:movie] MOV_readFrame (mov.c:191): '{}' {}x{} at {}/{} fps, frames from {}, "
                 "table stop frame {}, {} frames shown",
                 name, info->width, info->height, info->fpsNumerator, info->fpsDenominator,
                 info->firstFrame, info->stopFrame ? std::format("{}", *info->stopFrame) : "none",
                 info->shownFrames);
  return MoviePlayer{*info, std::move(*reader)};
}

core::Result<std::optional<assets::Sound>>
MoviePlayer::loadSound(const assets::VirtualFileSystem& files, std::string_view name) {
  auto info = readMovieInfo(files, name);
  if (!info) {
    return std::unexpected{info.error()};
  }
  if (!info->hasSound) {
    return std::optional<assets::Sound>{};
  }
  auto id = movieAsset(name, "audio");
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto bytes = files.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  // The WAV dwx extract writes, or the Ogg Opus dwx cook makes of it (at 48 kHz: the clock follows
  // the decoded sound's own rate).
  const assets::AssetSource* source = files.resolve(*id);
  auto sound = source != nullptr && source->file.extension() == ".wav" ? assets::decodeWav(*bytes)
                                                                       : assets::decodeOpus(*bytes);
  if (!sound) {
    return core::fail(std::format("the sound of the movie '{}': {}", name, sound.error().message));
  }
  return std::optional<assets::Sound>{std::move(*sound)};
}

std::uint64_t MoviePlayer::frameAt(double seconds) const {
  if (!(seconds > 0.0)) {
    return 0;
  }
  const double frame = std::floor(seconds * info_.fpsNumerator / info_.fpsDenominator);
  return static_cast<std::uint64_t>(std::min(frame, static_cast<double>(info_.shownFrames)));
}

core::Result<bool> MoviePlayer::advanceTo(double seconds) {
  if (info_.shownFrames == 0) {
    return false;
  }
  const std::uint64_t target = std::min<std::uint64_t>(frameAt(seconds), info_.shownFrames - 1);
  if (nextFrame_ > target) {
    return false;
  }
  while (nextFrame_ < target) {
    auto skipped = std::visit([](auto& reader) { return reader.skip(); }, reader_);
    if (!skipped) {
      return std::unexpected{skipped.error()};
    }
    ++nextFrame_;
  }
  auto read = std::visit([this](auto& reader) { return reader.read(yuv_); }, reader_);
  if (!read) {
    return std::unexpected{read.error()};
  }
  ++nextFrame_;
  if (!*read) {
    // The file ended before the frames its description counts: the last picture stays.
    info_.shownFrames = static_cast<std::uint32_t>(nextFrame_ - 1);
    core::logDebug("[port:movie] MOV_readFrame (mov.c:191): the video ended after {} frames, "
                   "before its stop frame; the movie ends there",
                   info_.shownFrames);
    return false;
  }
  toRgba(yuv_, rgba_);
  return true;
}

bool MoviePlayer::finishedAt(double seconds) const {
  return frameAt(seconds) >= info_.shownFrames;
}

} // namespace dwe::video
