#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

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
