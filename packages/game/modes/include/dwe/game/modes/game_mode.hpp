#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::modes {

inline constexpr std::string_view ClassicMode = "classic";
inline constexpr std::string_view FolderMode = "folder";
inline constexpr std::string_view LegacyStoryMode = "original";
inline constexpr std::string_view DefaultMode = FolderMode;

[[nodiscard]] std::string_view currentModeId(std::string_view saved);

struct OptionPin {
  std::string option;
  std::string value;

  friend bool operator==(const OptionPin&, const OptionPin&) = default;
};

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
  std::vector<OptionPin> options;

  friend bool operator==(const GameMode&, const GameMode&) = default;
};

core::Result<GameMode> parseGameMode(std::string_view yaml, std::string_view folder);

[[nodiscard]] bool takesProgressFrom(const GameMode& save, const GameMode& world);

} // namespace dwe::game::modes
