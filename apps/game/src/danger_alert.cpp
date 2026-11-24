#include "danger_alert.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace dwe::app {
namespace {

// 2.5 as a fraction, so the rule stays in whole numbers.
constexpr std::int64_t CrushNumerator = 5;
constexpr std::int64_t CrushDenominator = 2;

constexpr double Percent = 100.0;
constexpr double PlaceSteps = 10.0;

std::int64_t powerOf(const FightStrength& fighter) {
  return static_cast<std::int64_t>(fighter.hp) *
         (static_cast<std::int64_t>(fighter.offense) + fighter.defense);
}

double percentOf(double fraction) {
  return std::round(fraction * Percent * PlaceSteps) / PlaceSteps;
}

bool onScreen(const screens::WindowFraction& point) {
  return point.x >= 0.0 && point.x <= 1.0 && point.y >= 0.0 && point.y <= 1.0;
}

} // namespace

bool crushes(const FightStrength& wild, const FightStrength& partner) {
  return powerOf(wild) * CrushDenominator >= powerOf(partner) * CrushNumerator;
}

FightStrength strengthOf(const assets::MapNpc& npc) {
  return {.hp = npc.currentHp > 0 ? std::min(npc.currentHp, npc.hp) : npc.hp,
          .offense = npc.off,
          .defense = npc.def};
}

FightStrength strengthOf(const session::PartnerView& partner) {
  return {.hp = partner.currentHp,
          .offense = partner.stats[game::partner::Stat::Offense],
          .defense = partner.stats[game::partner::Stat::Defense]};
}

DangerMarks dangerMarksOf(bool shown, std::span<const assets::MapNpc> records,
                          const std::set<std::int32_t>& wild, const FightStrength& partner,
                          const DangerAnchor& anchor) {
  DangerMarks marks{};
  if (!shown) {
    return marks;
  }
  const std::size_t count = std::min(records.size(), marks.size());
  for (std::size_t slot = 0; slot < count; ++slot) {
    const assets::MapNpc& record = records[slot];
    if (!wild.contains(record.scriptId) || record.hp <= 0 ||
        !crushes(strengthOf(record), partner)) {
      continue;
    }
    if (const std::optional<screens::WindowFraction> at = anchor(slot, record);
        at && onScreen(*at)) {
      marks.at(slot) = {.visible = true, .x = percentOf(at->x), .y = percentOf(at->y)};
    }
  }
  return marks;
}

screens::ScreenSettings settingsOf(const DangerMarks& marks) {
  screens::ScreenSettings settings;
  for (std::size_t index = 0; index < marks.size(); ++index) {
    const DangerMark& mark = marks.at(index);
    settings.push_back(
        {.name = screens::listEntryName(DangerList, index, "visible"), .value = mark.visible});
    settings.push_back({.name = screens::listEntryName(DangerList, index, "x"), .value = mark.x});
    settings.push_back({.name = screens::listEntryName(DangerList, index, "y"), .value = mark.y});
  }
  return settings;
}

} // namespace dwe::app
