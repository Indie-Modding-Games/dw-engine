#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::game::story {
class StoryState;
}

namespace dwe::app {

// The console's and the scripts' story_set("trigger <n> on|off" | "var <n> <value>" |
// "money <bits>"): writes one of the story's triggers or variables, as the original's script state
// keeps them, or the bits the player carries, so a route can start from a point of the story its
// seeded game does not reach (the city's prosperity, variable 1; the fare of the bird transport). A
// debug aid: it runs no script and changes no rule.
inline constexpr std::string_view StorySetCommand = "story_set";

struct StoryWrite {
  enum class Kind : std::uint8_t { Trigger, Variable, Money };
  Kind kind = Kind::Trigger;
  std::int32_t number = 0;
  // The variable's value, 1 / 0 for a trigger set or cleared, or the bits.
  std::int32_t value = 0;
};

// Reads a story_set argument, or says what is wrong with it.
core::Result<StoryWrite> storyWriteFromText(std::string_view text);

// Writes a trigger or variable into the story's state; a number out of range, or a money write
// (the caller puts it in the bag), is an error and changes nothing.
core::Result<void> applyStoryWrite(game::story::StoryState& state, const StoryWrite& write);

} // namespace dwe::app
