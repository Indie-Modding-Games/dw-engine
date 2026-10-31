#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::game::story {
class StoryState;
}
namespace dwe::session {
class CardsAdapter;
}

namespace dwe::app {

// The console's and the scripts' story_set("trigger <n> on|off" | "var <n> <value>" |
// "money <bits>" | "card <n> <copies>"): writes one of the story's triggers or variables, as the
// original's script state keeps them, the bits the player carries, or gives copies of a card, so a
// route can start from a point of the story its seeded game does not reach (the city's prosperity,
// variable 1; the fare of the bird transport; the cards a collector asks for). A debug aid: it runs
// no script and changes no rule.
inline constexpr std::string_view StorySetCommand = "story_set";

struct StoryWrite {
  enum class Kind : std::uint8_t { Trigger, Variable, Money, Card };
  Kind kind = Kind::Trigger;
  std::int32_t number = 0;
  // The variable's value, 1 / 0 for a trigger set or cleared, the bits, or the copies of a card to
  // give.
  std::int32_t value = 0;
};

// Reads a story_set argument, or says what is wrong with it.
core::Result<StoryWrite> storyWriteFromText(std::string_view text);

// Writes a trigger or variable into the story's state; a number out of range, or a money or card
// write (the caller puts it in the bag or the card album), is an error and changes nothing.
core::Result<void> applyStoryWrite(game::story::StoryState& state, const StoryWrite& write);

// Gives a card write's copies (as the story's give_card does, one at a time).
core::Result<void> applyCardWrite(session::CardsAdapter& cards, const StoryWrite& write);

} // namespace dwe::app
