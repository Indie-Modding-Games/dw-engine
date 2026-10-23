#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/story/partner_routines.hpp>
#include <dwe/game/story/screen_history.hpp>
#include <dwe/game/story/story_battle.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/game/story/story_routines.hpp>
#include <dwe/session/story_scenes.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace dwe::game::story {
class StoryRunner;
} // namespace dwe::game::story

namespace dwe::session {

class GameSession;

// What the story's battles, scenes and routines share: the rules, the queues the app takes, and the
// state the original keeps for them. The routines' files (story_routines/) work on it.
struct StoryScenesContext {
  explicit StoryScenesContext(StoryScenesDesc d) : desc{d} {}

  [[nodiscard]] game::story::StoryStatePort& state() const;
  [[nodiscard]] game::story::StoryRunner& runner() const;
  // The operand variable's value (pstat 254); 0, logged, when it cannot be read.
  [[nodiscard]] std::int64_t operand() const;
  [[nodiscard]] std::int64_t variable(std::int64_t variable) const;
  [[nodiscard]] bool trigger(std::int64_t trigger) const;
  // Asks the app for a screen; a screen that waits gives the task the section waits on.
  game::story::RoutineStart openScreen(StoryScreenRequest request);

  StoryScenesDesc desc;
  StoryScenesRules rules;
  std::optional<assets::DigimonTable> digimon;
  GameSession* session = nullptr;
  // Whether the ruleset has the section, and whether the orders are published in the story's VM.
  bool enabled = false;
  bool published = false;

  game::story::ScreenHistory screens;
  game::story::SceneSync sync;
  game::story::RoutineTable routines;
  std::unique_ptr<game::story::StoryBattlePort> battlePort;
  std::unique_ptr<game::story::PartnerScenePort> partnerPort;
  std::unique_ptr<game::story::StoryBattles> battles;
  std::unique_ptr<game::story::PartnerRoutines> partner;

  // The app's queues.
  std::optional<game::story::ScriptedBattle> battleRequest;
  std::optional<game::story::BattleOutcome> battleOutcome;
  std::deque<StoryScreenRequest> screenRequests;
  bool screenOpen = false;
  std::optional<PartnerSceneRequest> partnerScene;

  // The shop's tally of routines 30, 31 and 56 (script_interp.c: SHOP_VARIABLE).
  std::int64_t shopTally = 0;
  // The recruit triggers as last seen, to tell a new resident.
  std::vector<bool> recruits;
  // A new game whose map-head chain has not run yet.
  bool chainPending = false;
};

// The routines of the opening (story_routines/*.cpp), one file each; each registers its own.
core::Result<void> registerOpeningRoutines(StoryScenesContext& scenes);

} // namespace dwe::session
