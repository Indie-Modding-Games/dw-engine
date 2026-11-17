#include "player_commands.hpp"

#include <dwe/input/action_map.hpp>
#include <dwe/input/keys.hpp>

#include <doctest/doctest.h>

using namespace dwe::app;

TEST_CASE("the local controller emits numbered gameplay commands") {
  LocalController controller;
  dwe::input::InputState input{dwe::input::defaultActionMap()};
  const WalkOrder walk{.stick = {0.5F, -0.25F}, .moveTarget = glm::vec3{10.0F, 0.0F, 20.0F}};

  controller.sample(input, walk, {.x = 1.0F, .y = -1.0F}, true);
  const PlayerCommands commands = controller.next(0);

  CHECK(commands.sequence == 0);
  CHECK(commands.field.stick == walk.stick);
  CHECK(commands.field.moveTarget == walk.moveTarget);
  CHECK(commands.field.run);
  CHECK_FALSE(commands.field.cancelTarget);
  CHECK_FALSE(commands.field.confirm);
  CHECK_FALSE(commands.fishing.left);
  CHECK(commands.fishing.right);
  CHECK(commands.fishing.up);
  CHECK_FALSE(commands.fishing.down);
  CHECK_FALSE(commands.fishing.confirmDown);
  CHECK_FALSE(commands.story.confirmPressed);
  CHECK_FALSE(commands.story.confirmDown);
  CHECK_FALSE(commands.story.cancelPressed);

  dwe::input::InputState storyInput{dwe::input::defaultActionMap()};
  storyInput.handle(dwe::platform::KeyChanged{.key = dwe::input::keys::Return,
                                               .down = true,
                                               .repeat = false});
  storyInput.handle(dwe::platform::KeyChanged{.key = dwe::input::keys::Up,
                                               .down = true,
                                               .repeat = false});
  storyInput.handle(dwe::platform::KeyChanged{.key = dwe::input::keys::Tab,
                                               .down = true,
                                               .repeat = false});
  controller.sample(storyInput, {}, {}, false);
  const PlayerCommands storyCommands = controller.next(1);
  CHECK(storyCommands.story.confirmPressed);
  CHECK(storyCommands.story.confirmPressedByKeyOrPad);
  CHECK(storyCommands.story.confirmDown);
  CHECK(storyCommands.story.upPressed);
  CHECK(storyCommands.story.menuPressed);
  CHECK(storyCommands.story.menuDown);

  controller.neutral();
  const PlayerCommands neutral = controller.next(2);
  CHECK(neutral.sequence == 2);
  CHECK(neutral.field.stick == glm::vec2{});
  CHECK_FALSE(neutral.field.moveTarget.has_value());
  CHECK_FALSE(neutral.fishing.right);
  CHECK_FALSE(neutral.story.confirmPressed);

  controller.reset();
  controller.neutral();
  CHECK(controller.next(0).sequence == 0);
}

TEST_CASE("the scripted controller emits its commands once then stays neutral") {
  PlayerCommands first;
  first.field.stick = {1.0F, 0.0F};
  first.story.confirmPressed = true;
  PlayerCommands second;
  second.field.run = true;
  ScriptedController controller{{first, second}};

  const PlayerCommands a = controller.next(0);
  CHECK(a.sequence == 0);
  CHECK(a.field.stick == glm::vec2{1.0F, 0.0F});
  CHECK(a.story.confirmPressed);

  const PlayerCommands b = controller.next(1);
  CHECK(b.sequence == 1);
  CHECK(b.field.run);
  CHECK_FALSE(b.story.confirmPressed);

  const PlayerCommands neutral = controller.next(2);
  CHECK(neutral.sequence == 2);
  CHECK(neutral.field.stick == glm::vec2{});
  CHECK_FALSE(neutral.field.run);
  CHECK_FALSE(neutral.story.confirmPressed);

  controller.reset();
  CHECK(controller.next(0).sequence == 0);
  CHECK(controller.next(1).field.run);
}
