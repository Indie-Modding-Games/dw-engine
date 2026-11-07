#include <dwe/session/story_lock.hpp>

#include <doctest/doctest.h>

using dwe::session::StoryLock;
using dwe::session::StoryLockRules;

namespace {

// One stage that opens the first town and lets its exits lead only to the farm.
StoryLockRules oneStage() {
  return {.activeStage = "e1",
          .closedMessage = "closed",
          .stages = {{.id = "e1",
                      .maps = {"town_a", "farm_*"},
                      .exits = {{.map = "town_a", .open = {"farm_a"}}},
                      .sections = {}}}};
}

} // namespace

TEST_CASE("the story lock closes the original's maps beyond its stage") {
  const StoryLock lock{oneStage(), "e1"};
  REQUIRE(lock.locks());
  CHECK(lock.mapOpen("town_a"));
  CHECK(lock.mapOpen("farm_b"));
  CHECK_FALSE(lock.mapOpen("cave_a"));
  CHECK(lock.exitOpen("town_a", "farm_a"));
  CHECK_FALSE(lock.exitOpen("town_a", "farm_b"));
}

TEST_CASE("the story lock never closes a map a pack adds, nor an exit into one") {
  CHECK(dwe::session::isPackMap("my_mod:meadow"));
  CHECK_FALSE(dwe::session::isPackMap("town_a"));
  const StoryLock lock{oneStage(), "e1"};
  CHECK(lock.mapOpen("my_mod:meadow"));
  // The exits of town_a are narrowed to the farm, but a pack's map is outside the story.
  CHECK(lock.exitOpen("town_a", "my_mod:meadow"));
  CHECK(lock.exitOpen("my_mod:meadow", "town_a"));
  CHECK_FALSE(lock.exitOpen("my_mod:meadow", "cave_a"));
}
