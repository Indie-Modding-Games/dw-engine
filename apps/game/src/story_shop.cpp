#include "story_shop.hpp"

#include <dwe/core/log.hpp>

#include <cstdint>
#include <string>

namespace dwe::app {

namespace {

// getShopkeeperLine (script_common.c:1174): operand 0xFF is a shop without an owner.
constexpr std::int64_t NoOwner = 0xFF;

} // namespace

bool StoryShop::take(const session::StoryScreenRequest& request, const FieldScene& scene) {
  const bool recycle = request.screen == session::StoryScreen::RecycleShop;
  if (request.screen != session::StoryScreen::Shop && !recycle) {
    return false;
  }
  session::ShopVisit visit{
      .ownerless = request.first == NoOwner, .keeperSpecies = {}, .recycle = recycle};
  // scriptIdToEntityId: the keeper is the map's NPC with that script id, by its species.
  for (const auto& npc : scene.map().npcs) {
    if (!visit.ownerless && npc.scriptId == request.first && npc.digimon) {
      visit.keeperSpecies = *npc.digimon;
    }
  }
  core::logDebug("[port:shop] call_routine 8 (script_interp.c:430): {}shop of script id {}, "
                 "keeper '{}'",
                 recycle ? "recycle " : "", request.first, visit.keeperSpecies);
  if (shop_ != nullptr) {
    shop_->open(visit);
  }
  waiting_ = request.waits;
  return true;
}

void StoryShop::update(session::StoryScenes& scenes) {
  // A visit that did not open (no shop section, the recycle shop) is over at once.
  if (waiting_ && (shop_ == nullptr || !shop_->visit())) {
    waiting_ = false;
    scenes.screenClosed();
  }
}

} // namespace dwe::app
