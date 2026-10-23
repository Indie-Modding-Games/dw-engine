#include "story_scene_glue.hpp"

#include <dwe/core/log.hpp>

#include <cstddef>
#include <vector>

#include "battle_trigger.hpp"

namespace dwe::app {

namespace {

game::story::BattleOutcome storyOutcome(game::battle::Outcome outcome) {
  switch (outcome) {
  case game::battle::Outcome::Victory:
    return game::story::BattleOutcome::Victory;
  case game::battle::Outcome::Fled:
    return game::story::BattleOutcome::Fled;
  case game::battle::Outcome::Defeat:
    return game::story::BattleOutcome::Defeat;
  }
  return game::story::BattleOutcome::Victory;
}

} // namespace

void StorySceneGlue::update(session::StoryScenes& scenes, BattleScreen& battle,
                            const assets::DigimonTable* species, const FieldScene& scene) {
  if (const auto outcome = battle.takeOutcome(); outcome && storyBattle_) {
    storyBattle_ = false;
    scenes.battleFinished(storyOutcome(*outcome));
  }
  if (const auto request = scenes.takeBattleRequest()) {
    startBattle(scenes, battle, species, scene, *request);
  }
  shop_.update(scenes);
  while (const auto screen = scenes.takeScreenRequest()) {
    if (shop_.take(*screen, scene)) {
      continue;
    }
    core::logDebug("[port:story] call_routine {}: the app has no screen {} yet, closed at once",
                   screen->routine, static_cast<int>(screen->screen));
    if (screen->waits) {
      scenes.screenClosed();
    }
  }
  if (const auto partner = scenes.takePartnerScene()) {
    core::logDebug("[port:partner] callDigimonRoutine {} (partner.c:1139): the app has no scene "
                   "for it yet, over at once",
                   static_cast<int>(partner->scene));
    scenes.partnerSceneFinished();
  }
}

// The enemies are the map's NPCs by script id (MapNpc::scriptId). A battle with none named is the
// one against the NPC the player talks to (btl.c: handleBattleStart), which the field does not
// tell yet.
void StorySceneGlue::startBattle(session::StoryScenes& scenes, BattleScreen& battle,
                                 const assets::DigimonTable* species, const FieldScene& scene,
                                 const game::story::ScriptedBattle& request) {
  std::vector<std::size_t> npcs;
  const auto& mapNpcs = scene.map().npcs;
  for (const std::int64_t enemy : request.enemies) {
    for (std::size_t index = 0; index < mapNpcs.size(); ++index) {
      if (mapNpcs[index].scriptId == enemy) {
        npcs.push_back(index);
      }
    }
  }
  auto started = [&]() -> core::Result<void> {
    if (species == nullptr || npcs.empty()) {
      return core::fail("no enemy of the map's NPCs");
    }
    auto battleRequest = mapBattleRequest(scene.field(), scene.map(), *species, npcs);
    if (!battleRequest) {
      return std::unexpected{battleRequest.error()};
    }
    return battle.start(std::move(*battleRequest));
  }();
  if (!started) {
    core::logWarning("[port:battle] startBattle (tamer.c:832): {}; the story reads a victory",
                     started.error().message);
    scenes.battleFinished(game::story::BattleOutcome::Victory);
    return;
  }
  storyBattle_ = true;
}

} // namespace dwe::app
