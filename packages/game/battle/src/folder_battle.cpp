#include "folder_battle.hpp"

#include <dwe/game/battle/folder_formulas.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "battle_system.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;
// The original's block spark (BTL_tickAttackHits), which a covered hit shows too.
constexpr std::int32_t BlockSparkFrames = 0x11;
constexpr std::int32_t BlockSparkKind = 0;

const FolderRules& rulesOf(const BattleContext& context) {
  return *context.rules->folder;
}

void emitFor(const BattleContext& context, EventKind kind, FighterId id, std::int32_t amount) {
  context.emit({.kind = kind,
                .fighter = id,
                .other = std::nullopt,
                .move = std::nullopt,
                .amount = amount,
                .status = StatusEffect::None,
                .command = Command::None});
}

// §6.2, §3.8: the damage of `attack` on its defender, covered or not, with the difficulty's share
// for the partner as the Classic battle takes it.
std::int32_t damageOf(BattleContext& context, const AttackInFlight& attack, bool covered) {
  const FolderRules& rules = rulesOf(context);
  const BattleState& state = *context.state;
  const Fighter& attacker = context.fighter(attack.attacker);
  const Fighter& defender = context.fighter(attack.defender);
  const BattleMove& move = context.moves->at(attack.move);
  const FighterStats offense = folderStats(context, attack.attacker);
  const FighterStats defense = folderStats(context, attack.defender);
  const std::int32_t modifiers =
      folder::elementPercent(rules, move.special, attacker.combatant.specials,
                             defender.combatant.specials) +
      folder::typePercent(rules, attacker.combatant.type, defender.combatant.type);
  std::int32_t damage = folder::damage(rules, {.power = move.power,
                                               .attack = offense.offense,
                                               .modifierPercent = modifiers,
                                               .defense = defense.defense,
                                               .covered = covered});
  const DamageRules& classic = context.rules->damage;
  if (move.finisher && state.commanded(attack.attacker) &&
      state.finisher.charge > classic.finisherChargeDivisor) {
    damage = damage * state.finisher.charge / classic.finisherChargeDivisor;
  }
  const std::int32_t percent = state.isPlayerSide(attack.attacker)   ? classic.partnerDealtPercent
                               : state.isPlayerSide(attack.defender) ? classic.partnerTakenPercent
                                                                     : Hundred;
  if (percent != Hundred) {
    damage = std::max(damage * percent / Hundred, covered ? 0 : 1);
  }
  return std::min(damage, classic.max);
}

// The damage fills the defender's bar; full, its defense breaks (§2.9).
void fillDefenseBar(BattleContext& context, FighterId id, std::int32_t damage) {
  const FolderRules& rules = rulesOf(context);
  FolderFighter& own = folderFighter(context, id);
  own.restFrames = 0;
  const std::int32_t capacity = folder::barCapacity(rules, folderStats(context, id).defense);
  own.defenseBar = std::min(own.defenseBar + damage, capacity);
  if (own.broken() || own.defenseBar < capacity) {
    return;
  }
  const FighterStats& base = context.fighter(id).stats;
  own.brokenFrames = std::max(folder::breakFrames(rules, base.speed, base.defense), 1);
  own.brokenLength = own.brokenFrames;
  emitFor(context, EventKind::DefenseBroken, id, own.brokenFrames);
}

// A covered hit: the defender takes the reduced damage behind its guard, as a block shows. One that
// is performing its technique keeps it and only takes the damage.
void coverHit(BattleContext& context, const AttackInFlight& attack, std::int32_t damage) {
  const FighterId id = attack.defender;
  Fighter& fighter = context.fighter(id);
  if (registerBlockedAttack(context, attack) && context.particles != nullptr) {
    context.particles->spawn(context.particleRolls(), HitParticles::Spark::Block, BlockSparkKind,
                             sparkPointOf(context, attack), id, BlockSparkFrames);
  }
  addPendingDamage(context, fighter, damage);
  if (BattleCounters* counters = context.state->countersOf(id)) {
    ++counters->blocks;
  }
  const FinisherRules& finisher = context.rules->finisher;
  addFinisherProgress(context, id,
                      fighter.finisherGoal * finisher.perBlock / finisher.progressDivisor);
  if (!fighter.attack) {
    lookAt(fighter, hitPointOf(context, attack));
    fighter.blocking = true;
    fighter.pose = Pose::Blocking;
    fighter.poseFrames = context.rules->timing.blockAnimationFrames;
    fighter.invulnerableFrames = context.rules->hit.blockFrames;
  }
  fighter.struck = false;
  context.emit({.kind = EventKind::AttackBlocked,
                .fighter = id,
                .other = attack.attacker,
                .move = attack.move,
                .amount = damage,
                .status = StatusEffect::None,
                .command = Command::None});
}

void handleFolderAttack(BattleContext& context, const AttackInFlight& attack) {
  const FighterId id = attack.defender;
  Fighter& fighter = context.fighter(id);
  if (counteringAttacker(context, fighter, attack.attacker)) {
    fighter.struck = false;
    return;
  }
  if (context.moves->at(attack.move).buff) {
    applyBuff(context, id, attack.move);
    return;
  }
  const FolderFighter& own = folderFighter(context, id);
  const BattleState& state = *context.state;
  // §2.9: Guard always covers; any other stance covers with DEF's chance. A broken defense never
  // does.
  const bool guarding = state.commanded(id) && own.stance == Stance::Guard;
  const bool covered =
      !own.broken() &&
      (guarding || context.random->below(Hundred) <
                       folder::coverPercent(rulesOf(context), folderStats(context, id).defense));
  const std::int32_t damage = damageOf(context, attack, covered);
  if (!covered || !fighter.attack) {
    stopOwnTechnique(context, id);
    fighter.knockedBack = false;
    fighter.blocking = false;
    fighter.attack.reset();
    fighter.planned = false;
    cancelFinisherOf(context, id);
  }
  // §2.4, §11.3: a commanded fighter keeps the target its trainer gave it; an enemy may turn to
  // whoever hit it, as in the original.
  if (!state.commanded(id)) {
    selectTarget(context, id, attack.attacker, true);
  }
  fillDefenseBar(context, id, damage);
  if (covered) {
    coverHit(context, attack, damage);
  } else {
    landHit(context, attack, damage);
  }
}

// §2.9: the bar drains after a rest, and always during a break; the break counts down; VEL sets the
// walking speed (§6.4).
void tickDefense(BattleContext& context, FighterId id) {
  const FolderRules& rules = rulesOf(context);
  FolderFighter& own = folderFighter(context, id);
  Fighter& fighter = context.fighter(id);
  if (own.broken() && --own.brokenFrames == 0) {
    own.brokenLength = 0;
    emitFor(context, EventKind::DefenseRestored, id, 0);
  }
  const FighterStats now = folderStats(context, id);
  const std::int32_t capacity = folder::barCapacity(rules, now.defense);
  own.defenseBar = std::min(own.defenseBar, capacity);
  if (own.broken() || own.restFrames >= rules.barRestFrames) {
    const std::int32_t drain =
        folder::barDrain(rules, capacity, fighter.stats.speed, fighter.stats.defense);
    own.defenseBar = std::max(own.defenseBar - drain, 0);
  } else {
    ++own.restFrames;
  }
  fighter.moveScalePercent = folder::movementPercent(rules, now.speed);
}

class FolderBattle final : public BattleSystem {
public:
  [[nodiscard]] BattleSystemId id() const override { return BattleSystemId::Folder; }

  void start(BattleContext& context) override {
    BattleState& state = *context.state;
    state.folder.emplace();
    state.folder->fighters.resize(state.fighters.size());
    for (FighterId id = 0; id < state.fighters.size(); ++id) {
      Fighter& fighter = state.fighters[id];
      fighter.moveScalePercent = folder::movementPercent(rulesOf(context), fighter.stats.speed);
      if (state.commanded(id)) {
        fighter.planned = true;
        fighter.moveRange = -1;
        setTarget(context, id, nearestFoe(context, id));
        CommandState& commands = state.commandsOf(id);
        commands.menu = folderMenu(state, id);
        commands.current = stanceCommand(state.folder->fighters[id].stance);
      }
    }
  }

  void tickDecisions(BattleContext& context) override {
    BattleState& state = *context.state;
    for (FighterId id = 0; id < state.fighters.size(); ++id) {
      if (state.commanded(id) && !state.fighters[id].gone()) {
        tickFolderCommanded(context, id);
      }
    }
    tickEnemyDecisions(context);
  }

  void handleAttack(BattleContext& context, const AttackInFlight& attack) override {
    handleFolderAttack(context, attack);
  }

  void endFrame(BattleContext& context) override {
    for (FighterId id = 0; id < context.state->fighters.size(); ++id) {
      tickDefense(context, id);
    }
  }
};

} // namespace

std::string_view battleSystemName(BattleSystemId system) {
  return system == BattleSystemId::Folder ? FolderBattleName : ClassicBattleName;
}

std::optional<BattleSystemId> battleSystemOf(std::string_view name) {
  if (name == ClassicBattleName) {
    return BattleSystemId::Classic;
  }
  if (name == FolderBattleName) {
    return BattleSystemId::Folder;
  }
  return std::nullopt;
}

FolderFighter& folderFighter(const BattleContext& context, FighterId id) {
  return context.state->folder->fighters.at(id);
}

FighterStats folderStats(const BattleContext& context, FighterId id) {
  FighterStats stats = context.fighter(id).stats;
  if (folderFighter(context, id).broken()) {
    const FolderRules& rules = rulesOf(context);
    stats.offense = folder::brokenStat(rules, stats.offense);
    stats.defense = folder::brokenStat(rules, stats.defense);
    stats.speed = folder::brokenStat(rules, stats.speed);
  }
  return stats;
}

std::unique_ptr<BattleSystem> makeFolderBattle() {
  return std::make_unique<FolderBattle>();
}

} // namespace dwe::game::battle
