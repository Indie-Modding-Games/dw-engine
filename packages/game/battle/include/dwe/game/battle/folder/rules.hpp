#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dwe::game::battle {

// Folder battle numbers: stats on a `stat_scale` scale (§6.3), times in battle frames, percents of
// the base. §5: a technique's preparation and recovery, before VEL.
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

// §3.2: what a technique is for, as bits: a technique may serve several.
enum class TechniqueFunction : std::uint8_t {
  Offensive = 1,
  Defensive = 2,
  Support = 4,
  Control = 8,
};
using TechniqueFunctions = std::uint8_t;

// §7.5: where a technique's effect area is centred; Single reaches its target alone.
enum class AreaShape : std::uint8_t { Single, AroundSelf, AroundTarget, Global };

// costo-mp.md §5: how long an effect lasts, for its cost.
enum class EffectDuration : std::uint8_t { Short, Medium, Long };
inline constexpr std::size_t EffectDurations = 3;

// costo-mp.md §6 and §7: the reach and area classes a cost is read from.
enum class ReachClass : std::uint8_t { Personal, Short, Medium, Long };
enum class AreaClass : std::uint8_t { Single, Small, Medium, Large };
inline constexpr std::size_t ReachClasses = 4;
inline constexpr std::size_t AreaClasses = 4;
// costo-mp.md §8.1: light, moderate and strong boosts.
inline constexpr std::size_t BuffTiers = 3;
// costo-mp.md §4.1: poison, confusion, stun and flattening (StatusEffect 1 to 4).
inline constexpr std::size_t CostedStatuses = 4;

// costo-mp.md v0.2: MP = ceil(power x power% + effects x chance x duration + reach + area), and a
// support move's boost by its size, how many stats it raises and its duration.
struct MpCostRules {
  std::int32_t powerPercent = 0;
  // The reach class of a ranged technique by its distance in pixels, and an area's by its size.
  std::int32_t shortReachUpTo = 0;
  std::int32_t mediumReachUpTo = 0;
  std::int32_t smallAreaUpTo = 0;
  std::int32_t mediumAreaUpTo = 0;
  std::array<std::int32_t, ReachClasses> reachCosts{};
  std::array<std::int32_t, AreaClasses> areaCosts{};
  std::array<std::int32_t, CostedStatuses> statusCosts{};
  std::array<std::int32_t, EffectDurations> durationPercents{};
  EffectDuration statusDuration = EffectDuration::Short;
  // A boost up to `lightBuffUpTo` percent is light, up to `moderateBuffUpTo` moderate, else
  // strong; one stat or several.
  std::int32_t lightBuffUpTo = 0;
  std::int32_t moderateBuffUpTo = 0;
  std::array<std::int32_t, BuffTiers> singleBuffCosts{};
  std::array<std::int32_t, BuffTiers> severalBuffCosts{};
  EffectDuration buffDuration = EffectDuration::Long;
};

// §3.10: what one technique's entry of `catalog` sets over what Folder reads from the move table.
struct TechniqueEntry {
  std::optional<TechniqueFunctions> functions;
  std::optional<AreaShape> area;
  std::optional<std::int32_t> areaPixels;
  std::optional<bool> friendlyFire;
  std::optional<EffectDuration> effectDuration;
  std::optional<std::int32_t> mpCost;

  friend bool operator==(const TechniqueEntry&, const TechniqueEntry&) = default;
};

struct FolderRules {
  std::int32_t statScale = 1000;
  // §4.2: an order's phase base times (INT cuts processing, VEL preparation). Preparation and
  // recovery come from `techniques`, else the finisher's or its MP tier's (cheapest first), else
  // these.
  std::int32_t processingFrames = 0;
  std::int32_t preparationFrames = 0;
  std::int32_t recoveryFrames = 0;
  std::vector<TimingTier> timingTiers;
  TechniqueTiming finisherTiming;
  std::map<std::string, TechniqueTiming, std::less<>> techniques;
  // §4.6: positioning that long without reaching the range gives the order up.
  std::int32_t giveUpFrames = 0;
  // §2.9: Aggressive chases to point-blank this long, then settles for the technique's reach.
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
  // §7.4: the cover chance gains `cover_near_bonus_percent` points at point-blank and
  // `cover_far_bonus_percent` from `cover_far_pixels` on, in a straight line between.
  std::int32_t coverNearBonusPercent = 0;
  std::int32_t coverFarBonusPercent = 0;
  std::int32_t coverFarPixels = 0;
  // §2.9: capacity is DEF times this; a full bar empties in `bar_drain_frames` after
  // `bar_rest_frames` unhit.
  std::int32_t barDefenseTimes = 0;
  std::int32_t barDrainFrames = 0;
  std::int32_t barRestFrames = 0;
  // §2.9: the break's length and debuff, and how much VEL and DEF each shorten it and speed the
  // drain, up to the cap together.
  std::int32_t breakFrames = 0;
  std::int32_t breakDebuffPercent = 0;
  std::int32_t perStatCutPercent = 0;
  std::int32_t statCutCapPercent = 0;
  // costo-mp.md: the MP the trainer's techniques cost; enemies keep the move table's.
  MpCostRules mpCosts;
  // §3.10: per technique, by move id, what differs from the reading of the move table.
  std::map<std::string, TechniqueEntry, std::less<>> catalog;
};

} // namespace dwe::game::battle
