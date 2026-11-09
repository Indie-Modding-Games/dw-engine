#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// When the original plays a movie (main.c: main).
enum class MovieMoment : std::uint8_t {
  Boot,        // at start, and when the title has been idle (runLandingScreen)
  NewGame,     // before a new game's first map (MAIN_STATE 0)
  ClearedLoad, // on continuing a game saved after the ending (MAIN_STATE 2)
  Ending,      // after the ending, before the title (MAIN_STATE 3)
};
inline constexpr std::size_t MovieMomentCount = 4;

// The movie main.c plays straight after another one: the ending's movie goes back round its loop to
// the boot movie, and only then to the "Press Start" screen (main.c: main, playMovie(3, 1) and then
// playMovie(0, 1) before runLandingScreen).
[[nodiscard]] constexpr std::optional<MovieMoment> movieAfter(MovieMoment moment) {
  if (moment == MovieMoment::Ending) {
    return MovieMoment::Boot;
  }
  return std::nullopt;
}

// The screen the original shows a movie on, and the line of it where the picture starts (mov.c:
// MOV_initEnvironment(&MOV_ENV, 0, 32, 0, 272): 176 lines from line 32 of 240).
inline constexpr float MovieScreenWidth = 320.0F;
inline constexpr float MovieScreenHeight = 240.0F;
inline constexpr float MoviePictureTop = 32.0F;

// Where a movie's picture goes in the window, in pixels.
struct MovieFrame {
  float left = 0.0F;
  float top = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
};

// The picture's place in a window. As the original: its screen fitted to the window with its shape
// kept, in the middle, and the picture on it from MoviePictureTop. Filling (the «widescreen
// pictures» option, on by default): the picture alone fitted to the window with its shape kept, in
// the middle; its 320 x 176 (about 16:9) then reaches a 16:9 window's edges, where the original's
// 4:3 screen leaves bars on every side. In a 4:3 window both are the same.
[[nodiscard]] constexpr MovieFrame movieFrame(float windowWidth, float windowHeight,
                                              float pictureWidth, float pictureHeight, bool fill) {
  if (fill) {
    const float scale = std::min(windowWidth / pictureWidth, windowHeight / pictureHeight);
    return {.left = (windowWidth - (pictureWidth * scale)) / 2.0F,
            .top = (windowHeight - (pictureHeight * scale)) / 2.0F,
            .width = pictureWidth * scale,
            .height = pictureHeight * scale};
  }
  const float scale = std::min(windowWidth / MovieScreenWidth, windowHeight / MovieScreenHeight);
  return {.left = (windowWidth - (MovieScreenWidth * scale)) / 2.0F,
          .top = ((windowHeight - (MovieScreenHeight * scale)) / 2.0F) + (MoviePictureTop * scale),
          .width = pictureWidth * scale,
          .height = pictureHeight * scale};
}

// The movie of a moment and whether Start ends it (mov.c: MOV_playMovie skips all but EDR).
struct MovieRule {
  std::string file;
  bool skippable = true;
};

// The movies section of the ruleset (rules/<id>/movies.yaml).
struct MovieRules {
  std::array<MovieRule, MovieMomentCount> movies;
  // Ticks without input on the title before the boot movie plays again (runLandingScreen: 600).
  std::uint32_t landingIdleFrames = 0;

  [[nodiscard]] const MovieRule& of(MovieMoment moment) const {
    return movies[static_cast<std::size_t>(moment)];
  }
};

inline constexpr std::string_view MoviesSection = "movies";

core::Result<MovieRules> readMovieRules(const ruleset::Section& section);

// The movies section of a ruleset of the packs; nothing, with a warning, when it cannot be read,
// and the game then plays no movies.
std::optional<MovieRules> loadMovieRules(const assets::VirtualFileSystem& files,
                                         std::string_view rulesetId);

} // namespace dwe::app
