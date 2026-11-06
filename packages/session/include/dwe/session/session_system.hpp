#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>

namespace dwe::session {

class GameSession;

// The player praised or scolded the partner: the discipline and happiness it had before.
struct PraiseOrScold {
  bool scolded = false;
  std::int32_t disciplineBefore = 0;
  std::int32_t happinessBefore = 0;
};

// A game system the session drives besides its core (clock, field, partner, save). Evolution,
// battle and inventory each plug in as one adapter file that implements this and turns the system's
// own structs into the partner's and back (docs/modulos/apps/session.md), so adding one changes no
// other system.
class SessionSystem {
public:
  SessionSystem() = default;
  virtual ~SessionSystem() = default;
  SessionSystem(const SessionSystem&) = delete;
  SessionSystem& operator=(const SessionSystem&) = delete;
  SessionSystem(SessionSystem&&) = delete;
  SessionSystem& operator=(SessionSystem&&) = delete;

  // The checks of the system's ruleset sections, given to the ruleset store with the others.
  [[nodiscard]] virtual ruleset::SectionChecks sectionChecks() const { return {}; }

  // Reads the system's sections, when the session starts and after each accepted ruleset change.
  virtual core::Result<void> readRules(const ruleset::Ruleset& /*rules*/) { return {}; }

  // Registers the system's save sections; the serializers live as long as the system.
  virtual core::Result<void> addSaveSections(game::save::SaveSections& /*sections*/) { return {}; }

  // Sets up the system's state for a new game, after the partner's.
  virtual core::Result<void> startNewGame(GameSession& /*session*/) { return {}; }

  // Runs on each tick after the partner's care, with what the game clock crossed.
  virtual void tick(GameSession& /*session*/, const ruleset::ClockAdvance& /*advance*/) {}

  // Sees each partner event after the session routed it (evolution reads VirusBarFull, for
  // example).
  virtual void routePartnerEvent(GameSession& /*session*/,
                                 const game::partner::PartnerEvent& /*event*/) {}

  // Runs after the player praised or scolded the partner, before its events are routed.
  virtual void afterPraiseOrScold(GameSession& /*session*/, const PraiseOrScold& /*event*/) {}

  // Runs when the partner wakes up after a sleep, before its events are routed.
  virtual void afterWakeUp(GameSession& /*session*/) {}

  // Runs before a save of the player's writes the sections (main_menu.c: writeSavegame), not before
  // an autosave, which the original does not have.
  virtual void beforeSave(GameSession& /*session*/) {}

  // Runs after a save was loaded and every section read, before the game goes on.
  virtual void afterLoad(GameSession& /*session*/) {}
};

} // namespace dwe::session
