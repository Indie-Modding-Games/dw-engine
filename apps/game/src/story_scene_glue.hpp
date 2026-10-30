#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/session/story_scenes.hpp>

#include <cstdint>
#include <vector>

#include "battle_screen.hpp"
#include "card_shop_screen.hpp"
#include "curling_screen.hpp"
#include "death_scene.hpp"
#include "discard_screen.hpp"
#include "egg_box_screen.hpp"
#include "field_scene.hpp"
#include "jukebox_screen.hpp"
#include "merit_shop_screen.hpp"
#include "save_machine.hpp"
#include "story_gym.hpp"
#include "story_item_keeper.hpp"
#include "story_naming.hpp"
#include "story_shop.hpp"
#include "story_tournament.hpp"
#include "trade_screen.hpp"

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
  // The tournaments the story's Tournament screen plays (call_routine 6).
  void attachTournament(session::TournamentAdapter* tournament, session::GameSession* session) {
    tournament_.attach(tournament, session);
  }
  // The stones game the story's curling scene plays (call_partner_routine 1).
  void attachCurling(CurlingScreen* curling) { curling_ = curling; }
  // The scenes the story's lost lives, deaths and rebirths play on, and the egg box between them.
  void attachDeath(DeathScene* death) { death_ = death; }
  void attachEggBox(EggBoxScreen* eggBox) { eggBox_ = eggBox; }
  // The item menu the story's DiscardItem screen opens (call_routine 0).
  void attachDiscard(DiscardScreen* discard) { discard_ = discard; }
  // The jukebox the story's Jukebox screen opens (call_routine 9).
  void attachJukebox(JukeboxScreen* jukebox) { jukebox_ = jukebox; }
  // The item trader the story's TradeShop screen opens (call_routine 2).
  void attachTrade(TradeScreen* trade) { trade_ = trade; }
  // The card shop's menus the story's CardBuy and CardSell screens open (call_routine 14).
  void attachCardShop(CardShopScreen* cardShop) { cardShop_ = cardShop; }
  // The merit exchange the story's MeritShop screen opens (call_routine 1).
  void attachMeritShop(MeritShopScreen* meritShop) { meritShop_ = meritShop; }
  // The save machine the story's SaveMenu screen opens (call_routine 48).
  void attachSaveMachine(SaveMachine* machine) { saveMachine_ = machine; }
  // The naming box the story's renaming opens (call_routine 32).
  void attachNaming(NamingBox* box, session::GameSession* session) { naming_.attach(box, session); }
  // One engine tick of the player's buttons for the story's naming box; true when it took them.
  bool pressNaming(const input::InputState& input) { return naming_.press(input); }

private:
  bool takeSaveMachine(const session::StoryScreenRequest& request);
  void startBattle(session::StoryScenes& scenes, BattleScreen& battle,
                   const assets::DigimonTable* species, const FieldScene& scene,
                   const game::story::ScriptedBattle& request);

  // Whether the battle on the screen is the story's.
  bool storyBattle_ = false;
  StoryShop shop_;
  StoryItemKeeper itemKeeper_;
  StoryGym gym_;
  StoryTournament tournament_;
  DeathScene* death_ = nullptr;
  EggBoxScreen* eggBox_ = nullptr;
  DiscardScreen* discard_ = nullptr;
  JukeboxScreen* jukebox_ = nullptr;
  TradeScreen* trade_ = nullptr;
  CardShopScreen* cardShop_ = nullptr;
  MeritShopScreen* meritShop_ = nullptr;
  // Whether the story waits on a scene of the death scene.
  bool deathScene_ = false;
  CurlingScreen* curling_ = nullptr;
  // Whether the story waits on the stones game.
  bool curlingScene_ = false;
  StoryNaming naming_;
  SaveMachine* saveMachine_ = nullptr;
  // Whether the story waits on the save machine.
  bool saveMachineWaits_ = false;
};

} // namespace dwe::app
