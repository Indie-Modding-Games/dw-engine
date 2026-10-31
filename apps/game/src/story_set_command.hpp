#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::game::story {
class StoryState;
}

namespace dwe::app {

// The console's and the scripts' story_set("trigger <n> on|off" | "var <n> <value>"): writes one of
// the story's triggers or variables, as the original's script state keeps them, so a route can
// start from a point of the story its seeded game does not reach (the city's prosperity, variable
// 1). A debug aid: it runs no script and changes no rule.
inline constexpr std::string_view StorySetCommand = "story_set";

struct StoryWrite {
  enum class Kind : std::uint8_t { Trigger, Variable };
  Kind kind = Kind::Trigger;
  std::int32_t number = 0;
  // The variable's value, or 1 / 0 for a trigger set or cleared.
  std::int32_t value = 0;
};

// Reads a story_set argument, or says what is wrong with it.
core::Result<StoryWrite> storyWriteFromText(std::string_view text);

// Writes it into the story's state; a number out of range is an error and changes nothing.
core::Result<void> applyStoryWrite(game::story::StoryState& state, const StoryWrite& write);

} // namespace dwe::app
