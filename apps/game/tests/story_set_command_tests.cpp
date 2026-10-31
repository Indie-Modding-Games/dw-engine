#include <dwe/game/story/story_state.hpp>

#include <doctest/doctest.h>

#include "story_set_command.hpp"

using dwe::app::applyStoryWrite;
using dwe::app::StoryWrite;
using dwe::app::storyWriteFromText;

TEST_CASE("story_set reads a trigger or a variable write") {
  const auto trigger = storyWriteFromText("trigger 86 on");
  REQUIRE(trigger);
  CHECK(trigger->kind == StoryWrite::Kind::Trigger);
  CHECK(trigger->number == 86);
  CHECK(trigger->value == 1);
  const auto cleared = storyWriteFromText("trigger 3  off");
  REQUIRE(cleared);
  CHECK(cleared->value == 0);
  const auto variable = storyWriteFromText("var 1 10");
  REQUIRE(variable);
  CHECK(variable->kind == StoryWrite::Kind::Variable);
  CHECK(variable->number == 1);
  CHECK(variable->value == 10);
  const auto money = storyWriteFromText("money 1800");
  REQUIRE(money);
  CHECK(money->kind == StoryWrite::Kind::Money);
  CHECK(money->value == 1800);
}

TEST_CASE("story_set refuses what it cannot write") {
  for (const auto* text :
       {"", "var 1", "var 1 256", "var 1 -1", "var x 2", "trigger 86 yes", "flag 1 2", "var 1 2 3",
        "money", "money -1", "money 1000000", "money 5 5"}) {
    CAPTURE(text);
    CHECK_FALSE(storyWriteFromText(text));
  }
}

TEST_CASE("story_set writes into the story's state and leaves it alone when out of range") {
  dwe::game::story::StoryState state;
  REQUIRE(applyStoryWrite(state, *storyWriteFromText("var 1 10")));
  REQUIRE(applyStoryWrite(state, *storyWriteFromText("trigger 86 on")));
  CHECK(state.variable(1).value() == 10);
  CHECK(state.trigger(86).value());
  REQUIRE(applyStoryWrite(state, *storyWriteFromText("trigger 86 off")));
  CHECK_FALSE(state.trigger(86).value());
  CHECK_FALSE(applyStoryWrite(state, *storyWriteFromText("trigger 800 on")));
  CHECK_FALSE(applyStoryWrite(state, *storyWriteFromText("var 256 1")).has_value());
}
