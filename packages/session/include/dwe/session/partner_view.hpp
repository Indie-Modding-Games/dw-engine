#pragma once

#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/partner/effects.hpp>
#include <dwe/game/partner/need_deadlines.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/game/partner/state.hpp>

#include <cstdint>
#include <string>

namespace dwe::session {

// A value with the range it moves in, for a bar of the UI.
struct CareMeter {
  std::int32_t value = 0;
  std::int32_t min = 0;
  std::int32_t max = 0;

  // Where the value is between min and max, from 0 to 1.
  [[nodiscard]] float fill() const;
};

// What the partner UI and the tuning panel show of the partner: a read-only copy made from its
// state and rules, so they bind to plain values and never change the partner.
struct PartnerView {
  std::string species;
  // The name the player gave it (PARTNER_ENTITY.name, GameSession::names), empty when none: the one
  // place screens read the partner's own name from.
  std::string nickname;
  game::partner::Level level = game::partner::Level::None;
  bool alive = false;
  std::int32_t ageDays = 0;
  std::int32_t lives = 0;
  std::int32_t lifetimeHoursLeft = 0;

  // The maximum stats, and the HP and MP left.
  game::partner::StatBlock stats;
  std::int32_t currentHp = 0;
  std::int32_t currentMp = 0;

  // The care meters.
  CareMeter happiness;
  CareMeter discipline;
  CareMeter tiredness;
  CareMeter energy;
  CareMeter weight;
  CareMeter virusBar;
  // Game minutes to the next meal and steps to the next need of the toilet.
  std::int32_t minutesToMeal = 0;
  std::int32_t stepsToToilet = 0;

  game::partner::Conditions conditions;
  // How soon each need does lasting harm if left alone, from the clock's time now
  // (game::partner::needDeadlinesOf; GameSession::partnerView fills it, viewOf leaves it empty).
  game::partner::NeedDeadlines needDeadlines;
  // How the partner feels about the map it is on.
  game::partner::AreaResponse area = game::partner::AreaResponse::Neutral;
  std::int32_t careMistakes = 0;
  std::int32_t battles = 0;
  // Hours since the last change of level (evoTimer).
  std::int32_t evolutionHours = 0;
  // When it wakes up and gets sleepy today (PartnerPara: wakeUpHour, sleepyHour and their
  // minutes); the schedule moves when it sleeps late or early.
  std::int32_t wakeHour = 0;
  std::int32_t wakeMinute = 0;
  std::int32_t sleepyHour = 0;
  std::int32_t sleepyMinute = 0;
};

// The view of a partner under its rules.
PartnerView viewOf(const game::partner::Partner& partner, const game::partner::CareRules& rules);

} // namespace dwe::session
