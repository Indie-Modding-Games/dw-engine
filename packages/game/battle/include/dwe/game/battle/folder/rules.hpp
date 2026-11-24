#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dwe::game::battle {

// The Folder battle's numbers (docs/architecture/folder-battle.md, combat-design.md). Stats are
// read on a scale of `stat_scale` (§6.3); times are battle frames; percents are of the base value.
// §5: how long a technique prepares before it goes off and recovers after it, before VEL.
struct TechniqueTiming {
  std::int32_t preparationFrames = 0;
  std::int32_t recoveryFrames = 0;

  friend bool operator==(const TechniqueTiming&, const TechniqueTiming&) = default;
};

// The timing of the techniques costing up to `mpUpTo` MP that no dearer tier covers.
struct TimingTier {
  std::int32_t mpUpTo = 0;
  TechniqueTiming timing;
};

struct FolderRules {
  std::int32_t statScale = 1000;
  // §4.2: the base times of an order's phases; INT shortens processing, VEL preparation (§6.4,
  // §6.5). Preparation and recovery are a technique's own: its entry in `techniques`, else the
  // finisher's or its MP tier's (cheapest first), else these.
  std::int32_t processingFrames = 0;
  std::int32_t preparationFrames = 0;
  std::int32_t recoveryFrames = 0;
  std::vector<TimingTier> timingTiers;
  TechniqueTiming finisherTiming;
  std::map<std::string, TechniqueTiming, std::less<>> techniques;
  // §4.6: positioning that long without reaching the range gives the order up.
  std::int32_t giveUpFrames = 0;
  // §2.9: Aggressive chases its target to point-blank this long, then settles for the technique's
  // reach.
  std::int32_t chaseFrames = 0;
  // §6.4: movement from min to min + span percent of the base speed; execution down by up to
  // `execution_cut_percent`. §6.5: processing down by up to `processing_cut_percent`.
  std::int32_t movementMinPercent = 0;
  std::int32_t movementSpanPercent = 0;
  std::int32_t executionCutPercent = 0;
  std::int32_t processingCutPercent = 0;
  // §6.2: the attribute modifier, ATK times this percent (pending balance).
  std::int32_t attackPercent = 0;
  // §3.8: each elemental advantage or resistance, STAB, and the type layer.
  std::int32_t elementPercent = 0;
  std::int32_t stabPercent = 0;
  std::int32_t typePercent = 0;
  // §3.8: the specialty each specialty beats, by specialty number.
  std::vector<std::int32_t> beats;
  // §2.9: passive cover up to this chance at full DEF; Guard and cover subtract DEF this many
  // times.
  std::int32_t coverMaxPercent = 0;
  std::int32_t coverDefenseTimes = 0;
  // §2.9: the bar's capacity is DEF times this; it empties from full in `bar_drain_frames` after
  // `bar_rest_frames` without damage.
  std::int32_t barDefenseTimes = 0;
  std::int32_t barDrainFrames = 0;
  std::int32_t barRestFrames = 0;
  // §2.9: the break's length and debuff, and how much VEL and DEF each shorten it and speed the
  // drain, up to the cap together.
  std::int32_t breakFrames = 0;
  std::int32_t breakDebuffPercent = 0;
  std::int32_t perStatCutPercent = 0;
  std::int32_t statCutCapPercent = 0;
};

} // namespace dwe::game::battle
