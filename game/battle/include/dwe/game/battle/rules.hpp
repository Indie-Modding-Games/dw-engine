#pragma once

#include <dwe/assets/entry_class.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::battle {

// The `battle` section of a ruleset (content/packs/game/rules/original/battle.yaml,
// content/schemas/ruleset_battle.schema.json). Times are battle frames, 20 a second; distances are
// world units and the ones the original compares squared keep the _squared name; percentages go
// from 0 to 100. Nothing has a default here: every number comes from the ruleset
// (docs/reference/battle.md).

inline constexpr std::string_view SectionName = "battle";

// How many specials (elements) there are, and so the size of the affinity table.
inline constexpr std::size_t SpecialCount = 7;

// The original's oddities, each a flag of `quirks:` (docs/reference/battle.md, "Rarezas").
namespace quirk {
inline constexpr std::string_view BrokenMpTiredness = "broken_mp_tiredness";
inline constexpr std::string_view CommandDelayDiscarded = "command_delay_discarded";
inline constexpr std::string_view SicknessAlwaysHits = "sickness_always_hits";
inline constexpr std::string_view SmartMenuDropsAttackAndModerate =
    "smart_menu_drops_attack_and_moderate";
inline constexpr std::string_view CounterNeedsMp = "counter_needs_mp";
inline constexpr std::string_view LowHpWeightsLoopBug = "low_hp_weights_loop_bug";
inline constexpr std::string_view TargetScoreUsesOwnStats = "target_score_uses_own_stats";
inline constexpr std::string_view WeakestReportsLastScore = "weakest_reports_last_score";
inline constexpr std::string_view ConfusionWaitsForPartnerAttack =
    "confusion_waits_for_partner_attack";
inline constexpr std::string_view InjuryCountsAnyHit = "injury_counts_any_hit";
inline constexpr std::string_view ScriptedEnemiesFreeMp = "scripted_enemies_free_mp";
inline constexpr std::string_view ScriptedBattleEveryoneJoins = "scripted_battle_everyone_joins";
inline constexpr std::string_view MpCheckAndPaymentRoundDifferently =
    "mp_check_and_payment_round_differently";
} // namespace quirk

// The strategies the section names.
struct StrategyNames {
  std::string partnerAi;
  std::string enemyAi;
  std::string damage;
  std::string obedience;
};

struct LimitRules {
  std::int32_t maxEnemies = 0;
  std::int32_t pendingDamageCap = 0;
  std::int32_t statCap = 0;
};

// One step of the gradual damage: while the pending damage is above `above`, `amount` of it goes
// each frame.
struct DamageTickStep {
  std::int32_t above = 0;
  std::int32_t amount = 0;
};

// An affinity (in tenths) mapped to another number.
using AffinityMap = std::vector<std::pair<std::int32_t, std::int32_t>>;

struct AffinityRules {
  // factors[move special][defender special], in tenths.
  std::array<std::array<std::int32_t, SpecialCount>, SpecialCount> factors{};
  std::int32_t emptySlot = 0;
  AffinityMap moveWeightBonus;
  AffinityMap targetScore;
};

struct BuffRule {
  std::string move;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
};

struct BuffUseTier {
  std::int32_t brain = 0;
  std::int32_t uses = 0;
};

struct BuffUseRules {
  std::int32_t perBrain = 0;
  std::int32_t base = 0;
  std::vector<BuffUseTier> tiers;
};

struct MoveRules {
  std::int32_t mpCostMultiplier = 0;
  // The finishers: the original's moves finisher_first_index to finisher_last_index, and those
  // finisher_ids names.
  assets::EntryClass finishers;
  // The moves a partner can learn: the original's below learnable_below_index, and those
  // learnable_ids names.
  assets::EntryClass learnable;
  std::string counter;
  std::string flattenedShot;
  std::int32_t speedReserveFloor = 0;
  std::int32_t sharedCooldownFrames = 0;
  std::vector<BuffRule> buffs;
  BuffUseRules buffUses;
  std::vector<std::pair<std::string, std::string>> learnedTogether;
};

struct SpeedReserveRules {
  std::int32_t start = 0;
  std::int32_t cap = 0;
  std::int32_t gainEveryFrames = 0;
  std::int32_t gainPerSpeed = 0;
  std::int32_t gainBase = 0;
};

struct PacingRules {
  std::int32_t afterAttackFrames = 0;
  std::int32_t noMoveFrames = 0;
  std::int32_t rangedPatienceFrames = 0;
  std::int32_t rangedBandPercent = 0;
  std::int32_t flattenedShotEveryFrames = 0;
  std::int32_t flattenedShotFrames = 0;
  std::int32_t flattenedShotReleaseFrame = 0;
};

// How close a fighter stays to its target while it waits, beyond the contact range.
struct DistanceBand {
  std::int64_t nearSquared = 0;
  std::int64_t farSquared = 0;
};

struct SpacingRules {
  std::int32_t contactMargin = 0;
  DistanceBand standing;
  DistanceBand walking;
  std::int64_t keepAwayMarginSquared = 0;
  std::int64_t keepAwayWalkingSquared = 0;
};

struct CommandDelayRules {
  std::int32_t lowDisciplineBelow = 0;
  std::int32_t lowBase = 0;
  std::int32_t lowDivisor = 0;
  std::int32_t highBase = 0;
  std::int32_t highDivisor = 0;
  std::int32_t highScale = 0;
};

struct ObedienceRules {
  std::int32_t brainBelow = 0;
  std::int32_t rollBrainDivisor = 0;
  std::int32_t rollPeriodFrames = 0;
  std::int32_t partnerChanceBase = 0;
  std::int32_t enemyChanceBrain = 0;
  std::int32_t enemyChanceDivisor = 0;
  std::int32_t idleFrames = 0;
  // By charge mode: aggressive, normal, cautious, none.
  std::array<std::int32_t, 4> retargetChances{};
  std::int32_t enemyFollowAttackerPercent = 0;
  CommandDelayRules commandDelay;
};

// An entry of the battle menu as the ruleset names it; `Techniques` stands for the equipped
// techniques.
enum class MenuEntry : std::uint8_t {
  Finisher,
  Flee,
  YourCall,
  Attack,
  Moderate,
  Distance,
  Defend,
  Change,
  Techniques
};

struct MenuTier {
  std::int32_t brain = 0;
  std::vector<MenuEntry> entries;
};

struct PartnerAiRules {
  std::int32_t statusTargetBrain = 0;
  std::int32_t switchMargin = 0;
  std::int32_t scoredTargetBrain = 0;
  std::array<std::int32_t, 4> powerWeights{};
  std::array<std::int32_t, 4> mpWeights{};
  std::array<std::int32_t, 4> reachWeights{};
  std::int32_t evenWeight = 0;
  std::int32_t emptySlotWeight = 0;
  std::int32_t areaBrain = 0;
  std::int32_t areaMinEnemies = 0;
  std::array<std::int32_t, 3> areaWeightPerEnemy{};
  std::int32_t lowHpPercent = 0;
  std::int32_t buffPriorityBrainDivisor = 0;
  std::int32_t buffPriorityBase = 0;
  std::int32_t buffPriorityDecayFrames = 0;
  std::int32_t confusedAirPercent = 0;
  std::int32_t confusedSwingPercent = 0;
  std::int32_t confusedTurnRoll = 0;
  std::int32_t confusedTurnFrom = 0;
};

struct EnemyAiRules {
  std::int32_t areaWeight = 0;
  // A tournament rival (std_main.c: STD_tickEnemyAI) is cautious one time in this many at each
  // decision; otherwise it rolls aggressive or normal.
  std::int32_t matchCautiousRoll = 0;
};

struct HitRules {
  std::int32_t alwaysHitBelowLevel = 0;
  std::int32_t accuracyDivisor = 0;
  std::int32_t attackerSpeedDivisor = 0;
  std::int32_t speedScale = 0;
  std::int32_t standingEvasionPercent = 0;
  std::int32_t blockFrames = 0;
  std::int32_t blockedMeleeMinPercent = 0;
  std::int32_t blockedMeleeMaxPercent = 0;
  std::int32_t counterMinMp = 0;
  std::int32_t bigHitPercent = 0;
};

struct DamageRules {
  std::int32_t affinityDivisor = 0;
  std::int32_t differenceCap = 0;
  std::int32_t powerScale = 0;
  std::int32_t varianceMin = 0;
  std::int32_t varianceMax = 0;
  std::int32_t counterDefensePercent = 0;
  std::int32_t finisherChargeDivisor = 0;
  std::int32_t enemyFinisherMin = 0;
  std::int32_t enemyFinisherMax = 0;
  std::int32_t min = 0;
  std::int32_t max = 0;
};

struct StatusRules {
  std::int32_t poisonEveryFrames = 0;
  std::int32_t poisonMinPercent = 0;
  std::int32_t poisonMaxPercent = 0;
  std::int32_t confusionMinFrames = 0;
  std::int32_t confusionMaxFrames = 0;
  std::int32_t stunMinFrames = 0;
  std::int32_t stunMaxFrames = 0;
  std::int32_t flattenTransitionFrames = 0;
  std::int32_t flattenMinFrames = 0;
  std::int32_t flattenMaxFrames = 0;
};

struct FinisherRules {
  std::int32_t goalBase = 0;
  std::int32_t perFrame = 0;
  std::int32_t perAttack = 0;
  std::int32_t perBlock = 0;
  std::int32_t perBlockedAttack = 0;
  std::int32_t progressDivisor = 0;
  std::int32_t auraFrames = 0;
  std::int32_t chargePerPress = 0;
  std::int32_t chargeCap = 0;
};

struct MpDiscount {
  std::int32_t brain = 0;
  std::int32_t percent = 0;
};

struct ConditionRules {
  std::int32_t statPenaltyPercent = 0;
  std::int32_t mpCostExtraPercent = 0;
  // Highest brain first.
  std::vector<MpDiscount> mpDiscounts;
};

struct EndingRules {
  std::int32_t fleeFrames = 0;
  std::int32_t fleeIgnoresHitsFrom = 0;
  std::int32_t reviveFrames = 0;
  std::int32_t reviveCountdownShownAt = 0;
};

// The item the tamer throws (main.c: startThrowingItem, tickThrownItem): it leaves the hand at this
// frame of the throw and lands on the partner flightFrames later.
struct ItemThrowRules {
  std::int32_t releaseFrame = 0;
  std::int32_t flightFrames = 0;
};

// What the battle's HUD shows (battle_main.c: BTL_renderEnemyHPBars).
struct HudRules {
  // An enemy's HP bar shows after it takes damage, until this many frames pass with no hit reaction
  // and no damage left to drain.
  std::int32_t enemyHpBarFrames = 0;
  // battle_ui.c: createPostBattleStatsBox: the end box waits this many frames before the stats
  // count up (POST_BATTLE_STATS_TIMER), a point a frame.
  std::int32_t endCountDelayFrames = 0;
  // battle_hud.c: BTL_drawBattleEndText: a line of the end box waits this many frames, or a press,
  // before the next one (BATTLE_END_WAIT_FRAMES).
  std::int32_t endWaitFrames = 0;
};

// What a battle's outcome does to the partner's care values.
struct CareChange {
  std::int32_t happiness = 0;
  std::int32_t discipline = 0;
  std::int32_t tiredness = 0;
  std::int32_t battles = 0;
};

// What a lost battle gives back to the partner's current HP and MP, each up to its maximum.
struct StatRestore {
  std::int32_t hp = 0;
  std::int32_t mp = 0;
};

struct ConsequenceRules {
  CareChange victory;
  CareChange flee;
  CareChange defeat;
  std::int32_t afterBattleTiredness = 0;
  std::int32_t afterBattleFood = 0;
  std::int32_t mpTirednessBelowPercent = 0;
  std::int32_t mpTirednessScale = 0;
  std::int32_t defeatLives = 0;
  StatRestore defeatRestore;
  std::int32_t elapsedMinutes = 0;
};

struct StatGainFallback {
  std::int32_t hpLostPercent = 0;
  std::int32_t attacks = 0;
  std::int32_t bigHits = 0;
  std::int32_t blocks = 0;
  std::int32_t speedHpLostPercent = 0;
  std::int32_t brainAttacks = 0;
  std::int32_t brainBigHits = 0;
};

struct RewardRules {
  std::vector<std::int32_t> enemyFactors;
  std::int32_t partnerScale = 0;
  StatGainFallback fallback;
  std::vector<std::string> noDropMaps;
  std::int32_t trackedMoves = 0;
};

struct JoiningRules {
  // chances[partner type - 1][enemy type - 1].
  std::array<std::array<std::int32_t, 3>, 3> chances{};
  std::int32_t untypedChance = 0;
  std::string bellItem;
  std::int32_t bellRoll = 0;
  std::string repelItem;
  std::int32_t repelRoll = 0;
  // The maps where an NPC behind a wall from the player or the partner does not join (btl.c:
  // isScreenConcave, CONCAVE_SCREENS).
  std::vector<std::string> concaveMaps;
};

// Engine values for what the original's animations and map collision decide (Hueco until measured).
struct TimingRules {
  std::int32_t attackFrames = 0;
  std::int32_t hitFrame = 0;
  std::int32_t hitReactionFrames = 0;
  std::int32_t blockAnimationFrames = 0;
  std::int32_t faintFrames = 0;
  std::int32_t walkSpeed = 0;
  std::int32_t projectileSpeed = 0;
  std::int32_t arenaRadius = 0;
  // map.c: entityCheckCollision: the combat area, a rectangle of the screen around its centre, and
  // how high over the ground the upper corner it tests is.
  std::int32_t combatAreaWidth = 0;
  std::int32_t combatAreaHeight = 0;
  std::int32_t combatAreaCornerHeight = 0;
  // battle_setup.c: BTL_handleBattleIntro: the player runs back (animation 3, this far a frame,
  // its radius the box the area test uses) until its step leaves the combat area, which is this
  // big while it follows its way back and this big on the last stretch to where it entered the map.
  std::int32_t retreatStep = 0;
  std::int32_t retreatRadius = 0;
  std::int32_t retreatTrailAreaWidth = 0;
  std::int32_t retreatTrailAreaHeight = 0;
  std::int32_t retreatEntryAreaWidth = 0;
  std::int32_t retreatEntryAreaHeight = 0;
};

// What the techniques' effect scripts read of the fighters (battle_effect.c): the species whose
// bone-list entries each variant uses (effect_common.c: getOriginalType and its table);
// one not listed is its own.
struct EffectRules {
  std::map<std::string, std::string, std::less<>> originalTypes;

  [[nodiscard]] std::string_view originalTypeOf(std::string_view species) const {
    const auto found = originalTypes.find(species);
    return found != originalTypes.end() ? std::string_view{found->second} : species;
  }
};

struct BattleRules {
  StrategyNames strategies;
  LimitRules limits;
  std::vector<DamageTickStep> damageTick;
  AffinityRules affinity;
  MoveRules moves;
  SpeedReserveRules speedReserve;
  PacingRules pacing;
  SpacingRules spacing;
  ObedienceRules obedience;
  std::vector<MenuTier> menus;
  PartnerAiRules partnerAi;
  EnemyAiRules enemyAi;
  HitRules hit;
  DamageRules damage;
  StatusRules status;
  FinisherRules finisher;
  ConditionRules condition;
  EndingRules ending;
  ItemThrowRules itemThrow;
  HudRules hud;
  ConsequenceRules consequences;
  RewardRules rewards;
  JoiningRules joining;
  TimingRules timing;
  ruleset::Quirks quirks;
  EffectRules effects;
};

// Reads the `battle` section. The errors name the file, line and column.
core::Result<BattleRules> readBattleRules(const ruleset::Section& section);

// Reads the section of a ruleset by name.
core::Result<BattleRules> readBattleRules(const ruleset::Ruleset& ruleset);

// The check of the section for the RulesetStore: it reads it and checks that every strategy it
// names exists.
core::Result<void> checkBattleSection(const ruleset::Section& section);

// The affinity of a move's special against a defender's special, in tenths; `emptySlot` when the
// defender has none.
std::int32_t affinityOf(const AffinityRules& affinity, std::int32_t moveSpecial,
                        std::int32_t defenderSpecial);

// Looks a value up in an affinity map; 0 when the affinity is not listed.
std::int32_t mapAffinity(const AffinityMap& map, std::int32_t affinity);

} // namespace dwe::game::battle
