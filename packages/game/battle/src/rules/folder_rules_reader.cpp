#include <dwe/game/battle/messages.hpp>
#include <dwe/game/battle/rules.hpp>

#include <cstdint>

#include "rules/value_reader.hpp"

namespace dwe::game::battle {

namespace {

TechniqueTiming readTechniqueTiming(ValueReader& in, const YamlNode& node) {
  return {.preparationFrames = in.count(node, "preparation_frames"),
          .recoveryFrames = in.count(node, "recovery_frames")};
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
                 "bar_defense_times",
                 "bar_drain_frames",
                 "bar_rest_frames",
                 "break_frames",
                 "break_debuff_percent",
                 "per_stat_cut_percent",
                 "stat_cut_cap_percent",
                 "timings",
                 "finisher_timing",
                 "techniques"})) {
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
                     .barDefenseTimes = in.positive(node, "bar_defense_times"),
                     .barDrainFrames = in.positive(node, "bar_drain_frames"),
                     .barRestFrames = in.count(node, "bar_rest_frames"),
                     .breakFrames = in.positive(node, "break_frames"),
                     .breakDebuffPercent = in.percent(node, "break_debuff_percent"),
                     .perStatCutPercent = in.percent(node, "per_stat_cut_percent"),
                     .statCutCapPercent = in.percent(node, "stat_cut_cap_percent")};
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
  return folder;
}

} // namespace dwe::game::battle
