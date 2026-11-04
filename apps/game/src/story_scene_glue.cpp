#include "story_scene_glue.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/render/map_layers.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

void restartObjectAnimations(render::MapDepthLayers& layers, std::int64_t first,
                             std::int64_t count) {
  core::logDebug("[port:story] resetMapObjectAnimation (overworld.c:1002): objects {} to {}", first,
                 first + count - 1);
  for (std::int64_t object = std::max<std::int64_t>(first, 0); object < first + count; ++object) {
    // restartAnimation leaves an object the map lacks alone; the original writes past its objects.
    layers.restartAnimation(static_cast<std::size_t>(object));
  }
}

void StorySceneGlue::update(session::StoryScenes& scenes, BattleScreen& battle,
                            const assets::DigimonTable* species, FieldScene& scene) {
  if (const auto outcome = battle.takeOutcome(); outcome && storyBattle_) {
    storyBattle_ = false;
    scenes.battleFinished(storyOutcome(*outcome));
  } else if (outcome && tournament_.matchRunning()) {
    tournament_.matchOver(*outcome);
  } else if (outcome == game::battle::Outcome::Fled) {
    // A flee the story did not ask for has no map change after it: the black screen fades back in.
    scene.field().releaseShade();
  }
  if (const auto request = scenes.takeBattleRequest()) {
    startBattle(scenes, battle, species, scene, *request);
  }
  shop_.update(scenes);
  itemKeeper_.update(scenes);
  gym_.update(scenes);
  tournament_.update(scenes, battle, scene);
  if (eggBox_ != nullptr) {
    eggBox_->update(scenes);
  }
  naming_.update(scenes);
  if (discard_ != nullptr) {
    discard_->update(scenes);
  }
  if (jukebox_ != nullptr) {
    jukebox_->update(scenes);
  }
  if (schedule_ != nullptr) {
    schedule_->update(scenes);
  }
  if (trade_ != nullptr) {
    trade_->update(scenes);
  }
  if (bird_ != nullptr) {
    bird_->update(scenes);
  }
  if (cardShop_ != nullptr) {
    cardShop_->update(scenes);
  }
  if (meritShop_ != nullptr) {
    meritShop_->update(scenes);
  }
  if (saveMachine_ != nullptr && saveMachine_->takeClosed() && saveMachineWaits_) {
    saveMachineWaits_ = false;
    scenes.screenClosed();
  }
  while (const auto screen = scenes.takeScreenRequest()) {
    if (shop_.take(*screen, scene) || itemKeeper_.take(*screen, scene) || gym_.take(*screen) ||
        naming_.take(*screen) || tournament_.take(*screen, scenes, scene) ||
        (eggBox_ != nullptr && eggBox_->take(*screen, scenes)) ||
        (discard_ != nullptr && discard_->take(*screen)) ||
        (jukebox_ != nullptr && jukebox_->take(*screen, scene)) ||
        (schedule_ != nullptr && schedule_->take(*screen, scene)) ||
        (trade_ != nullptr && trade_->take(*screen, scene)) ||
        (bird_ != nullptr && bird_->take(*screen, scene)) ||
        (cardShop_ != nullptr && cardShop_->take(*screen, scene)) ||
        (meritShop_ != nullptr && meritShop_->take(*screen, scene)) ||
        (moneyBox_ != nullptr && moneyBox_->take(*screen, scene)) || takeSaveMachine(*screen)) {
      continue;
    }
    // Routine 38 (overworld.c: createNinjamonEffect): the smoke runs on its own while the section
    // goes on.
    if (screen->screen == session::StoryScreen::SmokeEffect) {
      scene.startObjectSmoke();
      if (screen->waits) {
        scenes.screenClosed();
      }
      continue;
    }
    // Routine 21 (overworld.c: resetMapObjectAnimation): the `second` map objects from `first` play
    // their sprites again from the first frame, as Elecmon's sparks each time it shocks the player;
    // the section goes on.
    if (screen->screen == session::StoryScreen::ObjectAnimation) {
      restartObjectAnimations(scene.mapLayers(), screen->first, screen->second);
      if (screen->waits) {
        scenes.screenClosed();
      }
      continue;
    }
    core::logDebug("[port:story] call_routine {}: the app has no screen {} yet, closed at once",
                   screen->routine, static_cast<int>(screen->screen));
    if (screen->waits) {
      scenes.screenClosed();
    }
  }
  if (deathScene_ && death_->takeSceneOver()) {
    deathScene_ = false;
    scenes.partnerSceneFinished();
  }
  if (curlingScene_ && curling_->takeOver()) {
    curlingScene_ = false;
    scenes.partnerSceneFinished();
  }
  if (bossScene_ && boss_->takeOver()) {
    bossScene_ = false;
    scenes.partnerSceneFinished();
  }
  if (endingScene_ && ending_->takeOver()) {
    endingScene_ = false;
    scenes.partnerSceneFinished();
  }
  if (const auto partner = scenes.takePartnerScene()) {
    if (scenes.playsDeath() && death_ != nullptr && death_->play(partner->scene)) {
      deathScene_ = true;
      return;
    }
    // partner.c: callDigimonRoutine 1 starts the stones game (KAR_start); the story waits for its
    // end (KAR_finishMatch sets SOME_SCRIPT_SYNC_BIT).
    if (partner->scene == game::story::PartnerScene::Curling && curling_ != nullptr &&
        curling_->start(scene)) {
      curlingScene_ = true;
      return;
    }
    // tamer.c: tamerTickMachinedramonSpawn plays the final boss's arrival (EAB_tick) and sets
    // SOME_SCRIPT_SYNC_BIT when it ends.
    if (partner->scene == game::story::PartnerScene::BossAppears && boss_ != nullptr) {
      boss_->startArrival();
      bossScene_ = true;
      return;
    }
    // partner.c: callDigimonRoutine 6 gives the player its ending tick (tamerTickEnding), which
    // sets SOME_SCRIPT_SYNC_BIT once endi.c's scene is over.
    if (ending_ != nullptr && ending_->play(partner->scene)) {
      endingScene_ = true;
      return;
    }
    // callDigimonRoutine 5 gives the player its opening tick (tamerTickOpening). Only the debug
    // room's script calls it: it waits two frames and turns the map layer back on, which the engine
    // needs for nothing, so it is over at once here like any routine without a scene.
    core::logDebug("[port:partner] callDigimonRoutine {} (partner.c:1139): the app has no scene "
                   "for it yet, over at once",
                   static_cast<int>(partner->scene));
    scenes.partnerSceneFinished();
  }
}

// script_interp.c: call_routine 48 opens the save machine (main_menu.c: openSaveMachine) and
// call_routine 54 its save's question for the ending (gameClearSave); the script goes on once it
// closes.
bool StorySceneGlue::takeSaveMachine(const session::StoryScreenRequest& request) {
  const bool machine = request.screen == session::StoryScreen::SaveMenu;
  if ((!machine && request.screen != session::StoryScreen::ClearSave) || saveMachine_ == nullptr) {
    return false;
  }
  if (machine) {
    saveMachine_->open();
  } else {
    saveMachine_->openClearSave();
  }
  saveMachineWaits_ = request.waits;
  return true;
}

// The enemies are the map's NPCs by script id (MapNpc::scriptId). A battle with none predefined is
// the one against the NPC the player talked to (btl.c: handleBattleStart), which the story names,
// with the NPCs on screen that join it.
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
  if (request.joinable && npcs.size() == 1) {
    for (const std::int32_t joiner :
         battle.joiningNpcs(mapNpcs[npcs.front()].scriptId, request.fixedFight)) {
      for (std::size_t index = 0; index < mapNpcs.size(); ++index) {
        if (mapNpcs[index].scriptId == joiner) {
          npcs.push_back(index);
        }
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
    return battle.start(std::move(battleRequest->request), std::move(battleRequest->enemyNpcs),
                        request.fixedFight);
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
