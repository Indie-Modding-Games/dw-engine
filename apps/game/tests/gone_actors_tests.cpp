#include <dwe/scene/components.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <vector>

#include "gone_actors.hpp"

using dwe::app::dropGoneActors;
using dwe::scene::Entity;
using dwe::scene::Transform;
using dwe::scene::World;

namespace {

// What the field keeps of an NPC's actor here: its entity and the battle's hold on it.
struct HeldActor {
  Entity entity{};
  std::optional<int> fighter;
};

} // namespace

TEST_CASE("the NPC that ran off at a battle's start has no fighter for the field to take back") {
  // mgen02 (card #216): a wild fight with one more NPC on screen; it runs to its flee point
  // (holdFighter) and leaves the map at the intro's end (battle_setup.c:316) while the field does
  // not tick. Its actor still held a fighter when the battle ended, and the release read the
  // Transform of an entity that was gone.
  World world;
  std::vector<std::unique_ptr<HeldActor>> actors;
  for (int i = 0; i < 3; ++i) {
    const Entity entity = world.create();
    world.registry().emplace<Transform>(entity);
    actors.push_back(std::make_unique<HeldActor>(HeldActor{.entity = entity, .fighter = i}));
  }
  const Entity enemy = actors[0]->entity;
  const Entity ranOff = actors[1]->entity;
  world.destroy(ranOff);
  // A new NPC after it may take its slot again with another version: still not the same one.
  const Entity arrived = world.create();
  world.registry().emplace<Transform>(arrived);

  std::vector<Entity> dropped;
  dropGoneActors(actors, world, [&](const HeldActor& actor) { dropped.push_back(actor.entity); });

  CHECK(dropped == std::vector<Entity>{ranOff});
  REQUIRE(actors.size() == 2);
  CHECK(actors[0]->entity == enemy);
  // Every actor left can be let go: its entity has its Transform.
  for (const std::unique_ptr<HeldActor>& actor : actors) {
    CHECK(world.registry().all_of<Transform>(actor->entity));
  }
}

TEST_CASE("with every NPC still on the map no actor is dropped") {
  World world;
  std::vector<std::unique_ptr<HeldActor>> actors;
  actors.push_back(
      std::make_unique<HeldActor>(HeldActor{.entity = world.create(), .fighter = std::nullopt}));
  int dropped = 0;
  dropGoneActors(actors, world, [&](const HeldActor&) { ++dropped; });
  CHECK(dropped == 0);
  CHECK(actors.size() == 1);
}
