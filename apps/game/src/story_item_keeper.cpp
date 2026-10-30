#include "story_item_keeper.hpp"

#include <dwe/core/log.hpp>

namespace dwe::app {

bool StoryItemKeeper::take(const session::StoryScreenRequest& request, const FieldScene& scene) {
  if (request.screen != session::StoryScreen::ItemKeeper) {
    return false;
  }
  // tickItemKeeper reads the keeper from variable 254 (setupBoxOrigin, showMapHeadTextbox): the
  // map's NPC with that script id, by its species.
  session::ItemKeeperVisit visit;
  for (const auto& npc : scene.map().npcs) {
    if (npc.scriptId == request.first && npc.digimon) {
      visit.keeperSpecies = *npc.digimon;
    }
  }
  core::logDebug("[port:keeper] call_routine 11 (script_interp.c): item keeper of script id {}, "
                 "keeper '{}'",
                 request.first, visit.keeperSpecies);
  if (keeper_ != nullptr) {
    keeper_->open(visit);
  }
  waiting_ = request.waits;
  return true;
}

void StoryItemKeeper::update(session::StoryScenes& scenes) {
  // A visit that did not open (no item_keeper section) is over at once.
  if (waiting_ && (keeper_ == nullptr || !keeper_->visit())) {
    waiting_ = false;
    scenes.screenClosed();
  }
}

} // namespace dwe::app
