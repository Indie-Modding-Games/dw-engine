#include "aftermath.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;
constexpr std::int32_t MinutesPerHour = 60;
// Crossing an hour costs the partner an hour of its lifetime (tamer.c: advanceBattleTime).
constexpr std::int32_t LifetimeHoursPerHour = 1;

StatValues statsOf(const CombatStats& stats) {
  return {stats.maxHp, stats.maxMp, stats.offense, stats.defense, stats.speed, stats.brain};
}

// battle_ui.c: battleStatsGainsAndDrops: each stat gains by how strong the strongest enemy was, and
// when that gives nothing, by a chance from what the partner did.
StatValues statGains(const BattleState& state, const BattleRules& rules, ruleset::Random& random) {
  const RewardRules& rewards = rules.rewards;
  const Fighter& partner = state.partner();
  const StatValues partnerStats = statsOf(partner.startStats);
  const std::size_t enemies = state.enemyCount();
  const std::int32_t factor = rewards.enemyFactors.at(enemies - 1);
  StatValues gains{};
  for (std::size_t stat = 0; stat < StatCount; ++stat) {
    std::int32_t strongest = 0;
    for (FighterId id = 1; id <= enemies; ++id) {
      strongest = std::max(strongest, statsOf(state.fighters[id].startStats).at(stat));
    }
    const std::int32_t mine = partnerStats.at(stat);
    const std::int32_t scale = mine == 0 ? rewards.partnerScale : mine * rewards.partnerScale;
    if (strongest >= mine) {
      gains.at(stat) = (scale + (strongest * factor) - 1) / scale;
    } else if (random.below(Hundred) < strongest * factor * Hundred / scale) {
      gains.at(stat) = 1;
    }
  }
  const StatGainFallback& fallback = rewards.fallback;
  const BattleCounters& counters = state.counters;
  const std::int32_t maxHp = std::max(partner.stats.maxHp, 1);
  const std::int32_t hpLost = partner.combatant.hp - partner.hp;
  const StatValues chances{
      hpLost * fallback.hpLostPercent / maxHp,
      counters.attacks * fallback.attacks,
      counters.attacks * fallback.attacks,
      counters.bigHits * fallback.bigHits,
      (counters.blocks * fallback.blocks) + (hpLost * fallback.speedHpLostPercent / maxHp),
      (counters.attacks * fallback.brainAttacks) + (counters.bigHits * fallback.brainBigHits)};
  for (std::size_t stat = 0; stat < StatCount; ++stat) {
    if (gains.at(stat) == 0 && random.below(Hundred) < chances.at(stat)) {
      gains.at(stat) = 1;
    }
  }
  return gains;
}

std::vector<Drop> drops(const BattleSetup& setup, const BattleState& state,
                        const RewardRules& rules, ruleset::Random& random) {
  std::vector<Drop> items;
  if (std::ranges::find(rules.noDropMaps, setup.map) != rules.noDropMaps.end()) {
    return items;
  }
  // The enemies follow the partner in the fighters, in the setup's order.
  for (std::size_t index = 0; index < setup.enemies.size(); ++index) {
    const Combatant& enemy = setup.enemies[index];
    if (enemy.dropChance > random.below(Hundred) && enemy.dropItem) {
      const std::size_t fighter = index + 1;
      items.push_back({.item = *enemy.dropItem,
                       .species = enemy.species,
                       .position = fighter < state.fighters.size()
                                       ? state.fighters[fighter].position
                                       : enemy.position});
    }
  }
  return items;
}

// battle_ui.c: handleBattleEndBox, handleBattleInjury.
bool injured(const BattleSetup& setup, const BattleState& state, const BattleRules& rules,
             ruleset::Random& random) {
  const bool hit = rules.quirks.on(quirk::InjuryCountsAnyHit) ? state.counters.anyoneHit
                                                              : state.counters.partnerHit;
  if (setup.condition.injured || !hit) {
    return false;
  }
  const Fighter& partner = state.partner();
  const std::int32_t hpPercent = Hundred * partner.hp / std::max(partner.stats.maxHp, 1);
  return random.below(Hundred) < setup.condition.tiredness - hpPercent;
}

// battle_hud.c: BTL_appendCommandLearnedText: the brain at the start plus its raise reaches a
// menu; when the brain was below that menu, the entries it adds to the one before are learned.
// Only the highest menu reached counts, and the first (brain 0) teaches nothing.
std::vector<MenuEntry> commandsLearned(const std::vector<MenuTier>& menus, std::int32_t before,
                                       std::int32_t after) {
  std::size_t reached = menus.size();
  for (std::size_t index = 0; index < menus.size(); ++index) {
    if (menus[index].brain > 0 && menus[index].brain <= after) {
      reached = index;
    }
  }
  if (reached == menus.size() || before >= menus[reached].brain) {
    return {};
  }
  std::vector<MenuEntry> learned;
  for (const MenuEntry entry : menus[reached].entries) {
    if (reached == 0 || !std::ranges::contains(menus[reached - 1].entries, entry)) {
      learned.push_back(entry);
    }
  }
  return learned;
}

// battle_hud.c: BTL_appendMPBonusText: the same for the MP discounts by brain, highest first.
std::int32_t mpDiscountLearned(const std::vector<MpDiscount>& discounts, std::int32_t before,
                               std::int32_t after) {
  const auto reached = std::ranges::find_if(
      discounts, [after](const MpDiscount& discount) { return discount.brain <= after; });
  if (reached == discounts.end() || before >= reached->brain) {
    return 0;
  }
  return reached->percent;
}

// battle_ui.c: battleMoveLearning: one of the enemy techniques seen, of the partner's specials and
// in its species' list, by the move's chance for that special.
std::vector<std::string> learnedMoves(const BattleSetup& setup, const BattleState& state,
                                      const MoveCatalog& moves, ruleset::Random& random) {
  const Combatant& partner = setup.partner;
  const std::vector<std::string>& known = setup.condition.knownMoves;
  std::vector<MoveRef> candidates;
  for (const MoveRef ref : state.seenMoves) {
    const BattleMove& move = moves.at(ref);
    if (std::ranges::find(known, move.id) != known.end()) {
      continue;
    }
    const auto special =
        std::ranges::find(partner.specials, std::optional<std::int32_t>{move.special});
    if (special == partner.specials.end()) {
      continue;
    }
    if (std::ranges::find(partner.speciesMoves, std::optional<std::string>{move.id}) ==
        partner.speciesMoves.end()) {
      continue;
    }
    const auto slot = static_cast<std::size_t>(std::distance(partner.specials.begin(), special));
    const std::int32_t chance = move.learnChances ? move.learnChances->at(slot) : 0;
    if (chance > random.below(Hundred)) {
      candidates.push_back(ref);
    }
  }
  if (candidates.empty()) {
    return {};
  }
  const MoveRef learned = candidates.at(
      static_cast<std::size_t>(random.below(static_cast<std::int32_t>(candidates.size()))));
  std::vector<std::string> ids{moves.at(learned).id};
  for (const MoveRef paired : moves.learnedWith(learned)) {
    if (std::ranges::find(known, moves.at(paired).id) == known.end()) {
      ids.push_back(moves.at(paired).id);
    }
  }
  return ids;
}

// partner_impl.c: handlePostBattleTiredness: after a victory or a flight.
void addPostBattleTiredness(BattleResult& result, const Fighter& partner,
                            const BattleRules& rules) {
  const ConsequenceRules& rule = rules.consequences;
  result.care.tiredness += rule.afterBattleTiredness;
  const std::int32_t maxMp = partner.stats.maxMp;
  if (maxMp > 0 &&
      partner.mp < maxMp - (maxMp * (Hundred - rule.mpTirednessBelowPercent) / Hundred)) {
    const std::int32_t spent = maxMp - partner.mp;
    result.care.tiredness += rules.quirks.on(quirk::BrokenMpTiredness)
                                 ? spent / maxMp * rule.mpTirednessScale
                                 : spent * rule.mpTirednessScale / maxMp;
  }
  result.foodChange += rule.afterBattleFood;
}

} // namespace

BattleResult settleBattle(Outcome outcome, const BattleSetup& setup, const BattleState& state,
                          const BattleRules& rules, const MoveCatalog& moves,
                          ruleset::Random& random) {
  const Fighter& partner = state.partner();
  const ConsequenceRules& consequences = rules.consequences;
  BattleResult result;
  result.outcome = outcome;
  result.frames = state.frame;
  result.partnerHp = partner.hp;
  result.partnerMp = partner.mp;
  result.counters = state.counters;
  result.kind = setup.kind;
  result.partnerSpecies = setup.partner.species;
  // std_main.c: STD_deinitializeCombat: a tournament match leaves the partner as it ends, no more.
  if (setup.kind == BattleKind::Tournament) {
    return result;
  }
  switch (outcome) {
  case Outcome::Victory:
    for (const Combatant& enemy : setup.enemies) {
      result.bits += enemy.bits;
    }
    result.statGains = statGains(state, rules, random);
    result.drops = drops(setup, state, rules.rewards, random);
    result.injured = injured(setup, state, rules, random);
    {
      const std::int32_t brain = partner.startStats.brain;
      const std::int32_t raised =
          brain + result.statGains.at(static_cast<std::size_t>(StatIndex::Brain));
      result.commandsLearned = commandsLearned(rules.menus, brain, raised);
      result.mpDiscountLearned = mpDiscountLearned(rules.condition.mpDiscounts, brain, raised);
    }
    result.learnedMoves = learnedMoves(setup, state, moves, random);
    result.care = consequences.victory;
    result.recruitJoined = setup.recruitOnVictory;
    addPostBattleTiredness(result, partner, rules);
    break;
  case Outcome::Fled:
    result.care = consequences.flee;
    addPostBattleTiredness(result, partner, rules);
    break;
  case Outcome::Defeat:
    result.care = consequences.defeat;
    result.livesLost = consequences.defeatLives;
    // The partner leaves with the HP and MP the fight left it. A lost battle goes on at MAPHEAD
    // section 0x4DE, which calls tamer.c: tamerTickBattleLostLife and then the death map's
    // section 0x48 (dg 212): that script tells a defeat from a sickness by the current HP being 0
    // and gives the HP and MP back itself (add_stat 6 and 7).
    break;
  }
  result.elapsedMinutes = consequences.elapsedMinutes;
  result.hourCrossed = setup.startMinute + consequences.elapsedMinutes >= MinutesPerHour;
  result.lifetimeHoursLost = result.hourCrossed ? LifetimeHoursPerHour : 0;
  return result;
}

} // namespace dwe::game::battle
