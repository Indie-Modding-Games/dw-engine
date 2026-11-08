#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A game mode (docs/modding/modos.md): a way to play the engine's content, declared by a pack in
// modes/<id>/mode.yaml. The engine ships one, «Historia original».
namespace dwe::game::modes {

// The mode a game plays when nothing chose another: the original's story.
inline constexpr std::string_view OriginalMode = "original";

struct GameMode {
  // The mode's folder name, which mode.yaml repeats: a-z, 0-9 and '_', from a letter.
  std::string id;
  // Localization keys of its name and its line in the mode list.
  std::string nameKey;
  std::string descriptionKey;
  // The ruleset a new game of the mode starts with.
  std::string ruleset;
  // The rulesets the new game's rules screen may choose from for this mode; `ruleset` is one. With
  // only one, the screen is not shown.
  std::vector<std::string> rulesets;
  // Where the mode goes in the list: lower first, then by id.
  std::int32_t order = 0;

  friend bool operator==(const GameMode&, const GameMode&) = default;
};

// Parses the text of modes/<folder>/mode.yaml: exactly the keys id, name, description, ruleset,
// rulesets and order; the id must be the folder's. Errors name the line and column.
core::Result<GameMode> parseGameMode(std::string_view yaml, std::string_view folder);

} // namespace dwe::game::modes
