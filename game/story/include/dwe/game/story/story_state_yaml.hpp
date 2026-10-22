#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/story/story_state.hpp>

#include <cstdint>

namespace dwe::game::story {

// The version of the saved story state. fromYaml reads only this one; the session's save brings older ones up to it
// with game/save's MigrationChain.
inline constexpr std::int32_t StoryStateVersion = 1;

// The variables a save keeps, 0-34 (main_menu.c: writeSavegame); the rest live only while the game runs.
inline constexpr std::int32_t SavedVariableCount = 35;

// The story state as the save's `story` section (docs/architecture/save.md): `triggers`, the 100 bytes of trigger
// bits as 200 hex digits (byte k holds triggers 8k to 8k + 7, trigger n in bit n % 8, as the original keeps them),
// and `variables`, the saved variables in order.
core::YamlNode toYaml(const StoryState& state);

// Reads a story state toYaml wrote; the unsaved variables are 0. Errors name the line and column.
core::Result<StoryState> fromYaml(const core::YamlNode& node);

} // namespace dwe::game::story
