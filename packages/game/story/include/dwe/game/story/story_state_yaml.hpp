#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/story/story_state.hpp>

#include <cstdint>

namespace dwe::game::story {

// The version of the saved story state. fromYaml reads only this one; the session's save brings
// older ones up to it with packages/game/save's MigrationChain and storyStateFromVersion1.
// Version 2 (the ruleset's capacity, #97) holds any number of trigger bytes and the extra
// variables.
inline constexpr std::int32_t StoryStateVersion = 2;

// The base variables a save keeps, 0-34 (main_menu.c: writeSavegame); the rest of the base set
// lives only while the game runs. The ruleset's extra variables are all saved.
inline constexpr std::int32_t SavedVariableCount = 35;

// The story state as the save's `story` section (docs/architecture/save.md): `triggers`, the
// trigger bits as hex digits, two per byte (byte k holds triggers 8k to 8k + 7, trigger n in bit
// n % 8, as the original keeps them: 100 bytes for the base set, then the extra ones); `variables`,
// the saved base variables in order; and `extra_variables`, the ruleset's extra variables in
// order.
core::YamlNode toYaml(const StoryState& state);

// Reads a story state toYaml wrote into a state of `capacity`; the unsaved base variables are 0.
// Triggers and extra variables the capacity does not hold (a save made with a mod that added them,
// loaded without it) are dropped with a warning when any was set; those it holds that the save
// lacks start clear. Errors name the line and column.
core::Result<StoryState> fromYaml(const core::YamlNode& node, StoryCapacity capacity = {});

// The migration step from version 1 (exactly the base's 100 trigger bytes and no extra variables)
// to version 2.
core::Result<core::YamlNode> storyStateFromVersion1(core::YamlNode state);

} // namespace dwe::game::story
