#include <dwe/screens/partner_status_view.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
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
constexpr std::string_view TirednessLabel = "partner_menu.tiredness";

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
// The evolution check's hours of a level that never evolves on its own.
constexpr std::int32_t NoCheckHours = -1;

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
                         .tiredness = meterOf(TirednessLabel, partner.tiredness, false),
                         .careMistakes = partner.careMistakes,
                         .battles = partner.battles,
                         .lifetimeHours = partner.lifetimeHoursLeft,
                         .evolutionHours = partner.evolutionHours,
                         .evolutionCheckHours = std::nullopt,
                         .wakeHour = partner.wakeHour,
                         .wakeMinute = partner.wakeMinute,
                         .sleepyHour = partner.sleepyHour,
                         .sleepyMinute = partner.sleepyMinute,
                         .favoriteFoodKey = {},
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

std::array<std::optional<TechniqueView>, game::battle::EquippedSlots>
equippedTechniqueViewsOf(const game::battle::Combatant& partner,
                         const game::battle::MoveCatalog& catalog, const NameKeys& names) {
  std::array<std::optional<TechniqueView>, game::battle::EquippedSlots> result{};
  for (std::size_t index = 0; index < partner.moves.size(); ++index) {
    if (!partner.moves[index])
      continue;
    const auto move = catalog.find(*partner.moves[index]);
    if (!move)
      continue;
    const auto& entry = catalog.at(*move);
    result[index] = TechniqueView{.nameKey = names.move(*move),
                                  .power = entry.power,
                                  .mpCost = entry.mpCost,
                                  .range = static_cast<std::int32_t>(entry.range),
                                  .effect = static_cast<std::int32_t>(entry.status)};
  }
  return result;
}

void setProfileSpecies(PartnerStatusView& view, const assets::DigimonRow& species,
                       std::int32_t activePattern) {
  view.specials = species.special;
  view.type = species.type;
  view.activePattern = activePattern;
}

void setFavoriteFood(PartnerStatusView& view, std::optional<std::string_view> item,
                     const NameKeys& names) {
  view.favoriteFoodKey = item ? names.item(*item) : std::string{};
}

void setTechniqueChoices(PartnerStatusView& view, const assets::DigimonRow& species,
                         const session::PartnerMoves& moves,
                         const game::battle::MoveCatalog& catalog, const NameKeys& names) {
  // Orden visual de categorías del original; las variantes comparten celda con su técnica base.
  constexpr std::array<int, 7> Groups{0, 5, 1, 4, 2, 3, 6};
  view.techniqueChoices = {};
  for (std::size_t cell = 0; cell < view.techniqueChoices.size(); ++cell) {
    const int group = Groups[cell / 8];
    const int canonical = group * 8 + static_cast<int>(cell % 8) + (group == 6 ? 1 : 0);
    const int variant = canonical == 44 ? 48 : canonical == 55 ? 57 : canonical;
    auto& choice = view.techniqueChoices[cell];
    std::optional<game::battle::MoveRef> selected;
    for (std::size_t ref = 0; ref < catalog.size(); ++ref) {
      const auto& entry = catalog.at(ref);
      if (entry.index != canonical && entry.index != variant)
        continue;
      if (entry.index == canonical || !selected)
        selected = ref;
      choice.known = choice.known || session::knowsTechnique(moves, catalog, entry.id);
      for (std::size_t slot = 0; slot < species.moves.size(); ++slot) {
        if (species.moves[slot] == entry.id &&
            (choice.speciesSlot < 0 || std::cmp_less(slot, choice.speciesSlot)) &&
            !entry.finisher) {
          choice.speciesSlot = static_cast<std::int32_t>(slot);
        }
      }
    }
    if (!selected)
      continue;
    const auto& entry = catalog.at(*selected);
    choice.technique = TechniqueView{names.move(*selected), entry.power, entry.mpCost,
                                     static_cast<std::int32_t>(entry.range),
                                     static_cast<std::int32_t>(entry.status)};
    choice.special = entry.special;
    choice.available = choice.known && choice.speciesSlot >= 0;
    for (std::size_t place = 0; place < game::battle::FinisherSlot; ++place) {
      const auto slot = moves.equipped[place];
      if (!slot || *slot < 0 || std::cmp_greater_equal(*slot, species.moves.size()) ||
          !species.moves[static_cast<std::size_t>(*slot)])
        continue;
      const auto ref = catalog.find(*species.moves[static_cast<std::size_t>(*slot)]);
      if (ref && (catalog.at(*ref).index == canonical || catalog.at(*ref).index == variant))
        choice.equipped = true;
    }
  }
  view.canFinishTechniqueEdit = session::canFinishTechniqueEdit(moves, species, catalog);
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
  settings.push_back({.name = "partner.profile.type", .value = static_cast<double>(view.type)});
  settings.push_back(
      {.name = "partner.profile.active", .value = static_cast<double>(view.activePattern)});
  for (std::size_t index = 0; index < view.specials.size(); ++index) {
    settings.push_back({.name = "partner.profile.special." + std::to_string(index),
                        .value = static_cast<double>(view.specials[index].value_or(-1))});
  }
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
  const std::string tiredness{PartnerTirednessSetting};
  settings.push_back({.name = tiredness + ".label", .value = view.tiredness.labelKey});
  settings.push_back(
      {.name = tiredness + ".value", .value = static_cast<double>(view.tiredness.value)});
  settings.push_back(
      {.name = tiredness + ".min", .value = static_cast<double>(view.tiredness.min)});
  settings.push_back(
      {.name = tiredness + ".max", .value = static_cast<double>(view.tiredness.max)});
  settings.push_back({.name = std::string{PartnerCareMistakesSetting},
                      .value = static_cast<double>(view.careMistakes)});
  settings.push_back(
      {.name = std::string{PartnerFavoriteFoodSetting}, .value = view.favoriteFoodKey});
  const std::array<std::pair<std::string_view, std::int32_t>, 8> extras{{
      {PartnerBattlesSetting, view.battles},
      {PartnerLifetimeSetting, view.lifetimeHours},
      {PartnerEvolutionHoursSetting, view.evolutionHours},
      {PartnerEvolutionCheckSetting, view.evolutionCheckHours.value_or(NoCheckHours)},
      {PartnerWakeHourSetting, view.wakeHour},
      {PartnerWakeMinuteSetting, view.wakeMinute},
      {PartnerSleepyHourSetting, view.sleepyHour},
      {PartnerSleepyMinuteSetting, view.sleepyMinute},
  }};
  for (const auto& [name, value] : extras) {
    settings.push_back({.name = std::string{name}, .value = static_cast<double>(value)});
  }
  settings.push_back({.name = countName(PartnerConditionList),
                      .value = static_cast<double>(view.conditionKeys.size())});
  for (std::size_t index = 0; index < view.conditionKeys.size(); ++index) {
    settings.push_back({.name = listEntryName(PartnerConditionList, index, KeyField),
                        .value = view.conditionKeys[index]});
  }
  for (std::size_t index = 0; index < view.techniques.size(); ++index) {
    const auto& move = view.techniques[index];
    const auto base = std::string{"partner.technique."} + std::to_string(index) + ".";
    settings.push_back({.name = base + "empty", .value = !move.has_value()});
    settings.push_back({.name = base + "name", .value = move ? move->nameKey : std::string{}});
    settings.push_back(
        {.name = base + "power", .value = static_cast<double>(move ? move->power : 0)});
    settings.push_back(
        {.name = base + "mp", .value = static_cast<double>(move ? move->mpCost : 0)});
    settings.push_back(
        {.name = base + "range", .value = static_cast<double>(move ? move->range : 0)});
    settings.push_back(
        {.name = base + "effect", .value = static_cast<double>(move ? move->effect : 0)});
  }
  settings.push_back(
      {.name = "partner.technique.can_finish", .value = view.canFinishTechniqueEdit});
  for (std::size_t index = 0; index < view.techniqueChoices.size(); ++index) {
    const auto& choice = view.techniqueChoices[index];
    const auto base = std::string{"partner.technique.choice."} + std::to_string(index) + ".";
    settings.push_back({.name = base + "name",
                        .value = choice.technique ? choice.technique->nameKey : std::string{}});
    settings.push_back({.name = base + "available", .value = choice.available});
    settings.push_back({.name = base + "equipped", .value = choice.equipped});
    settings.push_back({.name = base + "known", .value = choice.known});
    settings.push_back(
        {.name = base + "power",
         .value = static_cast<double>(choice.technique ? choice.technique->power : 0)});
    settings.push_back(
        {.name = base + "mp",
         .value = static_cast<double>(choice.technique ? choice.technique->mpCost : 0)});
    settings.push_back(
        {.name = base + "range",
         .value = static_cast<double>(choice.technique ? choice.technique->range : 0)});
    settings.push_back(
        {.name = base + "effect",
         .value = static_cast<double>(choice.technique ? choice.technique->effect : 0)});
    settings.push_back({.name = base + "slot", .value = static_cast<double>(choice.speciesSlot)});
    settings.push_back({.name = base + "special", .value = static_cast<double>(choice.special)});
  }
  return settings;
}

} // namespace dwe::screens
