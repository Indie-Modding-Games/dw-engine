#pragma once

#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/report.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::session {
class EvolutionAdapter;
class GameSession;
} // namespace dwe::session

namespace dwe::ui {
class Ui;
} // namespace dwe::ui

namespace dwe::app {

// The option that shows the evolution requirements on the chart (evolution_requirements: true,
// false), an engine improvement on by default; off leaves the original chart.
inline constexpr std::string_view EvolutionRequirementsSetting = "evolution_requirements";

// One line of a requirement on the chart: what it is (care, weight, stats, hp, mp, off, def, spd,
// brn, bonus, from, disc, happy, battles, techs, special), the range asked in numbers, and the
// partner's value and whether it is met.
struct RequirementRow {
  std::string key;
  std::string range;
  std::string value;
  bool met = false;
};

// The range of a minimum ("100+") or a maximum ("0-10").
[[nodiscard]] std::string requirementRange(std::int32_t value, bool isMaximum);
// The weight range that scores ("25-35"), never below 0.
[[nodiscard]] std::string weightRange(std::int32_t low, std::int32_t high);

// The lines of a requirement, in the order the chart shows them: care mistakes, weight, the stats
// group and each stat it asks for, the bonus group and each bonus it accepts. A target with no
// care or weight asked (a special evolution) is one line, "special".
[[nodiscard]] std::vector<RequirementRow>
requirementRows(const game::evolution::RequirementReport& report);

// The rows as the chart reads them: "key:range" (static) or "value:met" (the partner), joined by
// '|'.
[[nodiscard]] std::string encodeRanges(const std::vector<RequirementRow>& rows);
[[nodiscard]] std::string encodeValues(const game::evolution::RequirementReport& report,
                                       const std::vector<RequirementRow>& rows);

// The forms the player has discovered: the ones the chart draws with their picture (the raised
// forms, "player.chart.raised", a list of species indices). The chart names any other form "???"
// and shows nothing of its requirement.
using DiscoveredForms = std::set<game::evolution::Species>;
[[nodiscard]] DiscoveredForms parseDiscovered(std::string_view list);

// The partner's line of the chart: "species,timer,check hours,due,next,fallback,fallback hours",
// -1 for none and UndiscoveredForm for a next form or fallback the player has not discovered.
inline constexpr int UndiscoveredForm = -2;
[[nodiscard]] std::string encodeOutlook(const game::evolution::EvolutionOutlook& outlook,
                                        const DiscoveredForms& discovered);

// The targets of the outlook whose requirement the chart shows: the discovered ones.
[[nodiscard]] std::vector<const game::evolution::RequirementReport*>
shownTargets(const game::evolution::EvolutionOutlook& outlook, const DiscoveredForms& discovered);

// Publishes the evolution requirements for the pause menu's chart (docs/modulos/apps/game.md,
// «Requisitos de evolución»): every target's requirement once (evoreq.req.<species>) and, while the
// menu is open, how the partner stands toward the targets of its chart (evoreq.partner,
// evoreq.partner.<species>), leaving out the forms not discovered yet. Every verdict comes from the
// evolution rules' own scoring.
class ChartRequirements {
public:
  ChartRequirements(ui::Ui& ui, const session::EvolutionAdapter* evolution);

  // Once per frame.
  void update(const session::GameSession& session);

private:
  void publishRequirements(const game::evolution::Evolution& evolution);
  void set(const std::string& name, std::string value);

  ui::Ui* ui_;
  const session::EvolutionAdapter* evolution_;
  bool published_ = false;
  std::string outlook_;
  std::vector<std::string> partnerKeys_;
  std::int32_t version_ = 0;
};

} // namespace dwe::app
