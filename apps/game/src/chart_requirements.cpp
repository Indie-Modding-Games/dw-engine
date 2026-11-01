#include "chart_requirements.hpp"

#include <dwe/game/evolution/tables.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/ui.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <utility>

namespace dwe::app {

namespace {

using game::evolution::BonusKind;
using game::evolution::BonusLine;
using game::evolution::EvolutionOutlook;
using game::evolution::NoRequirement;
using game::evolution::RequirementReport;

constexpr std::string_view PauseMenu = "pause_menu";
constexpr std::string_view RequirementPrefix = "evoreq.req.";
constexpr std::string_view PartnerPrefix = "evoreq.partner.";
constexpr std::string_view OutlookSetting = "evoreq.partner";
constexpr std::string_view VersionSetting = "evoreq.version";
constexpr std::string_view ReadySetting = "evoreq.ready";
constexpr std::string_view DiscoveredSetting = "player.chart.raised";

// The stats of a requirement by their keys in the chart's texts, in the original's order.
constexpr std::array<std::string_view, game::evolution::RequiredStatCount> StatKeys{
    "hp", "mp", "off", "def", "spd", "brn"};

std::string_view bonusKey(BonusKind kind) {
  switch (kind) {
  case BonusKind::From:
    return "from";
  case BonusKind::Discipline:
    return "disc";
  case BonusKind::Happiness:
    return "happy";
  case BonusKind::Battles:
    return "battles";
  case BonusKind::LearnedMoves:
    return "techs";
  }
  return "from";
}

std::string number(std::int32_t value) {
  return std::to_string(value);
}

int shownIndex(const std::optional<game::evolution::Species>& species,
               const DiscoveredForms& discovered) {
  if (!species) {
    return -1;
  }
  return discovered.contains(*species) ? *species : UndiscoveredForm;
}

} // namespace

std::string requirementRange(std::int32_t value, bool isMaximum) {
  if (isMaximum) {
    return std::format("0-{}", std::max(value, 0));
  }
  return std::format("{}+", std::max(value, 0));
}

std::string weightRange(std::int32_t low, std::int32_t high) {
  return std::format("{}-{}", std::max(low, 0), std::max(high, 0));
}

std::vector<RequirementRow> requirementRows(const RequirementReport& report) {
  std::vector<RequirementRow> rows;
  // A ruleset's gate (the hidden content's special condition): until it holds the target scores
  // nothing.
  if (report.gateMet) {
    rows.push_back({.key = "gate", .range = {}, .value = {}, .met = *report.gateMet});
  }
  if (report.careMistakes == NoRequirement && report.weight == NoRequirement) {
    rows.push_back({.key = "special", .range = {}, .value = {}, .met = report.passes()});
    return rows;
  }
  rows.push_back({.key = "care",
                  .range = requirementRange(report.careMistakes, report.careIsMaximum),
                  .value = number(report.careValue),
                  .met = report.careMet});
  rows.push_back({.key = "weight",
                  .range = weightRange(report.weightMin, report.weightMax),
                  .value = number(report.weightValue),
                  .met = report.weightMet});
  // A favoured stats target names the partner's highest stat in its group line.
  rows.push_back(
      {.key = "stats",
       .range = report.favouredStats ? "top" : "min",
       .value = report.favouredStats ? std::string{StatKeys.at(report.highestStat)} : std::string{},
       .met = report.statsMet});
  for (std::size_t i = 0; i < report.stats.size(); ++i) {
    const game::evolution::StatLine& stat = report.stats.at(i);
    const bool asked = report.favouredStats ? stat.required == game::evolution::FavouredStat
                                            : stat.required != NoRequirement;
    if (!asked) {
      continue;
    }
    rows.push_back({.key = std::string{StatKeys.at(i)},
                    .range = report.favouredStats
                                 ? std::string{}
                                 : requirementRange(stat.required * stat.divisor, false),
                    .value = number(stat.stat),
                    .met = stat.met});
  }
  if (report.bonus.empty()) {
    return rows;
  }
  rows.push_back({.key = "bonus", .range = {}, .value = {}, .met = report.bonusMet});
  for (const BonusLine& bonus : report.bonus) {
    rows.push_back({.key = std::string{bonusKey(bonus.kind)},
                    .range = bonus.kind == BonusKind::From
                                 ? number(bonus.required)
                                 : requirementRange(bonus.required, bonus.isMaximum),
                    .value = number(bonus.value),
                    .met = bonus.met});
  }
  return rows;
}

std::string encodeRanges(const std::vector<RequirementRow>& rows) {
  std::string text;
  for (const RequirementRow& row : rows) {
    if (!text.empty()) {
      text += '|';
    }
    text += std::format("{}:{}", row.key, row.range);
  }
  return text;
}

std::string encodeValues(const RequirementReport& report, const std::vector<RequirementRow>& rows) {
  // First the score: points, the points to pass and the groups.
  std::string text =
      std::format("{}:{}:{}", report.points, report.passPoints, RequirementReport::Groups);
  for (const RequirementRow& row : rows) {
    text += std::format("|{}:{}", row.value, row.met ? 1 : 0);
  }
  return text;
}

DiscoveredForms parseDiscovered(std::string_view list) {
  DiscoveredForms forms;
  while (!list.empty()) {
    const std::size_t comma = list.find(',');
    const std::string_view item = list.substr(0, comma);
    int species = 0;
    if (const auto [end, error] = std::from_chars(item.data(), item.data() + item.size(), species);
        error == std::errc{} && species >= 0) {
      forms.insert(static_cast<game::evolution::Species>(species));
    }
    list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
  }
  return forms;
}

std::string encodeOutlook(const EvolutionOutlook& outlook, const DiscoveredForms& discovered) {
  return std::format("{},{},{},{},{},{},{}", outlook.species, outlook.evolutionTimer,
                     outlook.checkHours.value_or(-1), outlook.due() ? 1 : 0,
                     shownIndex(outlook.next, discovered), shownIndex(outlook.fallback, discovered),
                     outlook.fallback ? outlook.fallbackHours : -1);
}

std::vector<const RequirementReport*> shownTargets(const EvolutionOutlook& outlook,
                                                   const DiscoveredForms& discovered) {
  std::vector<const RequirementReport*> shown;
  for (const RequirementReport& report : outlook.targets) {
    if (discovered.contains(report.target)) {
      shown.push_back(&report);
    }
  }
  return shown;
}

ChartRequirements::ChartRequirements(ui::Ui& ui, const session::EvolutionAdapter* evolution)
    : ui_(&ui), evolution_(evolution) {}

void ChartRequirements::set(const std::string& name, std::string value) {
  const ui::UiValue current = ui_->setting(name);
  if (const auto* now = std::get_if<std::string>(&current); now != nullptr && *now == value) {
    return;
  }
  ui_->setSetting(name, std::move(value));
}

void ChartRequirements::publishRequirements(const game::evolution::Evolution& evolution) {
  // A requirement's own values do not depend on the partner: any inputs give them.
  const game::evolution::EvolutionInputs none{};
  const game::evolution::EvolutionState state{};
  for (const game::evolution::Species species : evolution.tables().allSpecies()) {
    if (const auto report = evolution.report(none, state, species)) {
      set(std::format("{}{}", RequirementPrefix, species), encodeRanges(requirementRows(*report)));
    }
  }
  // The hours each level checks from, by level (1 baby to 4 champion).
  const game::evolution::CheckHours& hours = evolution.rules().checkHours;
  set("evoreq.hours",
      std::format("{},{},{},{}", hours.baby, hours.inTraining, hours.rookie, hours.champion));
  ui_->setSetting(ReadySetting, true);
}

void ChartRequirements::update(const session::GameSession& session) {
  if (evolution_ == nullptr || evolution_->evolution() == nullptr || !ui_->isVisible(PauseMenu)) {
    return;
  }
  if (!published_) {
    publishRequirements(*evolution_->evolution());
    published_ = true;
  }
  // The same forms the chart draws with their picture.
  const ui::UiValue raised = ui_->setting(DiscoveredSetting);
  const auto* raisedList = std::get_if<std::string>(&raised);
  const DiscoveredForms discovered =
      parseDiscovered(raisedList != nullptr ? std::string_view{*raisedList} : std::string_view{});
  const auto outlook = evolution_->outlook(session);
  std::string line = outlook ? encodeOutlook(*outlook, discovered) : std::string{"-1"};
  bool changed = line != outlook_;
  std::vector<std::string> keys;
  if (outlook) {
    for (const RequirementReport* report : shownTargets(*outlook, discovered)) {
      const std::string key = std::format("{}{}", PartnerPrefix, report->target);
      const std::string values = encodeValues(*report, requirementRows(*report));
      const ui::UiValue current = ui_->setting(key);
      const auto* now = std::get_if<std::string>(&current);
      changed = changed || now == nullptr || *now != values;
      set(key, values);
      keys.push_back(key);
    }
  }
  if (!changed) {
    return;
  }
  for (const std::string& old : partnerKeys_) {
    if (std::ranges::find(keys, old) == keys.end()) {
      ui_->setSetting(old, std::string{});
    }
  }
  partnerKeys_ = std::move(keys);
  outlook_ = line;
  set(std::string{OutlookSetting}, std::move(line));
  ui_->setSetting(VersionSetting, static_cast<double>(++version_));
}

} // namespace dwe::app
