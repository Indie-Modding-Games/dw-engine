#include <dwe/game/battle/messages.hpp>
#include <dwe/game/battle/rules.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "rules/value_reader.hpp"

namespace dwe::game::battle {

namespace {

TechniqueTiming readTechniqueTiming(ValueReader& in, const YamlNode& node) {
  return {.preparationFrames = in.count(node, "preparation_frames"),
          .recoveryFrames = in.count(node, "recovery_frames")};
}

constexpr std::array<std::string_view, 4> FunctionNames = {"offensive", "defensive", "support",
                                                           "control"};
constexpr std::array<std::string_view, 4> AreaNames = {"single", "around_self", "around_target",
                                                       "global"};
constexpr std::array<std::string_view, EffectDurations> DurationNames = {"short", "medium", "long"};

// The position of a name among `names`, failing the reading when it is none of them.
template <std::size_t N>
std::optional<std::size_t> choice(ValueReader& in, const YamlNode& node,
                                  const std::array<std::string_view, N>& names,
                                  std::string_view list) {
  const std::string name = in.yaml().text(node);
  for (std::size_t i = 0; i < N; ++i) {
    if (names.at(i) == name) {
      return i;
    }
  }
  if (in.yaml().ok()) {
    in.yaml().fail(node, messages::formatMessage(messages::NotOneOf, name, list));
  }
  return std::nullopt;
}

EffectDuration durationOf(ValueReader& in, const YamlNode& node) {
  return static_cast<EffectDuration>(
      choice(in, node, DurationNames, "short, medium, long").value_or(0));
}

template <std::size_t N>
std::array<std::int32_t, N> costs(ValueReader& in, const YamlNode& node, std::string_view key) {
  std::array<std::int32_t, N> values{};
  const std::span<const YamlNode> items = in.yaml().sequence(in.field(node, key), key, N);
  for (std::size_t i = 0; i < items.size(); ++i) {
    values.at(i) = in.yaml().integer<std::int32_t>(items[i]);
    if (in.yaml().ok() && values.at(i) < 0) {
      in.yaml().fail(items[i], messages::formatMessage(messages::NotNegative, key));
    }
  }
  return values;
}

MpCostRules readMpCosts(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "mp_costs",
                {"power_percent", "short_reach_up_to", "medium_reach_up_to", "small_area_up_to",
                 "medium_area_up_to", "reach_costs", "area_costs", "status_costs",
                 "duration_percents", "status_duration", "light_buff_up_to", "moderate_buff_up_to",
                 "single_buff_costs", "several_buff_costs", "buff_duration"})) {
    return {};
  }
  MpCostRules costsRead{.powerPercent = in.count(node, "power_percent"),
                        .shortReachUpTo = in.count(node, "short_reach_up_to"),
                        .mediumReachUpTo = in.count(node, "medium_reach_up_to"),
                        .smallAreaUpTo = in.count(node, "small_area_up_to"),
                        .mediumAreaUpTo = in.count(node, "medium_area_up_to"),
                        .reachCosts = costs<ReachClasses>(in, node, "reach_costs"),
                        .areaCosts = costs<AreaClasses>(in, node, "area_costs"),
                        .statusCosts = costs<CostedStatuses>(in, node, "status_costs"),
                        .durationPercents = costs<EffectDurations>(in, node, "duration_percents"),
                        .statusDuration = durationOf(in, in.field(node, "status_duration")),
                        .lightBuffUpTo = in.count(node, "light_buff_up_to"),
                        .moderateBuffUpTo = in.count(node, "moderate_buff_up_to"),
                        .singleBuffCosts = costs<BuffTiers>(in, node, "single_buff_costs"),
                        .severalBuffCosts = costs<BuffTiers>(in, node, "several_buff_costs"),
                        .buffDuration = durationOf(in, in.field(node, "buff_duration"))};
  in.ordered(node, "short_reach_up_to", costsRead.shortReachUpTo, "medium_reach_up_to",
             costsRead.mediumReachUpTo);
  in.ordered(node, "small_area_up_to", costsRead.smallAreaUpTo, "medium_area_up_to",
             costsRead.mediumAreaUpTo);
  in.ordered(node, "light_buff_up_to", costsRead.lightBuffUpTo, "moderate_buff_up_to",
             costsRead.moderateBuffUpTo);
  return costsRead;
}

TechniqueEntry readTechniqueEntry(ValueReader& in, const core::YamlEntry& entry) {
  TechniqueEntry own;
  const YamlNode& node = entry.value;
  if (!in.group(
          node, entry.key, {},
          {"functions", "area", "area_pixels", "friendly_fire", "effect_duration", "mp_cost"})) {
    return own;
  }
  if (const YamlNode* functions = node.find("functions")) {
    TechniqueFunctions bits = 0;
    for (const YamlNode& item : in.yaml().sequence(*functions, "functions")) {
      bits |= static_cast<TechniqueFunctions>(
          1U
          << choice(in, item, FunctionNames, "offensive, defensive, support, control").value_or(0));
    }
    own.functions = bits;
  }
  if (const YamlNode* area = node.find("area")) {
    own.area = static_cast<AreaShape>(
        choice(in, *area, AreaNames, "single, around_self, around_target, global").value_or(0));
  }
  if (node.find("area_pixels") != nullptr) {
    own.areaPixels = in.count(node, "area_pixels");
  }
  if (const YamlNode* friendly = node.find("friendly_fire")) {
    own.friendlyFire = in.yaml().boolean(*friendly);
  }
  if (const YamlNode* duration = node.find("effect_duration")) {
    own.effectDuration = durationOf(in, *duration);
  }
  if (node.find("mp_cost") != nullptr) {
    own.mpCost = in.count(node, "mp_cost");
  }
  return own;
}

} // namespace

FolderRules readFolder(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "folder",
                {"stat_scale",
                 "processing_frames",
                 "preparation_frames",
                 "recovery_frames",
                 "give_up_frames",
                 "chase_frames",
                 "movement_min_percent",
                 "movement_span_percent",
                 "execution_cut_percent",
                 "processing_cut_percent",
                 "attack_percent",
                 "element_percent",
                 "stab_percent",
                 "type_percent",
                 "beats",
                 "cover_max_percent",
                 "cover_defense_times",
                 "cover_near_bonus_percent",
                 "cover_far_bonus_percent",
                 "cover_far_pixels",
                 "bar_defense_times",
                 "bar_drain_frames",
                 "bar_rest_frames",
                 "break_frames",
                 "break_debuff_percent",
                 "per_stat_cut_percent",
                 "stat_cut_cap_percent",
                 "timings",
                 "finisher_timing",
                 "techniques",
                 "mp_costs",
                 "catalog"})) {
    return {};
  }
  FolderRules folder{.statScale = in.positive(node, "stat_scale"),
                     .processingFrames = in.count(node, "processing_frames"),
                     .preparationFrames = in.count(node, "preparation_frames"),
                     .recoveryFrames = in.count(node, "recovery_frames"),
                     .timingTiers = {},
                     .finisherTiming = {},
                     .techniques = {},
                     .giveUpFrames = in.positive(node, "give_up_frames"),
                     .chaseFrames = in.count(node, "chase_frames"),
                     .movementMinPercent = in.positive(node, "movement_min_percent"),
                     .movementSpanPercent = in.count(node, "movement_span_percent"),
                     .executionCutPercent = in.percent(node, "execution_cut_percent"),
                     .processingCutPercent = in.percent(node, "processing_cut_percent"),
                     .attackPercent = in.count(node, "attack_percent"),
                     .elementPercent = in.percent(node, "element_percent"),
                     .stabPercent = in.percent(node, "stab_percent"),
                     .typePercent = in.percent(node, "type_percent"),
                     .beats = {},
                     .coverMaxPercent = in.percent(node, "cover_max_percent"),
                     .coverDefenseTimes = in.positive(node, "cover_defense_times"),
                     .coverNearBonusPercent = in.percent(node, "cover_near_bonus_percent"),
                     .coverFarBonusPercent = in.percent(node, "cover_far_bonus_percent"),
                     .coverFarPixels = in.count(node, "cover_far_pixels"),
                     .barDefenseTimes = in.positive(node, "bar_defense_times"),
                     .barDrainFrames = in.positive(node, "bar_drain_frames"),
                     .barRestFrames = in.count(node, "bar_rest_frames"),
                     .breakFrames = in.positive(node, "break_frames"),
                     .breakDebuffPercent = in.percent(node, "break_debuff_percent"),
                     .perStatCutPercent = in.percent(node, "per_stat_cut_percent"),
                     .statCutCapPercent = in.percent(node, "stat_cut_cap_percent"),
                     .mpCosts = readMpCosts(in, in.field(node, "mp_costs")),
                     .catalog = {}};
  for (const YamlNode& item : in.yaml().sequence(in.field(node, "beats"), "beats")) {
    const auto beaten = in.yaml().integer<std::int32_t>(item);
    if (in.yaml().ok() && beaten < 0) {
      in.yaml().fail(item, messages::formatMessage(messages::NotPositive, "beats"));
    }
    folder.beats.push_back(beaten);
  }
  for (const YamlNode& item : in.yaml().sequence(in.field(node, "timings"), "timings")) {
    if (!in.group(item, "a timings tier", {"mp_up_to", "preparation_frames", "recovery_frames"})) {
      break;
    }
    const std::int32_t mpUpTo = in.count(item, "mp_up_to");
    if (!folder.timingTiers.empty()) {
      in.ordered(item, "mp_up_to", folder.timingTiers.back().mpUpTo, "the next mp_up_to", mpUpTo);
    }
    folder.timingTiers.push_back({.mpUpTo = mpUpTo, .timing = readTechniqueTiming(in, item)});
  }
  const YamlNode& finisher = in.field(node, "finisher_timing");
  if (in.group(finisher, "finisher_timing", {"preparation_frames", "recovery_frames"})) {
    folder.finisherTiming = readTechniqueTiming(in, finisher);
  }
  for (const core::YamlEntry& entry : in.field(node, "techniques").entries()) {
    if (!in.group(entry.value, entry.key, {"preparation_frames", "recovery_frames"})) {
      break;
    }
    folder.techniques.emplace(entry.key, readTechniqueTiming(in, entry.value));
  }
  for (const core::YamlEntry& entry : in.field(node, "catalog").entries()) {
    folder.catalog.emplace(entry.key, readTechniqueEntry(in, entry));
  }
  return folder;
}

} // namespace dwe::game::battle
