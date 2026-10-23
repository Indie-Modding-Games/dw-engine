#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/session/story_scenes.hpp>

#include "battle_screen.hpp"
#include "death_scene.hpp"
#include "egg_box_screen.hpp"
#include "field_scene.hpp"
#include "story_gym.hpp"
#include "story_shop.hpp"

namespace dwe::app {

// The app's side of the story's scenes (session::StoryScenes): it starts the battles the story
// asks for on the battle screen and reports their outcome; the menus and the partner's scenes the
// app has no screen for yet close at once, logged.
class StorySceneGlue {
public:
  void update(session::StoryScenes& scenes, BattleScreen& battle,
              const assets::DigimonTable* species, const FieldScene& scene);
  // The shop the story's Shop screen opens.
  void attachShop(session::ShopCounter* shop) { shop_.attach(shop); }
  // The gym the story's GymLibrary and GymCourse screens run.
  void attachGym(GymScreen* gym) { gym_.attach(gym); }
  // The scenes the story's lost lives, deaths and rebirths play on, and the egg box between them.
  void attachDeath(DeathScene* death) { death_ = death; }
  void attachEggBox(EggBoxScreen* eggBox) { eggBox_ = eggBox; }

private:
  void startBattle(session::StoryScenes& scenes, BattleScreen& battle,
                   const assets::DigimonTable* species, const FieldScene& scene,
                   const game::story::ScriptedBattle& request);

  // Whether the battle on the screen is the story's.
  bool storyBattle_ = false;
  StoryShop shop_;
  StoryGym gym_;
  DeathScene* death_ = nullptr;
  EggBoxScreen* eggBox_ = nullptr;
  // Whether the story waits on a scene of the death scene.
  bool deathScene_ = false;
};

} // namespace dwe::app
