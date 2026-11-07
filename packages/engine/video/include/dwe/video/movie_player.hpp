#pragma once

#include <dwe/assets/sound.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/video/av1_reader.hpp>
#include <dwe/video/picture.hpp>
#include <dwe/video/y4m_reader.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dwe::video {

// What movie/<name>/movie.yaml says about a movie, as dwx extract writes it.
struct MovieInfo {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t fpsNumerator = 0;
  std::uint32_t fpsDenominator = 1;
  // The frames the original shows: up to the frame its table stops at (mov.c: MOV_readFrame sets
  // MOVIE_COMPLETED once the frame number reaches frameCount), or all of them when the table's
  // count is past the file's last frame or the movie has no table entry.
  std::uint32_t shownFrames = 0;
  // The number of the file's first frame, and the table's stop frame when it has one.
  std::uint32_t firstFrame = 0;
  std::optional<std::uint32_t> stopFrame;
  bool hasSound = false;
};

// Reads movie/<name>/movie.yaml.
core::Result<MovieInfo> readMovieInfo(const assets::VirtualFileSystem& files,
                                      std::string_view name);

// Plays one movie of the packs (movie/<name>/: the video, the sound and movie.yaml): it reads the
// video as the clock moves and gives the picture of the moment as RGBA. The video is the Y4M dwx
// extract writes or the AV1 (IVF) dwx cook makes of it, the sound the WAV or its Ogg Opus; the
// same asset ids give either (docs/architecture/peliculas.md). It knows nothing of the
// screen or the sound device; its caller draws the picture and plays the sound, and drives the
// clock with the sound's position (mov.c: MOV_playMovie shows each frame as the stream brings it,
// so the sound leads and the pictures follow).
class MoviePlayer {
public:
  static core::Result<MoviePlayer> open(const assets::VirtualFileSystem& files,
                                        std::string_view name);

  [[nodiscard]] const MovieInfo& info() const { return info_; }

  // The sound of the movie, decoded whole; nothing when the movie has none.
  [[nodiscard]] static core::Result<std::optional<assets::Sound>>
  loadSound(const assets::VirtualFileSystem& files, std::string_view name);

  // Moves the video to the frame shown seconds after the start, passing over the frames it is
  // late for. Returns whether the picture changed.
  core::Result<bool> advanceTo(double seconds);

  // The picture shown now: width * height * 4 bytes of RGBA, rows from the top; empty before the
  // first advanceTo.
  [[nodiscard]] std::span<const std::byte> picture() const { return rgba_; }

  // Whether the movie is over at this time: its last shown frame has had its whole duration.
  [[nodiscard]] bool finishedAt(double seconds) const;

private:
  using Reader = std::variant<Y4mReader, Av1Reader>;

  MoviePlayer(MovieInfo info, Reader reader) : info_(info), reader_(std::move(reader)) {}

  [[nodiscard]] std::uint64_t frameAt(double seconds) const;

  MovieInfo info_;
  Reader reader_;
  // The index of the next frame the reader gives.
  std::uint64_t nextFrame_ = 0;
  YuvPicture yuv_;
  std::vector<std::byte> rgba_;
};

} // namespace dwe::video
