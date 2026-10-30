#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/session/story_scenes.hpp>

#include <cstdint>
#include <vector>

#include "battle_screen.hpp"
#include "death_scene.hpp"
#include "discard_screen.hpp"
#include "egg_box_screen.hpp"
#include "field_scene.hpp"
#include "story_gym.hpp"
#include "story_item_keeper.hpp"
#include "story_naming.hpp"
#include "story_shop.hpp"

namespace dwe::app {

// The app's side of the story's scenes (session::StoryScenes): it starts the battles the story
// asks for on the battle screen and reports their outcome; the menus and the partner's scenes the
// app has no screen for yet close at once, logged.
class StorySceneGlue {
public:
  void update(session::StoryScenes& scenes, BattleScreen& battle,
              const assets::DigimonTable* species, FieldScene& scene);
  // The shop the story's Shop screen opens.
  void attachShop(session::ShopCounter* shop) { shop_.attach(shop); }
  // The item keeper the story's ItemKeeper screen opens.
  void attachItemKeeper(session::ItemKeeper* keeper) { itemKeeper_.attach(keeper); }
  // The gym the story's GymLibrary and GymCourse screens run.
  void attachGym(GymScreen* gym) { gym_.attach(gym); }
  // The scenes the story's lost lives, deaths and rebirths play on, and the egg box between them.
  void attachDeath(DeathScene* death) { death_ = death; }
  void attachEggBox(EggBoxScreen* eggBox) { eggBox_ = eggBox; }
  // The item menu the story's DiscardItem screen opens (call_routine 0).
  void attachDiscard(DiscardScreen* discard) { discard_ = discard; }
  // The naming box the story's renaming opens (call_routine 32).
  void attachNaming(NamingBox* box, session::GameSession* session) { naming_.attach(box, session); }
  // One engine tick of the player's buttons for the story's naming box; true when it took them.
  bool pressNaming(const input::InputState& input) { return naming_.press(input); }

private:
  void startBattle(session::StoryScenes& scenes, BattleScreen& battle,
                   const assets::DigimonTable* species, const FieldScene& scene,
                   const game::story::ScriptedBattle& request);

  // Whether the battle on the screen is the story's.
  bool storyBattle_ = false;
  StoryShop shop_;
  StoryItemKeeper itemKeeper_;
  StoryGym gym_;
  DeathScene* death_ = nullptr;
  EggBoxScreen* eggBox_ = nullptr;
  DiscardScreen* discard_ = nullptr;
  // Whether the story waits on a scene of the death scene.
  bool deathScene_ = false;
  StoryNaming naming_;
};

} // namespace dwe::app
