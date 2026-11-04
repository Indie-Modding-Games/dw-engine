#pragma once

#include <dwe/scene/world.hpp>

#include <memory>
#include <vector>

namespace dwe::app {

// Takes out of `actors` those whose entity left the world, handing each to `gone` first (to free
// what the scene keeps for it, such as its model). The field's actors for the map's NPCs follow
// the NPCs the field holds, but an NPC can leave the map while the field does not tick: the battle
// takes off the NPCs that ran off once its intro ends (battle_setup.c:316, scriptUnloadEntity
// clears their entity slot), so until the field ticks again their actors stand for nothing, and
// whoever walks the list first (FieldScene::releaseFighters at the battle's end) drops them.
template <typename Actor, typename Gone>
void dropGoneActors(std::vector<std::unique_ptr<Actor>>& actors, const scene::World& world,
                    Gone&& gone) {
  std::erase_if(actors, [&](const std::unique_ptr<Actor>& actor) {
    if (world.alive(actor->entity)) {
      return false;
    }
    gone(*actor);
    return true;
  });
}

} // namespace dwe::app
