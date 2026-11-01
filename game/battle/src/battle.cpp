#include <dwe/game/battle/messages.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "aftermath.hpp"
#include "battle_context.hpp"

namespace dwe::game::battle {

namespace {

constexpr std::int32_t Hundred = 100;
// The longest real frame advance() takes into account: a longer one (a breakpoint, a window drag)
// runs as this.
constexpr double MaxFrameSeconds = 0.25;
// battle_setup.c: BTL_initializeCombat: BATTLE_FRAME_COUNT starts at 1.
constexpr std::int64_t FirstFrame = 1;

// Which side a fighter is made for: the partner's slots are fixed, an enemy's are not.
enum class Side : std::uint8_t { Partner, Enemy };

// Resolves the equipped moves. The partner's first three are techniques and its fourth a finisher;
// an enemy's NPC record may equip any of its species' moves in any slot (btl.c: loadBattleData
// loads each one), and a finisher there starts as a finisher wherever it sits.
core::Result<std::array<std::optional<MoveRef>, EquippedSlots>>
resolveMoves(const Combatant& combatant, const MoveCatalog& moves, Side side) {
  std::array<std::optional<MoveRef>, EquippedSlots> resolved{};
  for (std::size_t slot = 0; slot < EquippedSlots; ++slot) {
    const std::optional<std::string>& id = combatant.moves.at(slot);
    if (!id) {
      continue;
    }
    const std::optional<MoveRef> ref = moves.find(*id);
    if (!ref) {
      return core::fail(messages::formatMessage(messages::UnknownMove, *id));
    }
    const bool finisher = moves.at(*ref).finisher;
    if (side == Side::Enemy) {
      resolved.at(slot) = ref;
      continue;
    }
    if (slot == FinisherSlot && !finisher) {
      return core::fail(messages::formatMessage(messages::NotFinisherSlot, combatant.species, *id));
    }
    if (slot != FinisherSlot && finisher) {
      return core::fail(
          messages::formatMessage(messages::FinisherInTechniqueSlot, combatant.species, *id));
    }
    resolved.at(slot) = ref;
  }
  return resolved;
}

// battle_setup.c: BTL_initializeCombat: how many support moves a fighter may use, by its brain.
std::int32_t buffUses(const BuffUseRules& rules, std::int32_t brain) {
  std::int32_t uses = (brain / rules.perBrain) + rules.base;
  for (const BuffUseTier& tier : rules.tiers) {
    if (brain >= tier.brain) {
      uses = tier.uses;
    }
  }
  return uses;
}

core::Result<Fighter> makeFighter(const Combatant& combatant, const BattleRules& rules,
                                  const MoveCatalog& moves, Side side) {
  if (combatant.stats.maxHp < 1) {
    return core::fail(messages::formatMessage(messages::NoMaxHp, combatant.species));
  }
  auto resolved = resolveMoves(combatant, moves, side);
  if (!resolved) {
    return std::unexpected{resolved.error()};
  }
  const CombatStats& stats = combatant.stats;
  Fighter fighter;
  fighter.combatant = combatant;
  fighter.moves = *resolved;
  fighter.stats = {.maxHp = stats.maxHp,
                   .maxMp = stats.maxMp,
                   .offense = stats.offense,
                   .defense = stats.defense,
                   .speed = stats.speed,
                   .brain = stats.brain};
  fighter.startStats = stats;
  fighter.hp = std::min(combatant.hp, stats.maxHp);
  fighter.mp = std::min(combatant.mp, stats.maxMp);
  fighter.position = combatant.position;
  fighter.facing = {.x = 0, .z = FacingScale};
  fighter.chargeMode = combatant.chargeMode;
  fighter.speedReserve = rules.speedReserve.start;
  fighter.buffsLeft = buffUses(rules.moves.buffUses, stats.brain);
  fighter.buffPriority =
      (stats.brain / rules.partnerAi.buffPriorityBrainDivisor) + rules.partnerAi.buffPriorityBase;
  fighter.finisherGoal = rules.finisher.goalBase - stats.speed;
  return fighter;
}

// battle_setup.c: BTL_initializeCombat: a sick or injured partner fights with less of each stat but
// its HP and MP.
void applySicknessPenalty(Fighter& partner, const BattleRules& rules) {
  const std::int32_t percent = rules.condition.statPenaltyPercent;
  for (std::int32_t* stat : {&partner.stats.offense, &partner.stats.defense, &partner.stats.speed,
                             &partner.stats.brain}) {
    *stat -= *stat * percent / Hundred;
  }
}

Command commandOf(MenuEntry entry) {
  switch (entry) {
  case MenuEntry::Finisher:
    return Command::Finisher;
  case MenuEntry::Flee:
    return Command::Flee;
  case MenuEntry::YourCall:
    return Command::YourCall;
  case MenuEntry::Attack:
    return Command::Attack;
  case MenuEntry::Moderate:
    return Command::Moderate;
  case MenuEntry::Distance:
    return Command::Distance;
  case MenuEntry::Defend:
    return Command::Defend;
  case MenuEntry::Change:
    return Command::Change;
  case MenuEntry::Techniques:
    break;
  }
  return Command::None;
}

// battle_setup.c: BTL_initializeCombat: the menu by the partner's brain before any penalty.
std::vector<Command> buildMenu(const BattleRules& rules, const Fighter& partner, bool scripted) {
  const MenuTier* tier = &rules.menus.front();
  for (const MenuTier& candidate : rules.menus) {
    if (partner.startStats.brain >= candidate.brain) {
      tier = &candidate;
    }
  }
  std::vector<MenuEntry> entries = tier->entries;
  const bool hasTechniques = std::ranges::find(entries, MenuEntry::Techniques) != entries.end();
  if (hasTechniques && !rules.quirks.on(quirk::SmartMenuDropsAttackAndModerate)) {
    for (const MenuEntry missing : {MenuEntry::Moderate, MenuEntry::Attack}) {
      if (std::ranges::find(entries, missing) == entries.end()) {
        entries.insert(std::ranges::find(entries, MenuEntry::Techniques), missing);
      }
    }
  }
  std::vector<Command> menu;
  for (const MenuEntry entry : entries) {
    if (entry == MenuEntry::Techniques) {
      for (const Command technique :
           {Command::Technique3, Command::Technique2, Command::Technique1}) {
        const auto slot =
            static_cast<std::size_t>(technique) - static_cast<std::size_t>(Command::Technique1);
        if (partner.moves.at(slot)) {
          menu.push_back(technique);
        }
      }
    } else if (entry != MenuEntry::Flee || !scripted) {
      menu.push_back(commandOf(entry));
    }
  }
  return menu;
}

Point centerOf(const std::vector<Fighter>& fighters) {
  std::int64_t x = 0;
  std::int64_t z = 0;
  for (const Fighter& fighter : fighters) {
    x += fighter.position.x;
    z += fighter.position.z;
  }
  const auto count = static_cast<std::int64_t>(fighters.size());
  return {.x = static_cast<std::int32_t>(x / count), .z = static_cast<std::int32_t>(z / count)};
}

bool enemiesDown(const BattleState& state) {
  for (FighterId id = 1; id < state.fighters.size(); ++id) {
    if (state.fighters[id].pose != Pose::Fainted) {
      return false;
    }
  }
  return true;
}

} // namespace

Battle::Battle(const BattleRules& rules, const MoveCatalog& moves, BattleStrategies strategies,
               std::uint64_t seed)
    : rules_{&rules}, moves_{&moves}, strategies_{std::move(strategies)}, random_{seed},
      clock_{1.0 / FramesPerSecond, MaxFrameSeconds} {}

core::Result<Battle> Battle::start(BattleSetup setup, const BattleRules& rules,
                                   const MoveCatalog& moves, BattleStrategies strategies,
                                   std::uint64_t seed) {
  const auto enemies = static_cast<std::int32_t>(setup.enemies.size());
  if (enemies < 1 || enemies > rules.limits.maxEnemies) {
    return core::fail(
        messages::formatMessage(messages::NoEnemies, rules.limits.maxEnemies, enemies));
  }
  Battle battle{rules, moves, std::move(strategies), seed};
  BattleState& state = battle.state_;
  auto partner = makeFighter(setup.partner, rules, moves, Side::Partner);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  state.fighters.push_back(std::move(*partner));
  for (const Combatant& combatant : setup.enemies) {
    auto enemy = makeFighter(combatant, rules, moves, Side::Enemy);
    if (!enemy) {
      return std::unexpected{enemy.error()};
    }
    state.fighters.push_back(std::move(*enemy));
  }
  for (Fighter& each : state.fighters) {
    each.hpBarFrames = rules.hud.enemyHpBarFrames;
  }
  Fighter& fighter = state.partner();
  if (setup.condition.sick || setup.condition.injured) {
    applySicknessPenalty(fighter, rules);
  }
  state.commands.menu = buildMenu(rules, fighter, setup.scripted);
  state.commands.savedChargeMode = fighter.chargeMode;
  state.frame = FirstFrame;
  state.tamer = setup.tamer;
  state.scripted = setup.scripted;
  state.tamerless = setup.tamerless;
  state.arenaCenter = centerOf(state.fighters);
  if (setup.timeLimit) {
    // std_main.c: STD_initializeCombat keeps each side's HP as the match starts (STD_STARTING_HP).
    state.matchClock = MatchClock{.seconds = *setup.timeLimit,
                                  .frames = 1,
                                  .partnerStartHp = state.partner().hp,
                                  .enemyStartHp = state.fighters.at(1).hp};
  }
  lookAt(state.partner(), state.fighters.at(1).position);
  for (FighterId id = 1; id < state.fighters.size(); ++id) {
    lookAt(state.fighters[id], state.partner().position);
  }
  // btl.c: loadBattleData: everyone stands as the battle loads.
  startShownClips(state);
  battle.setup_ = std::move(setup);
  return battle;
}

core::Result<void> Battle::command(Command command) {
  if (result_) {
    return core::fail(std::string{messages::BattleOver});
  }
  if (state_.tamerless) {
    return core::fail(std::string{messages::NoCommands});
  }
  if (state_.bagOpen) {
    return core::fail(std::string{messages::BagOpen});
  }
  if (std::ranges::find(state_.commands.menu, command) == state_.commands.menu.end()) {
    return core::fail(std::string{messages::CommandNotInMenu});
  }
  if (state_.livingEnemies() == 0) {
    return {};
  }
  CommandState& commands = state_.commands;
  const Fighter& partner = state_.partner();
  switch (command) {
  case Command::Finisher:
    if (!partner.hasFinisher() || partner.finisherProgress < partner.finisherGoal) {
      return core::fail(std::string{messages::FinisherNotReady});
    }
    commands.buffered = command;
    commands.current = command;
    pending().push_back({.kind = EventKind::CommandGiven,
                         .fighter = PartnerId,
                         .other = std::nullopt,
                         .move = std::nullopt,
                         .amount = 0,
                         .status = StatusEffect::None,
                         .command = command});
    return {};
  case Command::Flee:
    commands.buffered = command;
    commands.current = command;
    if (!state_.fleeFrames) {
      state_.fleeFrames = 0;
      pending().push_back({.kind = EventKind::FleeStarted,
                           .fighter = PartnerId,
                           .other = std::nullopt,
                           .move = std::nullopt,
                           .amount = 0,
                           .status = StatusEffect::None,
                           .command = Command::None});
    }
    return {};
  case Command::Change:
    commands.changeTarget = true;
    break;
  default:
    break;
  }
  commands.buffered = command;
  pending();
  BattleContext context{.rules = rules_,
                        .moves = moves_,
                        .state = &state_,
                        .strategies = &strategies_,
                        .random = &random_,
                        .events = &events_,
                        .condition = &setup_.condition,
                        .ground = ground_,
                        .flights = flights_,
                        .particles = particleRolls_ ? &*particleRolls_ : nullptr,
                        .visuals = &statusVisuals_};
  commands.delay = strategies_.obedience->commandDelay(context.view(), setup_.condition.discipline);
  return {};
}

void Battle::defeatEnemies() {
  for (std::size_t enemy = 1; enemy < state_.fighters.size(); ++enemy) {
    Fighter& fighter = state_.fighters.at(enemy);
    fighter.pendingDamage = std::max(fighter.pendingDamage, fighter.hp);
  }
}

void Battle::defeatPartner() {
  if (state_.fighters.empty()) {
    return;
  }
  Fighter& partner = state_.fighters.front();
  partner.pendingDamage = std::max(partner.pendingDamage, partner.hp);
}

void Battle::pressFinisherCharge() {
  chargePressed_ = true;
}

void Battle::setClips(FighterId fighter, FighterClips clips) {
  state_.fighters.at(fighter).clips = std::make_shared<const FighterClips>(std::move(clips));
}

bool Battle::bagAvailable() const {
  return !result_ && !state_.tamerless && !state_.fleeFrames && !state_.thrownItem &&
         !state_.matchClock;
}

core::Result<void> Battle::openBag() {
  if (!bagAvailable()) {
    return core::fail(std::string{messages::BagUnavailable});
  }
  state_.bagOpen = true;
  return {};
}

void Battle::closeBag() {
  state_.bagOpen = false;
}

core::Result<void> Battle::throwItem(ThrownItem item) {
  if (!bagAvailable()) {
    return core::fail(std::string{messages::BagUnavailable});
  }
  BattleContext context = contextOf(pending());
  battle::throwItem(context, item);
  return {};
}

bool Battle::hpBarShown(FighterId fighter) const {
  return fighter == PartnerId ||
         state_.fighters.at(fighter).hpBarFrames < rules_->hud.enemyHpBarFrames;
}

void Battle::restorePartner(std::int32_t hp, std::int32_t mp) {
  battle::restorePartner(state_.partner(), hp, mp);
}

void Battle::curePartnerStatus() {
  BattleContext context = contextOf(pending());
  cureStatus(context, PartnerId);
}

void Battle::protectPartner() {
  state_.partner().status.protectedFromStatus = true;
}

core::Result<void> Battle::revivePartner(std::int32_t hp) {
  const Fighter& partner = state_.partner();
  if (result_ || !partner.fainted || state_.deathFrames >= rules_->ending.reviveFrames) {
    return core::fail(std::string{messages::NotRevivable});
  }
  BattleContext context = contextOf(pending());
  battle::revivePartner(context, hp);
  return {};
}

BattleContext Battle::contextOf(BattleEvents& events) {
  return {.rules = rules_,
          .moves = moves_,
          .state = &state_,
          .strategies = &strategies_,
          .random = &random_,
          .events = &events,
          .condition = &setup_.condition,
          .ground = ground_,
          .flights = flights_,
          .particles = particleRolls_ ? &*particleRolls_ : nullptr,
          .visuals = &statusVisuals_};
}

BattleEvents& Battle::pending() {
  if (delivered_) {
    events_.clear();
    delivered_ = false;
  }
  return events_;
}

const BattleEvents& Battle::step() {
  pending();
  delivered_ = true;
  if (result_) {
    events_.clear();
    return events_;
  }
  BattleContext context{.rules = rules_,
                        .moves = moves_,
                        .state = &state_,
                        .strategies = &strategies_,
                        .random = &random_,
                        .events = &events_,
                        .condition = &setup_.condition,
                        .ground = ground_,
                        .flights = flights_,
                        .particles = particleRolls_ ? &*particleRolls_ : nullptr,
                        .visuals = &statusVisuals_};
  if (state_.matchClock && endMatch()) {
    return events_;
  }
  // battle_setup.c: BTL_isBattleFinished, before anything else of the frame.
  const Fighter& partner = state_.partner();
  if (!state_.matchClock && state_.fleeFrames && *state_.fleeFrames >= rules_->ending.fleeFrames &&
      !partner.defeated()) {
    finish(Outcome::Fled);
    return events_;
  }
  // The countdown waits while a thrown item is on its way (TAMER_ITEM.worldItem.type != 0xff).
  if (!state_.matchClock && partner.fainted && partner.pose == Pose::Fainted &&
      !state_.thrownItem && ++state_.deathFrames >= rules_->ending.reviveFrames) {
    finish(Outcome::Defeat);
    return events_;
  }
  if (!state_.matchClock && !partner.fainted && enemiesDown(state_)) {
    finish(Outcome::Victory);
    return events_;
  }
  tickPartnerAi(context);
  tickEnemyAi(context);
  tickFighterStates(context);
  tickThrownItem(context);
  // command_menu.c: BTL_tickFinisherChargeup.
  FinisherState& finisher = state_.finisher;
  if (finisher.charging && chargePressed_ && finisher.charge < rules_->finisher.chargeCap) {
    finisher.charge =
        std::min(finisher.charge + rules_->finisher.chargePerPress, rules_->finisher.chargeCap);
    events_.push_back({.kind = EventKind::FinisherCharged,
                       .fighter = PartnerId,
                       .other = std::nullopt,
                       .move = std::nullopt,
                       .amount = finisher.charge,
                       .status = StatusEffect::None,
                       .command = Command::None});
  }
  chargePressed_ = false;
  drainPendingDamage(context);
  tickHpBars(context);
  updateShownClips(context);
  tickRootMotion(context);
  faceEffectTargets(context);
  // BTL_battleTickFrame: tickObjects, after the fighters: the sparks' draws of the frame.
  if (particleRolls_) {
    particleRolls_->tick(random_);
  }
  // The status visuals' world objects, with the bubbles' rolls.
  statusVisuals_.tick(particleRolls_ ? random_ : statusVisuals_.ownRolls());
  if (state_.fleeFrames) {
    ++*state_.fleeFrames;
  }
  // std_main.c: STD_tickFighterCounter, an object of the frame's end: a second goes every 20
  // counted frames while neither side is down.
  if (MatchClock* clock = state_.matchClock ? &*state_.matchClock : nullptr) {
    ++clock->frames;
    const bool anyoneDown = std::ranges::any_of(state_.fighters, &Fighter::fainted);
    if (!anyoneDown && clock->frames % FramesPerSecond == 0 && clock->seconds > 0) {
      --clock->seconds;
    }
  }
  ++state_.frame;
  return events_;
}

// std_main.c: STD_checkEndCondition. A side is down once its faint has played (anim 0x2B, done).
bool Battle::endMatch() {
  const Fighter& partner = state_.partner();
  if (partner.pendingDamage != 0 || state_.fighters.at(1).pendingDamage != 0) {
    return false;
  }
  const bool partnerDown = partner.fainted && partner.pose == Pose::Fainted;
  if (partnerDown) {
    if (state_.livingEnemies() > 0) {
      finish(Outcome::Defeat);
      return true;
    }
    if (!enemiesDown(state_)) {
      return false;
    }
    finish(Outcome::Defeat, true);
    return true;
  }
  if (enemiesDown(state_)) {
    if (!partner.defeated()) {
      finish(Outcome::Victory);
      return true;
    }
    return false;
  }
  MatchClock& clock = *state_.matchClock;
  if (clock.seconds != 0 || partner.defeated() || state_.livingEnemies() == 0) {
    return false;
  }
  // STD_DAMAGE: the HP each side lost since the match started.
  const std::int32_t partnerDamage = clock.partnerStartHp - partner.hp;
  const std::int32_t enemyDamage = clock.enemyStartHp - state_.fighters.at(1).hp;
  if (partnerDamage == enemyDamage) {
    finish(Outcome::Defeat, true);
  } else {
    finish(partnerDamage < enemyDamage ? Outcome::Victory : Outcome::Defeat);
  }
  return true;
}

BattleEvents Battle::advance(double seconds) {
  return advance(seconds, [](const BattleEvents& /*frame*/) {});
}

BattleEvents Battle::advance(double seconds,
                             const std::function<void(const BattleEvents&)>& onFrame) {
  BattleEvents all;
  const int ticks = clock_.advance(seconds);
  for (int tick = 0; tick < ticks && !finished(); ++tick) {
    const BattleEvents& frame = step();
    onFrame(frame);
    all.insert(all.end(), frame.begin(), frame.end());
  }
  return all;
}

void Battle::finish(Outcome outcome, bool draw) {
  // BTL_deinitializeCombat: BTL_removeStatusEffects for every fighter.
  statusVisuals_.clear();
  result_ = settleBattle(outcome, setup_, state_, *rules_, *moves_, random_);
  if (state_.matchClock) {
    result_->timeUp = state_.matchClock->seconds == 0;
    result_->draw = draw;
  }
  events_.push_back({.kind = EventKind::BattleEnded,
                     .fighter = PartnerId,
                     .other = std::nullopt,
                     .move = std::nullopt,
                     .amount = 0,
                     .status = StatusEffect::None,
                     .command = Command::None});
}

} // namespace dwe::game::battle
