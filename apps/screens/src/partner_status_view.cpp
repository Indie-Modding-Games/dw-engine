#include <dwe/screens/partner_status_view.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace dwe::screens {
namespace {

using game::partner::Condition;
using game::partner::Stat;

// The scale of the stat bars of the status page (game_menu.c: renderDigimonStatsView): a full bar
// is 999.
constexpr std::int32_t StatBarMax = 999;

// The scale of the HP and MP bars of the same page: a full bar is a maximum of 9999
// (renderDigimonStatsBar(maxHp, 9999, ...)); the bar shows the maximum, not what is left.
constexpr std::int32_t PoolBarMax = 9999;

constexpr std::array<Stat, 2> PoolStats{Stat::Hp, Stat::Mp};
constexpr std::array<Stat, 4> BarStats{Stat::Offense, Stat::Defense, Stat::Speed, Stat::Brain};

// The bars below the stats, in the original's order, with the texts of their labels.
constexpr std::string_view HappinessLabel = "partner_menu.happiness";
constexpr std::string_view DisciplineLabel = "partner_menu.discipline";
constexpr std::string_view VirusLabel = "partner_menu.virus";

constexpr std::array<std::pair<Condition, std::string_view>, game::partner::ConditionCount>
    ConditionLabels{{
        {Condition::Sleepy, "partner_menu.condition.sleepy"},
        {Condition::Tired, "partner_menu.condition.tired"},
        {Condition::Hungry, "partner_menu.condition.hungry"},
        {Condition::NeedsToilet, "partner_menu.condition.needs_toilet"},
        {Condition::Sulking, "partner_menu.condition.sulking"},
        {Condition::Injured, "partner_menu.condition.injured"},
        {Condition::Sick, "partner_menu.condition.sick"},
    }};

constexpr std::string_view LabelField = "label";
constexpr std::string_view ValueField = "value";
constexpr std::string_view MinField = "min";
constexpr std::string_view MaxField = "max";
constexpr std::string_view TwoToneField = "two_tone";
constexpr std::string_view KeyField = "key";
constexpr std::string_view CountSuffix = ".count";

MeterView meterOf(std::string_view label, const session::CareMeter& meter, bool twoTone) {
  return {.labelKey = std::string{label},
          .value = meter.value,
          .min = meter.min,
          .max = meter.max,
          .twoTone = twoTone};
}

void addMeter(ScreenSettings& settings, std::string_view list, std::size_t index,
              const MeterView& meter) {
  settings.push_back({.name = listEntryName(list, index, LabelField), .value = meter.labelKey});
  settings.push_back(
      {.name = listEntryName(list, index, ValueField), .value = static_cast<double>(meter.value)});
  settings.push_back(
      {.name = listEntryName(list, index, MinField), .value = static_cast<double>(meter.min)});
  settings.push_back(
      {.name = listEntryName(list, index, MaxField), .value = static_cast<double>(meter.max)});
  settings.push_back({.name = listEntryName(list, index, TwoToneField), .value = meter.twoTone});
}

std::string countName(std::string_view list) {
  return std::string{list} + std::string{CountSuffix};
}

} // namespace

PartnerStatusView partnerStatusViewOf(const session::PartnerView& partner, const NameKeys& names) {
  PartnerStatusView view{.nameKey = names.species(partner.species),
                         .nickname = partner.nickname,
                         .ageDays = partner.ageDays,
                         .weight = partner.weight.value,
                         .lives = partner.lives,
                         .hp = partner.currentHp,
                         .maxHp = partner.stats[Stat::Hp],
                         .mp = partner.currentMp,
                         .maxMp = partner.stats[Stat::Mp],
                         .pools = {},
                         .stats = {},
                         .meters = {meterOf(HappinessLabel, partner.happiness, true),
                                    meterOf(DisciplineLabel, partner.discipline, true),
                                    meterOf(VirusLabel, partner.virusBar, false)},
                         .conditionKeys = {}};
  for (std::size_t index = 0; index < PoolStats.size(); ++index) {
    const Stat stat = PoolStats.at(index);
    view.pools.at(index) = {.labelKey =
                                mainLabelKey(StatLabelsBlock, static_cast<std::size_t>(stat)),
                            .value = partner.stats[stat],
                            .min = 0,
                            .max = PoolBarMax,
                            .twoTone = false};
  }
  for (std::size_t index = 0; index < BarStats.size(); ++index) {
    const Stat stat = BarStats.at(index);
    view.stats.at(index) = {.labelKey =
                                mainLabelKey(StatLabelsBlock, static_cast<std::size_t>(stat)),
                            .value = partner.stats[stat],
                            .min = 0,
                            .max = StatBarMax,
                            .twoTone = false};
  }
  for (const auto& [condition, label] : ConditionLabels) {
    if (partner.conditions.has(condition)) {
      view.conditionKeys.emplace_back(label);
    }
  }
  return view;
}

ScreenSettings settingsOf(const PartnerStatusView& view) {
  ScreenSettings settings{
      {.name = std::string{PartnerNameSetting}, .value = view.nameKey},
      {.name = std::string{PartnerNicknameSetting}, .value = view.nickname},
      {.name = std::string{PartnerAgeSetting}, .value = static_cast<double>(view.ageDays)},
      {.name = std::string{PartnerWeightSetting}, .value = static_cast<double>(view.weight)},
      {.name = std::string{PartnerLivesSetting}, .value = static_cast<double>(view.lives)},
      {.name = std::string{PartnerHpSetting}, .value = static_cast<double>(view.hp)},
      {.name = std::string{PartnerMaxHpSetting}, .value = static_cast<double>(view.maxHp)},
      {.name = std::string{PartnerMpSetting}, .value = static_cast<double>(view.mp)},
      {.name = std::string{PartnerMaxMpSetting}, .value = static_cast<double>(view.maxMp)},
  };
  for (std::size_t index = 0; index < view.pools.size(); ++index) {
    addMeter(settings, PartnerPoolList, index, view.pools.at(index));
  }
  for (std::size_t index = 0; index < view.stats.size(); ++index) {
    addMeter(settings, PartnerStatList, index, view.stats.at(index));
  }
  settings.push_back(
      {.name = countName(PartnerMeterList), .value = static_cast<double>(view.meters.size())});
  for (std::size_t index = 0; index < view.meters.size(); ++index) {
    addMeter(settings, PartnerMeterList, index, view.meters[index]);
  }
  settings.push_back({.name = countName(PartnerConditionList),
                      .value = static_cast<double>(view.conditionKeys.size())});
  for (std::size_t index = 0; index < view.conditionKeys.size(); ++index) {
    settings.push_back({.name = listEntryName(PartnerConditionList, index, KeyField),
                        .value = view.conditionKeys[index]});
  }
  return settings;
}

} // namespace dwe::screens
