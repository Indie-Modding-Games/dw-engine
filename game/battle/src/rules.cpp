#include <dwe/assets/entry_class.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/messages.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/game/battle/strategies.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::battle {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::int32_t PercentMax = 100;

// Reads the values of the section with the checks every kind of value needs, naming the key in the
// errors.
class ValueReader {
public:
  explicit ValueReader(YamlReader& reader) : reader_(&reader) {}

  YamlReader& yaml() { return *reader_; }

  bool group(const YamlNode& node, std::string_view what,
             std::initializer_list<std::string_view> keys) {
    return reader_->expectKeys(node, what, keys);
  }

  bool group(const YamlNode& node, std::string_view what,
             std::initializer_list<std::string_view> keys,
             std::initializer_list<std::string_view> optional) {
    return reader_->expectKeys(node, what, keys, optional);
  }

  const YamlNode& field(const YamlNode& node, std::string_view key) {
    return reader_->field(node, key);
  }

  std::int32_t any(const YamlNode& node, std::string_view key) {
    return reader_->integer<std::int32_t>(reader_->field(node, key));
  }

  std::int64_t any64(const YamlNode& node, std::string_view key) {
    return reader_->integer<std::int64_t>(reader_->field(node, key));
  }

  std::int32_t count(const YamlNode& node, std::string_view key) {
    const YamlNode& value = reader_->field(node, key);
    const auto number = reader_->integer<std::int32_t>(value);
    if (reader_->ok() && number < 0) {
      reader_->fail(value, messages::formatMessage(messages::NotNegative, key));
    }
    return number;
  }

  std::int64_t count64(const YamlNode& node, std::string_view key) {
    const YamlNode& value = reader_->field(node, key);
    const auto number = reader_->integer<std::int64_t>(value);
    if (reader_->ok() && number < 0) {
      reader_->fail(value, messages::formatMessage(messages::NotNegative, key));
    }
    return number;
  }

  std::int32_t positive(const YamlNode& node, std::string_view key) {
    const YamlNode& value = reader_->field(node, key);
    const auto number = reader_->integer<std::int32_t>(value);
    if (reader_->ok() && number <= 0) {
      reader_->fail(value, messages::formatMessage(messages::NotPositive, key));
    }
    return number;
  }

  std::int32_t percent(const YamlNode& node, std::string_view key) {
    return percentOf(reader_->field(node, key), key);
  }

  std::int32_t percentOf(const YamlNode& value, std::string_view what) {
    const auto number = reader_->integer<std::int32_t>(value);
    if (reader_->ok() && (number < 0 || number > PercentMax)) {
      reader_->fail(value, messages::formatMessage(messages::NotPercent, what));
    }
    return number;
  }

  std::string text(const YamlNode& node, std::string_view key) {
    return reader_->text(reader_->field(node, key));
  }

  template <std::size_t N>
  std::array<std::int32_t, N> numbers(const YamlNode& node, std::string_view key, bool percents) {
    std::array<std::int32_t, N> values{};
    const std::span<const YamlNode> items = reader_->sequence(reader_->field(node, key), key, N);
    for (std::size_t i = 0; i < items.size(); ++i) {
      values.at(i) = percents ? percentOf(items[i], key) : reader_->integer<std::int32_t>(items[i]);
    }
    return values;
  }

  void ordered(const YamlNode& node, std::string_view low, std::int32_t lowValue,
               std::string_view high, std::int32_t highValue) {
    if (reader_->ok() && lowValue > highValue) {
      reader_->fail(
          node, messages::formatMessage(messages::NotOrdered, messages::SectionText, low, high));
    }
  }

private:
  YamlReader* reader_;
};

StrategyNames readStrategies(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "strategies", {"partner_ai", "enemy_ai", "damage", "obedience"})) {
    return {};
  }
  return {.partnerAi = in.text(node, "partner_ai"),
          .enemyAi = in.text(node, "enemy_ai"),
          .damage = in.text(node, "damage"),
          .obedience = in.text(node, "obedience")};
}

LimitRules readLimits(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "limits", {"max_enemies", "pending_damage_cap", "stat_cap"})) {
    return {};
  }
  return {.maxEnemies = in.positive(node, "max_enemies"),
          .pendingDamageCap = in.positive(node, "pending_damage_cap"),
          .statCap = in.positive(node, "stat_cap")};
}

std::vector<DamageTickStep> readDamageTick(ValueReader& in, const YamlNode& node) {
  std::vector<DamageTickStep> steps;
  for (const YamlNode& item : in.yaml().sequence(node, "damage_tick")) {
    if (!in.group(item, "a damage_tick step", {"above", "amount"})) {
      break;
    }
    steps.push_back({.above = in.count(item, "above"), .amount = in.positive(item, "amount")});
  }
  return steps;
}

AffinityMap readAffinityMap(ValueReader& in, const YamlNode& node, std::string_view what) {
  AffinityMap map;
  if (!node.isMapping()) {
    in.yaml().fail(node, messages::formatMessage(messages::NotAffinityKey, what));
    return map;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    std::int32_t affinity = 0;
    const auto parsed =
        std::from_chars(entry.key.data(), entry.key.data() + entry.key.size(), affinity);
    if (parsed.ec != std::errc{} || parsed.ptr != entry.key.data() + entry.key.size()) {
      in.yaml().fail(entry.value, messages::formatMessage(messages::NotAffinityKey, entry.key));
      break;
    }
    map.emplace_back(affinity, in.yaml().integer<std::int32_t>(entry.value));
  }
  return map;
}

AffinityRules readAffinity(ValueReader& in, const YamlNode& node) {
  AffinityRules affinity;
  if (!in.group(node, "affinity", {"factors", "empty_slot", "move_weight_bonus", "target_score"})) {
    return affinity;
  }
  const std::span<const YamlNode> rows =
      in.yaml().sequence(in.field(node, "factors"), "factors", SpecialCount);
  for (std::size_t row = 0; row < rows.size(); ++row) {
    const std::span<const YamlNode> cells =
        in.yaml().sequence(rows[row], "a row of factors", SpecialCount);
    for (std::size_t column = 0; column < cells.size(); ++column) {
      affinity.factors.at(row).at(column) = in.yaml().integer<std::int32_t>(cells[column]);
    }
  }
  affinity.emptySlot = in.positive(node, "empty_slot");
  affinity.moveWeightBonus =
      readAffinityMap(in, in.field(node, "move_weight_bonus"), "move_weight_bonus");
  affinity.targetScore = readAffinityMap(in, in.field(node, "target_score"), "target_score");
  return affinity;
}

BuffUseRules readBuffUses(ValueReader& in, const YamlNode& node) {
  BuffUseRules uses;
  if (!in.group(node, "buff_uses", {"per_brain", "base", "tiers"})) {
    return uses;
  }
  uses.perBrain = in.positive(node, "per_brain");
  uses.base = in.count(node, "base");
  for (const YamlNode& item : in.yaml().sequence(in.field(node, "tiers"), "tiers")) {
    if (!in.group(item, "a buff_uses tier", {"brain", "uses"})) {
      break;
    }
    uses.tiers.push_back({.brain = in.count(item, "brain"), .uses = in.count(item, "uses")});
  }
  return uses;
}

MoveRules readMoves(ValueReader& in, const YamlNode& node) {
  MoveRules moves;
  if (!in.group(node, "moves",
                {"mp_cost_multiplier", "finisher_first_index", "finisher_last_index",
                 "learnable_below_index", "counter", "flattened_shot", "speed_reserve_floor",
                 "shared_cooldown_frames", "buffs", "buff_uses", "learned_together"},
                {"finisher_ids", "learnable_ids"})) {
    return moves;
  }
  moves.mpCostMultiplier = in.positive(node, "mp_cost_multiplier");
  const std::int32_t finisherFirst = in.count(node, "finisher_first_index");
  const std::int32_t finisherLast = in.count(node, "finisher_last_index");
  in.ordered(node, "finisher_first_index", finisherFirst, "finisher_last_index", finisherLast);
  moves.finishers.ranges = {{.from = finisherFirst, .to = finisherLast}};
  moves.learnable.below = in.count(node, "learnable_below_index");
  if (const YamlNode* ids = node.find("finisher_ids")) {
    assets::readEntryIds(in.yaml(), *ids, "finisher_ids", moves.finishers);
  }
  if (const YamlNode* ids = node.find("learnable_ids")) {
    assets::readEntryIds(in.yaml(), *ids, "learnable_ids", moves.learnable);
  }
  moves.counter = in.text(node, "counter");
  moves.flattenedShot = in.text(node, "flattened_shot");
  moves.speedReserveFloor = in.any(node, "speed_reserve_floor");
  moves.sharedCooldownFrames = in.count(node, "shared_cooldown_frames");
  for (const YamlNode& item : in.yaml().sequence(in.field(node, "buffs"), "buffs")) {
    if (!in.group(item, "a buff", {"move", "offense", "defense", "speed"})) {
      break;
    }
    moves.buffs.push_back({.move = in.text(item, "move"),
                           .offense = in.percent(item, "offense"),
                           .defense = in.percent(item, "defense"),
                           .speed = in.percent(item, "speed")});
  }
  moves.buffUses = readBuffUses(in, in.field(node, "buff_uses"));
  for (const YamlNode& pair :
       in.yaml().sequence(in.field(node, "learned_together"), "learned_together")) {
    const std::span<const YamlNode> both = in.yaml().sequence(pair, "a pair learned together", 2);
    if (both.size() == 2) {
      moves.learnedTogether.emplace_back(in.yaml().text(both[0]), in.yaml().text(both[1]));
    }
  }
  return moves;
}

SpeedReserveRules readSpeedReserve(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "speed_reserve",
                {"start", "cap", "gain_every_frames", "gain_per_speed", "gain_base"})) {
    return {};
  }
  return {.start = in.any(node, "start"),
          .cap = in.positive(node, "cap"),
          .gainEveryFrames = in.positive(node, "gain_every_frames"),
          .gainPerSpeed = in.positive(node, "gain_per_speed"),
          .gainBase = in.count(node, "gain_base")};
}

PacingRules readPacing(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "pacing",
                {"after_attack_frames", "no_move_frames", "ranged_patience_frames",
                 "ranged_band_percent", "flattened_shot_every_frames", "flattened_shot_frames",
                 "flattened_shot_release_frame"})) {
    return {};
  }
  PacingRules pacing{.afterAttackFrames = in.count(node, "after_attack_frames"),
                     .noMoveFrames = in.count(node, "no_move_frames"),
                     .rangedPatienceFrames = in.count(node, "ranged_patience_frames"),
                     .rangedBandPercent = in.percent(node, "ranged_band_percent"),
                     .flattenedShotEveryFrames = in.positive(node, "flattened_shot_every_frames"),
                     .flattenedShotFrames = in.positive(node, "flattened_shot_frames"),
                     .flattenedShotReleaseFrame = in.count(node, "flattened_shot_release_frame")};
  if (in.yaml().ok() && pacing.flattenedShotReleaseFrame >= pacing.flattenedShotFrames) {
    in.yaml().fail(node, messages::ShotAfterAttack);
  }
  return pacing;
}

DistanceBand readBand(ValueReader& in, const YamlNode& node, std::string_view what) {
  if (!in.group(node, what, {"near_squared", "far_squared"})) {
    return {};
  }
  return {.nearSquared = in.count64(node, "near_squared"),
          .farSquared = in.count64(node, "far_squared")};
}

SpacingRules readSpacing(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "spacing",
                {"contact_margin", "standing", "walking", "keep_away_margin_squared",
                 "keep_away_walking_squared"})) {
    return {};
  }
  return {.contactMargin = in.count(node, "contact_margin"),
          .standing = readBand(in, in.field(node, "standing"), "standing"),
          .walking = readBand(in, in.field(node, "walking"), "walking"),
          .keepAwayMarginSquared = in.count64(node, "keep_away_margin_squared"),
          .keepAwayWalkingSquared = in.count64(node, "keep_away_walking_squared")};
}

CommandDelayRules readCommandDelay(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "command_delay",
                {"low_discipline_below", "low_base", "low_divisor", "high_base", "high_divisor",
                 "high_scale"})) {
    return {};
  }
  return {.lowDisciplineBelow = in.count(node, "low_discipline_below"),
          .lowBase = in.count(node, "low_base"),
          .lowDivisor = in.positive(node, "low_divisor"),
          .highBase = in.count(node, "high_base"),
          .highDivisor = in.positive(node, "high_divisor"),
          .highScale = in.count(node, "high_scale")};
}

ObedienceRules readObedience(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "obedience",
                {"brain_below", "roll_brain_divisor", "roll_period_frames", "partner_chance_base",
                 "enemy_chance_brain", "enemy_chance_divisor", "idle_frames", "retarget_chances",
                 "enemy_follow_attacker_percent", "command_delay"})) {
    return {};
  }
  return {.brainBelow = in.count(node, "brain_below"),
          .rollBrainDivisor = in.positive(node, "roll_brain_divisor"),
          .rollPeriodFrames = in.positive(node, "roll_period_frames"),
          .partnerChanceBase = in.count(node, "partner_chance_base"),
          .enemyChanceBrain = in.count(node, "enemy_chance_brain"),
          .enemyChanceDivisor = in.positive(node, "enemy_chance_divisor"),
          .idleFrames = in.positive(node, "idle_frames"),
          .retargetChances = in.numbers<4>(node, "retarget_chances", true),
          .enemyFollowAttackerPercent = in.percent(node, "enemy_follow_attacker_percent"),
          .commandDelay = readCommandDelay(in, in.field(node, "command_delay"))};
}

constexpr std::array<std::pair<std::string_view, MenuEntry>, 9> MenuEntryNames{{
    {"finisher", MenuEntry::Finisher},
    {"flee", MenuEntry::Flee},
    {"your_call", MenuEntry::YourCall},
    {"attack", MenuEntry::Attack},
    {"moderate", MenuEntry::Moderate},
    {"distance", MenuEntry::Distance},
    {"defend", MenuEntry::Defend},
    {"change", MenuEntry::Change},
    {"techniques", MenuEntry::Techniques},
}};

std::vector<MenuTier> readMenus(ValueReader& in, const YamlNode& node) {
  std::vector<MenuTier> menus;
  if (!in.group(node, "commands", {"menus"})) {
    return menus;
  }
  for (const YamlNode& item : in.yaml().sequence(in.field(node, "menus"), "menus")) {
    if (!in.group(item, "a menu", {"brain", "commands"})) {
      break;
    }
    MenuTier tier{.brain = in.count(item, "brain"), .entries = {}};
    for (const YamlNode& entry : in.yaml().sequence(in.field(item, "commands"), "commands")) {
      const std::string name = in.yaml().text(entry);
      const auto found = std::ranges::find(MenuEntryNames, std::string_view{name},
                                           &std::pair<std::string_view, MenuEntry>::first);
      if (found == MenuEntryNames.end()) {
        in.yaml().fail(entry, messages::formatMessage(messages::UnknownMenuEntry, name));
        break;
      }
      tier.entries.push_back(found->second);
    }
    const bool sorted = menus.empty() ? tier.brain == 0 : menus.back().brain < tier.brain;
    if (in.yaml().ok() && !sorted) {
      in.yaml().fail(item, messages::MenusNotSorted);
    }
    menus.push_back(std::move(tier));
  }
  return menus;
}

PartnerAiRules readPartnerAi(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "partner_ai",
                {"status_target_brain", "switch_margin", "scored_target_brain", "power_weights",
                 "mp_weights", "reach_weights", "even_weight", "empty_slot_weight", "area_brain",
                 "area_min_enemies", "area_weight_per_enemy", "low_hp_percent",
                 "buff_priority_brain_divisor", "buff_priority_base", "buff_priority_decay_frames",
                 "confused_air_percent", "confused_swing_percent", "confused_turn_roll",
                 "confused_turn_from"})) {
    return {};
  }
  PartnerAiRules ai{.statusTargetBrain = in.count(node, "status_target_brain"),
                    .switchMargin = in.any(node, "switch_margin"),
                    .scoredTargetBrain = in.count(node, "scored_target_brain"),
                    .powerWeights = in.numbers<4>(node, "power_weights", false),
                    .mpWeights = in.numbers<4>(node, "mp_weights", false),
                    .reachWeights = in.numbers<4>(node, "reach_weights", false),
                    .evenWeight = in.count(node, "even_weight"),
                    .emptySlotWeight = in.count(node, "empty_slot_weight"),
                    .areaBrain = in.count(node, "area_brain"),
                    .areaMinEnemies = in.count(node, "area_min_enemies"),
                    .areaWeightPerEnemy = in.numbers<3>(node, "area_weight_per_enemy", false),
                    .lowHpPercent = in.percent(node, "low_hp_percent"),
                    .buffPriorityBrainDivisor = in.positive(node, "buff_priority_brain_divisor"),
                    .buffPriorityBase = in.count(node, "buff_priority_base"),
                    .buffPriorityDecayFrames = in.positive(node, "buff_priority_decay_frames"),
                    .confusedAirPercent = in.percent(node, "confused_air_percent"),
                    .confusedSwingPercent = in.percent(node, "confused_swing_percent"),
                    .confusedTurnRoll = in.positive(node, "confused_turn_roll"),
                    .confusedTurnFrom = in.count(node, "confused_turn_from")};
  in.ordered(node, "confused_turn_from", ai.confusedTurnFrom, "confused_turn_roll",
             ai.confusedTurnRoll);
  return ai;
}

EnemyAiRules readEnemyAi(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "enemy_ai", {"area_weight", "match_cautious_roll"})) {
    return {};
  }
  return {.areaWeight = in.count(node, "area_weight"),
          .matchCautiousRoll = in.positive(node, "match_cautious_roll")};
}

HitRules readHit(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "hit",
                {"always_hit_below_level", "accuracy_divisor", "attacker_speed_divisor",
                 "speed_scale", "standing_evasion_percent", "block_frames",
                 "blocked_melee_min_percent", "blocked_melee_max_percent", "counter_min_mp",
                 "big_hit_percent"})) {
    return {};
  }
  HitRules hit{.alwaysHitBelowLevel = in.count(node, "always_hit_below_level"),
               .accuracyDivisor = in.positive(node, "accuracy_divisor"),
               .attackerSpeedDivisor = in.positive(node, "attacker_speed_divisor"),
               .speedScale = in.positive(node, "speed_scale"),
               .standingEvasionPercent = in.count(node, "standing_evasion_percent"),
               .blockFrames = in.count(node, "block_frames"),
               .blockedMeleeMinPercent = in.percent(node, "blocked_melee_min_percent"),
               .blockedMeleeMaxPercent = in.percent(node, "blocked_melee_max_percent"),
               .counterMinMp = in.count(node, "counter_min_mp"),
               .bigHitPercent = in.percent(node, "big_hit_percent")};
  in.ordered(node, "blocked_melee_min_percent", hit.blockedMeleeMinPercent,
             "blocked_melee_max_percent", hit.blockedMeleeMaxPercent);
  return hit;
}

DamageRules readDamage(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "damage",
                {"affinity_divisor", "difference_cap", "power_scale", "variance_min",
                 "variance_max", "counter_defense_percent", "finisher_charge_divisor",
                 "enemy_finisher_min", "enemy_finisher_max", "min", "max"})) {
    return {};
  }
  DamageRules damage{.affinityDivisor = in.positive(node, "affinity_divisor"),
                     .differenceCap = in.count(node, "difference_cap"),
                     .powerScale = in.positive(node, "power_scale"),
                     .varianceMin = in.count(node, "variance_min"),
                     .varianceMax = in.count(node, "variance_max"),
                     .counterDefensePercent = in.percent(node, "counter_defense_percent"),
                     .finisherChargeDivisor = in.positive(node, "finisher_charge_divisor"),
                     .enemyFinisherMin = in.count(node, "enemy_finisher_min"),
                     .enemyFinisherMax = in.count(node, "enemy_finisher_max"),
                     .min = in.count(node, "min"),
                     .max = in.positive(node, "max")};
  in.ordered(node, "variance_min", damage.varianceMin, "variance_max", damage.varianceMax);
  in.ordered(node, "enemy_finisher_min", damage.enemyFinisherMin, "enemy_finisher_max",
             damage.enemyFinisherMax);
  in.ordered(node, "min", damage.min, "max", damage.max);
  return damage;
}

StatusRules readStatus(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "status",
                {"poison_every_frames", "poison_min_percent", "poison_max_percent",
                 "confusion_min_frames", "confusion_max_frames", "stun_min_frames",
                 "stun_max_frames", "flatten_transition_frames", "flatten_min_frames",
                 "flatten_max_frames"})) {
    return {};
  }
  StatusRules status{.poisonEveryFrames = in.positive(node, "poison_every_frames"),
                     .poisonMinPercent = in.percent(node, "poison_min_percent"),
                     .poisonMaxPercent = in.percent(node, "poison_max_percent"),
                     .confusionMinFrames = in.positive(node, "confusion_min_frames"),
                     .confusionMaxFrames = in.positive(node, "confusion_max_frames"),
                     .stunMinFrames = in.positive(node, "stun_min_frames"),
                     .stunMaxFrames = in.positive(node, "stun_max_frames"),
                     .flattenTransitionFrames = in.positive(node, "flatten_transition_frames"),
                     .flattenMinFrames = in.positive(node, "flatten_min_frames"),
                     .flattenMaxFrames = in.positive(node, "flatten_max_frames")};
  in.ordered(node, "poison_min_percent", status.poisonMinPercent, "poison_max_percent",
             status.poisonMaxPercent);
  in.ordered(node, "confusion_min_frames", status.confusionMinFrames, "confusion_max_frames",
             status.confusionMaxFrames);
  in.ordered(node, "stun_min_frames", status.stunMinFrames, "stun_max_frames",
             status.stunMaxFrames);
  in.ordered(node, "flatten_transition_frames", status.flattenTransitionFrames,
             "flatten_min_frames", status.flattenMinFrames);
  in.ordered(node, "flatten_min_frames", status.flattenMinFrames, "flatten_max_frames",
             status.flattenMaxFrames);
  return status;
}

FinisherRules readFinisher(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "finisher",
                {"goal_base", "per_frame", "per_attack", "per_block", "per_blocked_attack",
                 "progress_divisor", "aura_frames", "charge_per_press", "charge_cap"})) {
    return {};
  }
  return {.goalBase = in.positive(node, "goal_base"),
          .perFrame = in.count(node, "per_frame"),
          .perAttack = in.count(node, "per_attack"),
          .perBlock = in.count(node, "per_block"),
          .perBlockedAttack = in.count(node, "per_blocked_attack"),
          .progressDivisor = in.positive(node, "progress_divisor"),
          .auraFrames = in.count(node, "aura_frames"),
          .chargePerPress = in.count(node, "charge_per_press"),
          .chargeCap = in.count(node, "charge_cap")};
}

ConditionRules readCondition(ValueReader& in, const YamlNode& node) {
  ConditionRules condition;
  if (!in.group(node, "condition",
                {"stat_penalty_percent", "mp_cost_extra_percent", "mp_discounts"})) {
    return condition;
  }
  condition.statPenaltyPercent = in.percent(node, "stat_penalty_percent");
  condition.mpCostExtraPercent = in.count(node, "mp_cost_extra_percent");
  for (const YamlNode& item : in.yaml().sequence(in.field(node, "mp_discounts"), "mp_discounts")) {
    if (!in.group(item, "an MP discount", {"brain", "percent"})) {
      break;
    }
    const MpDiscount discount{.brain = in.count(item, "brain"),
                              .percent = in.percent(item, "percent")};
    if (in.yaml().ok() && !condition.mpDiscounts.empty() &&
        condition.mpDiscounts.back().brain <= discount.brain) {
      in.yaml().fail(item, messages::DiscountsNotSorted);
    }
    condition.mpDiscounts.push_back(discount);
  }
  return condition;
}

EndingRules readEnding(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "ending",
                {"flee_frames", "flee_ignores_hits_from", "revive_frames",
                 "revive_countdown_shown_at"})) {
    return {};
  }
  EndingRules ending{.fleeFrames = in.positive(node, "flee_frames"),
                     .fleeIgnoresHitsFrom = in.count(node, "flee_ignores_hits_from"),
                     .reviveFrames = in.positive(node, "revive_frames"),
                     .reviveCountdownShownAt = in.count(node, "revive_countdown_shown_at")};
  in.ordered(node, "flee_ignores_hits_from", ending.fleeIgnoresHitsFrom, "flee_frames",
             ending.fleeFrames);
  in.ordered(node, "revive_countdown_shown_at", ending.reviveCountdownShownAt, "revive_frames",
             ending.reviveFrames);
  return ending;
}

ItemThrowRules readItemThrow(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "item_throw", {"release_frame", "flight_frames"})) {
    return {};
  }
  return {.releaseFrame = in.count(node, "release_frame"),
          .flightFrames = in.positive(node, "flight_frames")};
}

HudRules readHud(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "hud",
                {"enemy_hp_bar_frames", "end_count_delay_frames", "end_wait_frames"})) {
    return {};
  }
  return {.enemyHpBarFrames = in.positive(node, "enemy_hp_bar_frames"),
          .endCountDelayFrames = in.count(node, "end_count_delay_frames"),
          .endWaitFrames = in.count(node, "end_wait_frames")};
}

CareChange readCareChange(ValueReader& in, const YamlNode& node, std::string_view what) {
  if (!in.group(node, what, {"happiness", "discipline", "tiredness", "battles"})) {
    return {};
  }
  return {.happiness = in.any(node, "happiness"),
          .discipline = in.any(node, "discipline"),
          .tiredness = in.any(node, "tiredness"),
          .battles = in.count(node, "battles")};
}

StatRestore readStatRestore(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "defeat_restore", {"hp", "mp"})) {
    return {};
  }
  return {.hp = in.count(node, "hp"), .mp = in.count(node, "mp")};
}

ConsequenceRules readConsequences(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "consequences",
                {"victory", "flee", "defeat", "after_battle_tiredness", "after_battle_food",
                 "mp_tiredness_below_percent", "mp_tiredness_scale", "defeat_lives",
                 "defeat_restore", "elapsed_minutes"})) {
    return {};
  }
  return {.victory = readCareChange(in, in.field(node, "victory"), "victory"),
          .flee = readCareChange(in, in.field(node, "flee"), "flee"),
          .defeat = readCareChange(in, in.field(node, "defeat"), "defeat"),
          .afterBattleTiredness = in.any(node, "after_battle_tiredness"),
          .afterBattleFood = in.any(node, "after_battle_food"),
          .mpTirednessBelowPercent = in.percent(node, "mp_tiredness_below_percent"),
          .mpTirednessScale = in.count(node, "mp_tiredness_scale"),
          .defeatLives = in.count(node, "defeat_lives"),
          .defeatRestore = readStatRestore(in, in.field(node, "defeat_restore")),
          .elapsedMinutes = in.count(node, "elapsed_minutes")};
}

StatGainFallback readFallback(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "fallback",
                {"hp_lost_percent", "attacks", "big_hits", "blocks", "speed_hp_lost_percent",
                 "brain_attacks", "brain_big_hits"})) {
    return {};
  }
  return {.hpLostPercent = in.percent(node, "hp_lost_percent"),
          .attacks = in.count(node, "attacks"),
          .bigHits = in.count(node, "big_hits"),
          .blocks = in.count(node, "blocks"),
          .speedHpLostPercent = in.percent(node, "speed_hp_lost_percent"),
          .brainAttacks = in.count(node, "brain_attacks"),
          .brainBigHits = in.count(node, "brain_big_hits")};
}

RewardRules readRewards(ValueReader& in, const YamlNode& node) {
  RewardRules rewards;
  if (!in.group(node, "rewards",
                {"enemy_factors", "partner_scale", "fallback", "no_drop_maps", "tracked_moves"})) {
    return rewards;
  }
  for (const YamlNode& item :
       in.yaml().sequence(in.field(node, "enemy_factors"), "enemy_factors")) {
    rewards.enemyFactors.push_back(in.yaml().integer<std::int32_t>(item));
  }
  rewards.partnerScale = in.positive(node, "partner_scale");
  rewards.fallback = readFallback(in, in.field(node, "fallback"));
  for (const YamlNode& item : in.yaml().sequence(in.field(node, "no_drop_maps"), "no_drop_maps")) {
    rewards.noDropMaps.push_back(in.yaml().text(item));
  }
  rewards.trackedMoves = in.count(node, "tracked_moves");
  return rewards;
}

JoiningRules readJoining(ValueReader& in, const YamlNode& node) {
  JoiningRules joining;
  if (!in.group(node, "joining",
                {"chances", "untyped_chance", "bell_item", "bell_roll", "repel_item", "repel_roll"},
                {"concave_maps"})) {
    return joining;
  }
  const std::span<const YamlNode> rows =
      in.yaml().sequence(in.field(node, "chances"), "chances", 3);
  for (std::size_t row = 0; row < rows.size(); ++row) {
    const std::span<const YamlNode> cells = in.yaml().sequence(rows[row], "a row of chances", 3);
    for (std::size_t column = 0; column < cells.size(); ++column) {
      joining.chances.at(row).at(column) = in.percentOf(cells[column], "chances");
    }
  }
  joining.untypedChance = in.percent(node, "untyped_chance");
  joining.bellItem = in.text(node, "bell_item");
  joining.bellRoll = in.any(node, "bell_roll");
  joining.repelItem = in.text(node, "repel_item");
  joining.repelRoll = in.any(node, "repel_roll");
  if (const YamlNode* maps = node.find("concave_maps")) {
    for (const YamlNode& map : in.yaml().sequence(*maps, "concave_maps")) {
      joining.concaveMaps.push_back(in.yaml().text(map));
    }
  }
  return joining;
}

TimingRules readTiming(ValueReader& in, const YamlNode& node) {
  if (!in.group(node, "timing",
                {"attack_frames", "hit_frame", "hit_reaction_frames", "block_animation_frames",
                 "faint_frames", "walk_speed", "projectile_speed", "arena_radius",
                 "combat_area_width", "combat_area_height", "combat_area_corner_height",
                 "retreat_step", "retreat_radius", "retreat_trail_area_width",
                 "retreat_trail_area_height", "retreat_entry_area_width",
                 "retreat_entry_area_height"})) {
    return {};
  }
  TimingRules timing{.attackFrames = in.positive(node, "attack_frames"),
                     .hitFrame = in.count(node, "hit_frame"),
                     .hitReactionFrames = in.count(node, "hit_reaction_frames"),
                     .blockAnimationFrames = in.count(node, "block_animation_frames"),
                     .faintFrames = in.positive(node, "faint_frames"),
                     .walkSpeed = in.positive(node, "walk_speed"),
                     .projectileSpeed = in.positive(node, "projectile_speed"),
                     .arenaRadius = in.positive(node, "arena_radius"),
                     .combatAreaWidth = in.positive(node, "combat_area_width"),
                     .combatAreaHeight = in.positive(node, "combat_area_height"),
                     .combatAreaCornerHeight = in.count(node, "combat_area_corner_height"),
                     .retreatStep = in.positive(node, "retreat_step"),
                     .retreatRadius = in.count(node, "retreat_radius"),
                     .retreatTrailAreaWidth = in.positive(node, "retreat_trail_area_width"),
                     .retreatTrailAreaHeight = in.positive(node, "retreat_trail_area_height"),
                     .retreatEntryAreaWidth = in.positive(node, "retreat_entry_area_width"),
                     .retreatEntryAreaHeight = in.positive(node, "retreat_entry_area_height")};
  if (in.yaml().ok() && timing.hitFrame >= timing.attackFrames) {
    in.yaml().fail(node, messages::HitAfterAttack);
  }
  return timing;
}

// The optional `effects` group: without it every species is its own original type.
EffectRules readEffects(ValueReader& in, const YamlNode& root) {
  EffectRules effects;
  const YamlNode* node = root.find("effects");
  if (node == nullptr || !in.group(*node, "effects", {"original_types"})) {
    return effects;
  }
  const YamlNode& types = in.field(*node, "original_types");
  if (!types.isMapping()) {
    in.yaml().fail(types, messages::formatMessage(messages::NotSpeciesMap, "original_types"));
    return effects;
  }
  for (const core::YamlEntry& entry : types.entries()) {
    effects.originalTypes.emplace(entry.key, in.yaml().text(entry.value));
  }
  return effects;
}

void checkEnemyFactors(YamlReader& reader, const YamlNode& node, const BattleRules& rules) {
  if (reader.ok() &&
      std::cmp_not_equal(rules.rewards.enemyFactors.size(), rules.limits.maxEnemies)) {
    reader.fail(node, messages::NoEnemyFactors);
  }
}

} // namespace

core::Result<BattleRules> readBattleRules(const ruleset::Section& section) {
  YamlReader reader;
  ValueReader in{reader};
  const YamlNode& root = section.node;
  if (!in.group(root, messages::SectionText,
                {"strategies",    "limits",    "damage_tick", "affinity",   "moves",
                 "speed_reserve", "pacing",    "spacing",     "obedience",  "commands",
                 "partner_ai",    "enemy_ai",  "hit",         "damage",     "status",
                 "finisher",      "condition", "ending",      "item_throw", "hud",
                 "consequences",  "rewards",   "joining",     "timing",     "quirks"},
                {"effects"})) {
    return std::unexpected{reader.error(section.file)};
  }
  BattleRules rules{.strategies = readStrategies(in, in.field(root, "strategies")),
                    .limits = readLimits(in, in.field(root, "limits")),
                    .damageTick = readDamageTick(in, in.field(root, "damage_tick")),
                    .affinity = readAffinity(in, in.field(root, "affinity")),
                    .moves = readMoves(in, in.field(root, "moves")),
                    .speedReserve = readSpeedReserve(in, in.field(root, "speed_reserve")),
                    .pacing = readPacing(in, in.field(root, "pacing")),
                    .spacing = readSpacing(in, in.field(root, "spacing")),
                    .obedience = readObedience(in, in.field(root, "obedience")),
                    .menus = readMenus(in, in.field(root, "commands")),
                    .partnerAi = readPartnerAi(in, in.field(root, "partner_ai")),
                    .enemyAi = readEnemyAi(in, in.field(root, "enemy_ai")),
                    .hit = readHit(in, in.field(root, "hit")),
                    .damage = readDamage(in, in.field(root, "damage")),
                    .status = readStatus(in, in.field(root, "status")),
                    .finisher = readFinisher(in, in.field(root, "finisher")),
                    .condition = readCondition(in, in.field(root, "condition")),
                    .ending = readEnding(in, in.field(root, "ending")),
                    .itemThrow = readItemThrow(in, in.field(root, "item_throw")),
                    .hud = readHud(in, in.field(root, "hud")),
                    .consequences = readConsequences(in, in.field(root, "consequences")),
                    .rewards = readRewards(in, in.field(root, "rewards")),
                    .joining = readJoining(in, in.field(root, "joining")),
                    .timing = readTiming(in, in.field(root, "timing")),
                    .quirks = {},
                    .effects = readEffects(in, root)};
  checkEnemyFactors(reader, in.field(root, "rewards"), rules);
  rules.quirks = ruleset::readQuirks(
      reader, in.field(root, ruleset::QuirksKey),
      {quirk::BrokenMpTiredness, quirk::CommandDelayDiscarded, quirk::SicknessAlwaysHits,
       quirk::SmartMenuDropsAttackAndModerate, quirk::CounterNeedsMp, quirk::LowHpWeightsLoopBug,
       quirk::TargetScoreUsesOwnStats, quirk::WeakestReportsLastScore,
       quirk::ConfusionWaitsForPartnerAttack, quirk::InjuryCountsAnyHit,
       quirk::ScriptedEnemiesFreeMp, quirk::ScriptedBattleEveryoneJoins,
       quirk::MpCheckAndPaymentRoundDifferently});
  return reader.finish(std::move(rules), section.file);
}

core::Result<BattleRules> readBattleRules(const ruleset::Ruleset& ruleset) {
  auto section = ruleset.requireSection(SectionName);
  if (!section) {
    return std::unexpected{section.error()};
  }
  return readBattleRules(**section);
}

core::Result<void> checkBattleSection(const ruleset::Section& section) {
  auto rules = readBattleRules(section);
  if (!rules) {
    return std::unexpected{rules.error()};
  }
  return checkStrategyNames(section);
}

std::int32_t affinityOf(const AffinityRules& affinity, std::int32_t moveSpecial,
                        std::int32_t defenderSpecial) {
  const auto inTable = [](std::int32_t special) {
    return special >= 0 && std::cmp_less(special, SpecialCount);
  };
  if (!inTable(moveSpecial) || !inTable(defenderSpecial)) {
    return affinity.emptySlot;
  }
  return affinity.factors.at(static_cast<std::size_t>(moveSpecial))
      .at(static_cast<std::size_t>(defenderSpecial));
}

std::int32_t mapAffinity(const AffinityMap& map, std::int32_t affinity) {
  const auto found =
      std::ranges::find(map, affinity, &std::pair<std::int32_t, std::int32_t>::first);
  return found == map.end() ? 0 : found->second;
}

} // namespace dwe::game::battle
