#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/session/story_scenes.hpp>

#include "battle_screen.hpp"
#include "field_scene.hpp"

namespace dwe::app {

// The app's side of the story's scenes (session::StoryScenes): it starts the battles the story
// asks for on the battle screen and reports their outcome; the menus and the partner's scenes the
// app has no screen for yet close at once, logged.
class StorySceneGlue {
public:
  void update(session::StoryScenes& scenes, BattleScreen& battle,
              const assets::DigimonTable* species, const FieldScene& scene);

private:
  void startBattle(session::StoryScenes& scenes, BattleScreen& battle,
                   const assets::DigimonTable* species, const FieldScene& scene,
                   const game::story::ScriptedBattle& request);

  // Whether the battle on the screen is the story's.
  bool storyBattle_ = false;
};

} // namespace dwe::app
