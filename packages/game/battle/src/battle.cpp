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
#include "usability.hpp"

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
    const auto variant = combatant.moveVariants.find(*id);
    const std::optional<MoveRef> ref =
        variant != combatant.moveVariants.end() ? std::optional{variant->second} : moves.find(*id);
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

// battle_setup.c: BTL_initializeCombat: the menu by the partner's brain before any penalty. A
// versus side's comes from the versus menus (vs_combat.c: VS__combatInit), without flee or change.
std::vector<Command> buildMenu(const BattleRules& rules, const Fighter& partner, bool scripted,
                               bool versus) {
  const std::vector<MenuTier>& tiers =
      versus && !rules.versusMenus.empty() ? rules.versusMenus : rules.menus;
  const MenuTier* tier = &tiers.front();
  for (const MenuTier& candidate : tiers) {
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
    } else if (versus && (entry == MenuEntry::Flee || entry == MenuEntry::Change)) {
      continue;
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
  for (FighterId id = 1; id <= state.enemyCount(); ++id) {
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
  const bool versus = setup.versus.has_value();
  if ((versus || setup.kind == BattleKind::Versus) &&
      (!versus || setup.kind != BattleKind::Versus || enemies != 1 || !setup.timeLimit)) {
    return core::fail(std::string{messages::VersusShape});
  }
  Battle battle{rules, moves, std::move(strategies), seed};
  BattleState& state = battle.state_;
  auto partner = makeFighter(setup.partner, rules, moves, Side::Partner);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  state.fighters.push_back(std::move(*partner));
  for (const Combatant& combatant : setup.enemies) {
    // vs_combat.c: VS_loadFighterEntities: a versus rival equips as a partner does.
    auto enemy = makeFighter(combatant, rules, moves, versus ? Side::Partner : Side::Enemy);
    if (!enemy) {
      return std::unexpected{enemy.error()};
    }
    state.fighters.push_back(std::move(*enemy));
  }
  state.enemies = setup.enemies.size();
  // docs/architecture/coop-combat.md §4.1: the other players' partners, after the enemies.
  if (!setup.allies.empty() && (versus || setup.timeLimit)) {
    return core::fail(std::string{messages::VersusShape});
  }
  for (const AllySetup& ally : setup.allies) {
    auto fighter = makeFighter(ally.partner, rules, moves, Side::Partner);
    if (!fighter) {
      return std::unexpected{fighter.error()};
    }
    if (ally.condition.sick || ally.condition.injured) {
      applySicknessPenalty(*fighter, rules);
    }
    AllyState allyState{.id = state.fighters.size(),
                        .owner = ally.owner,
                        .commands = {},
                        .condition = ally.condition,
                        .counters = {},
                        .tamer = ally.tamer,
                        .thrownItem = std::nullopt,
                        .fleeFrames = std::nullopt,
                        .deathFrames = 0,
                        .released = false};
    allyState.commands.menu = buildMenu(rules, *fighter, setup.scripted, false);
    allyState.commands.savedChargeMode = fighter->chargeMode;
    state.fighters.push_back(std::move(*fighter));
    state.allies.push_back(std::move(allyState));
  }
  std::vector<std::int32_t> nodeCounts;
  for (Fighter& each : state.fighters) {
    each.hpBarFrames = rules.hud.enemyHpBarFrames;
    nodeCounts.push_back(each.combatant.boneCount);
  }
  battle.hitParticles_.setNodeCounts(std::move(nodeCounts));
  Fighter& fighter = state.partner();
  if (setup.condition.sick || setup.condition.injured) {
    applySicknessPenalty(fighter, rules);
  }
  state.commands.menu = buildMenu(rules, fighter, setup.scripted, versus);
  state.commands.savedChargeMode = fighter.chargeMode;
  if (versus) {
    const Fighter& rival = state.fighters.at(RivalId);
    state.rival = VersusRival{.commands = {}, .discipline = setup.versus->rivalDiscipline};
    state.rival->commands.menu = buildMenu(rules, rival, false, true);
    state.rival->commands.savedChargeMode = rival.chargeMode;
  }
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
  for (FighterId id = 1; id <= state.enemyCount(); ++id) {
    lookAt(state.fighters[id], state.partner().position);
  }
  // docs/architecture/coop-combat.md §4.2: the touched enemy goes for the starter's partner, the
  // others for the partner nearest to them.
  for (const AllyState& ally : state.allies) {
    lookAt(state.fighters[ally.id], state.fighters.at(1).position);
  }
  if (!state.allies.empty()) {
    state.fighters.at(1).target = PartnerId;
    for (FighterId id = 2; id <= state.enemyCount(); ++id) {
      FighterId nearest = PartnerId;
      for (const AllyState& ally : state.allies) {
        if (distanceSquared(state.fighters[id].position, state.fighters[ally.id].position) <
            distanceSquared(state.fighters[id].position, state.fighters[nearest].position)) {
          nearest = ally.id;
        }
      }
      state.fighters[id].target = nearest;
    }
  }
  // btl.c: loadBattleData: everyone stands as the battle loads.
  startShownClips(state);
  battle.setup_ = std::move(setup);
  return battle;
}

core::Result<void> Battle::command(Command command) {
  return this->command(command, PartnerId);
}

core::Result<void> Battle::command(Command command, FighterId side) {
  if (result_) {
    return core::fail(std::string{messages::BattleOver});
  }
  if (!state_.commanded(side)) {
    return core::fail(std::string{messages::NotCommanded});
  }
  if (state_.tamerless) {
    return core::fail(std::string{messages::NoCommands});
  }
  const AllyState* ally = state_.allyOf(side);
  if (ally != nullptr && ally->released) {
    return core::fail(std::string{messages::NotCommanded});
  }
  // In co-op a player's bag holds only their own partner (docs/architecture/multiplayer.md §6).
  if (ally != nullptr ? ally->bagOpen : state_.bagOpen) {
    return core::fail(std::string{messages::BagOpen});
  }
  // battle_tick.c: handleCommands: once the partner is called back it only runs (currentCommand 1
  // returns before the buttons are read), the finisher's button included.
  if (state_.fleeFramesOf(side) || state_.fighters.at(side).gone()) {
    return core::fail(std::string{messages::Fleeing});
  }
  CommandState& commands = state_.commandsOf(side);
  if (std::ranges::find(commands.menu, command) == commands.menu.end()) {
    return core::fail(std::string{messages::CommandNotInMenu});
  }
  if (state_.remainingOthers(side).empty()) {
    return {};
  }
  const Fighter& partner = state_.fighters.at(side);
  switch (command) {
  case Command::Finisher:
    if (!partner.hasFinisher() || partner.finisherProgress < partner.finisherGoal) {
      return core::fail(std::string{messages::FinisherNotReady});
    }
    commands.buffered = command;
    commands.current = command;
    pending().push_back({.kind = EventKind::CommandGiven,
                         .fighter = side,
                         .other = std::nullopt,
                         .move = std::nullopt,
                         .amount = 0,
                         .status = StatusEffect::None,
                         .command = command});
    return {};
  case Command::Flee:
    commands.buffered = command;
    commands.current = command;
    state_.fleeFramesOf(side) = 0;
    pending().push_back({.kind = EventKind::FleeStarted,
                         .fighter = side,
                         .other = std::nullopt,
                         .move = std::nullopt,
                         .amount = 0,
                         .status = StatusEffect::None,
                         .command = Command::None});
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
                        .particles = &hitParticles_,
                        .originalRolls = originalRolls_,
                        .visuals = &statusVisuals_};
  // vs_combat.c: VS___tickVSInput works the delay out and then sets it to 0.
  commands.delay =
      state_.versus()
          ? 0
          : strategies_.obedience->commandDelay(context.view(side), context.disciplineOf(side));
  return {};
}

void Battle::defeatEnemies() {
  BattleContext context = contextOf(pending());
  for (FighterId enemy = 1; enemy <= state_.enemyCount(); ++enemy) {
    strikeDown(context, enemy);
  }
}

void Battle::defeatPartner() {
  if (state_.fighters.empty()) {
    return;
  }
  Fighter& partner = state_.fighters.front();
  partner.pendingDamage = std::max(partner.pendingDamage, partner.hp);
}

void Battle::runOutClock(std::optional<bool> partnerWins) {
  if (!state_.matchClock || state_.fighters.size() < 2) {
    return;
  }
  // Wide enough that a hit still landing before the judgement does not turn it round.
  constexpr std::int32_t Margin = 200;
  Fighter& partner = state_.fighters.at(0);
  Fighter& enemy = state_.fighters.at(1);
  const std::int32_t partnerLost = state_.matchClock->partnerStartHp - partner.hp;
  const std::int32_t enemyLost = state_.matchClock->enemyStartHp - enemy.hp;
  const auto lose = [](Fighter& fighter, std::int32_t hp) {
    fighter.hp = std::max(1, fighter.hp - std::max(0, hp));
  };
  if (!partnerWins) {
    lose(partnerLost < enemyLost ? partner : enemy, std::abs(partnerLost - enemyLost));
  } else if (*partnerWins) {
    lose(enemy, partnerLost - enemyLost + Margin);
  } else {
    lose(partner, enemyLost - partnerLost + Margin);
  }
  state_.matchClock->seconds = 0;
}

void Battle::pressFinisherCharge() {
  chargePressed_ = true;
}

void Battle::cheerFinisher() {
  cheered_ = true;
}

bool Battle::bagAvailableFor(FighterId thrower) const {
  const AllyState* ally = state_.allyOf(thrower);
  if (ally == nullptr) {
    return bagAvailable();
  }
  return !result_ && !state_.tamerless && !ally->fleeFrames && !ally->thrownItem &&
         !state_.fighters.at(thrower).gone();
}

core::Result<void> Battle::throwItem(ThrownItem item, FighterId thrower, FighterId target) {
  if (!bagAvailableFor(thrower)) {
    return core::fail(std::string{messages::BagUnavailable});
  }
  if (target >= state_.fighters.size() || !state_.isPlayerSide(target) ||
      state_.fighters[target].gone()) {
    return core::fail(std::string{messages::BagUnavailable});
  }
  BattleContext context = contextOf(pending());
  battle::throwItem(context, item, thrower, target);
  return {};
}

void Battle::releaseAlly(FighterId ally) {
  if (AllyState* state = state_.allyOf(ally)) {
    state->released = true;
    state->commands.buffered = Command::YourCall;
    if (state->commands.current != Command::Flee) {
      state->commands.current = Command::YourCall;
    }
  }
}

std::int32_t Battle::deathFramesOf(FighterId id) const {
  const AllyState* ally = state_.allyOf(id);
  return ally != nullptr ? ally->deathFrames : state_.deathFrames;
}

void Battle::setClips(FighterId fighter, FighterClips clips) {
  state_.fighters.at(fighter).clips = std::make_shared<const FighterClips>(std::move(clips));
}

std::optional<std::int32_t> Battle::partnerMoveCost(std::size_t slot) const {
  return partnerMoveCost(slot, PartnerId);
}

std::optional<std::int32_t> Battle::partnerMoveCost(std::size_t slot, FighterId id) const {
  const Fighter& fighter = state_.fighters.at(id);
  const std::optional<MoveRef>& ref = fighter.moves.at(slot);
  if (!ref) {
    return std::nullopt;
  }
  const AllyState* ally = state_.allyOf(id);
  const PartnerCondition& condition = ally != nullptr ? ally->condition : setup_.condition;
  const bool unwell = condition.sick || condition.injured;
  return partnerMpCost(*rules_, moves_->at(*ref).mpCost, fighter.stats.brain, unwell,
                       MpCostUse::Payment);
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

bool Battle::bagOpen(FighterId thrower) const {
  const AllyState* ally = state_.allyOf(thrower);
  return ally != nullptr ? ally->bagOpen : state_.bagOpen;
}

core::Result<void> Battle::openBag(FighterId thrower) {
  AllyState* ally = state_.allyOf(thrower);
  if (ally == nullptr) {
    return openBag();
  }
  if (!bagAvailableFor(thrower)) {
    return core::fail(std::string{messages::BagUnavailable});
  }
  ally->bagOpen = true;
  return {};
}

void Battle::closeBag(FighterId thrower) {
  if (AllyState* ally = state_.allyOf(thrower)) {
    ally->bagOpen = false;
  } else {
    closeBag();
  }
}

core::Result<void> Battle::throwItem(ThrownItem item) {
  if (!bagAvailable()) {
    return core::fail(std::string{messages::BagUnavailable});
  }
  BattleContext context = contextOf(pending());
  battle::throwItem(context, item);
  return {};
}

std::optional<std::int32_t> Battle::thrownItemFlightFrame() const {
  return thrownItemFlightFrame(PartnerId);
}

const std::optional<ThrownItemState>& Battle::thrownItemOf(FighterId thrower) const {
  const AllyState* ally = state_.allyOf(thrower);
  return ally != nullptr ? ally->thrownItem : state_.thrownItem;
}

std::optional<std::int32_t> Battle::thrownItemFlightFrame(FighterId thrower) const {
  const std::optional<ThrownItemState>& item = thrownItemOf(thrower);
  if (!item) {
    return std::nullopt;
  }
  return flightFrameOf(*item, rules_->itemThrow);
}

bool Battle::hpBarShown(FighterId fighter) const {
  return state_.isPlayerSide(fighter) ||
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
          .particles = &hitParticles_,
          .originalRolls = originalRolls_,
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
                        .particles = &hitParticles_,
                        .originalRolls = originalRolls_,
                        .visuals = &statusVisuals_};
  if (state_.matchClock && endMatch()) {
    return events_;
  }
  if (!state_.allies.empty() && endCoop()) {
    return events_;
  }
  // battle_setup.c: BTL_isBattleFinished, before anything else of the frame.
  const Fighter& partner = state_.partner();
  if (!state_.matchClock && state_.allies.empty() && state_.fleeFrames &&
      *state_.fleeFrames >= rules_->ending.fleeFrames && !partner.defeated()) {
    finish(Outcome::Fled);
    return events_;
  }
  // The countdown waits while a thrown item is on its way (TAMER_ITEM.worldItem.type != 0xff).
  if (!state_.matchClock && state_.allies.empty() && partner.fainted &&
      partner.pose == Pose::Fainted && !state_.thrownItem &&
      ++state_.deathFrames >= rules_->ending.reviveFrames) {
    finish(Outcome::Defeat);
    return events_;
  }
  // The win also needs the partner to outlive the damage still on its way: a lethal hit that lands
  // with the last enemy's fall drains the partner to 0 HP, it faints and the countdown follows.
  if (!state_.matchClock && state_.allies.empty() && !partner.fainted && !partner.defeated() &&
      enemiesDown(state_)) {
    finish(Outcome::Victory);
    return events_;
  }
  tickPartnerAi(context);
  tickEnemyAi(context);
  tickFighterStates(context);
  tickThrownItem(context);
  // command_menu.c: BTL_tickFinisherChargeup.
  FinisherState& finisher = state_.finisher;
  // A cheer (co-op) adds half of a press, at least 1.
  const std::int32_t pressed = chargePressed_ ? rules_->finisher.chargePerPress
                               : cheered_     ? std::max(rules_->finisher.chargePerPress / 2, 1)
                                              : 0;
  if (finisher.charging && pressed > 0 && finisher.charge < rules_->finisher.chargeCap) {
    finisher.charge = std::min(finisher.charge + pressed, rules_->finisher.chargeCap);
    events_.push_back({.kind = EventKind::FinisherCharged,
                       .fighter = finisher.fighter.value_or(PartnerId),
                       .other = std::nullopt,
                       .move = std::nullopt,
                       .amount = finisher.charge,
                       .status = StatusEffect::None,
                       .command = Command::None});
  }
  chargePressed_ = false;
  cheered_ = false;
  drainPendingDamage(context);
  tickHpBars(context);
  updateShownClips(context);
  tickRootMotion(context);
  faceEffectTargets(context);
  // BTL_battleTickFrame: tickObjects, after the fighters: the sparks' draws of the frame.
  hitParticles_.tick(particleRolls(), state_.fighters);
  // The status visuals' world objects, with the bubbles' rolls.
  statusVisuals_.tick(originalRolls_ ? random_ : statusVisuals_.ownRolls(), state_.fighters);
  if (state_.fleeFrames) {
    ++*state_.fleeFrames;
  }
  for (AllyState& ally : state_.allies) {
    if (ally.fleeFrames) {
      ++*ally.fleeFrames;
    }
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
    const bool won = partnerDamage < enemyDamage;
    finish(won ? Outcome::Victory : Outcome::Defeat);
    // STD_tickBattleResultScreen: the winner waits in its stance, the loser falls.
    const FighterId loser = won ? FighterId{1} : PartnerId;
    faintAtTimeUp(state_.fighters.at(loser), loser);
  }
  return true;
}

bool Battle::endCoop() {
  const EndingRules& ending = rules_->ending;
  for (const FighterId id : state_.partnersIn()) {
    Fighter& fighter = state_.fighters[id];
    std::optional<std::int32_t>& flee = state_.fleeFramesOf(id);
    AllyState* ally = state_.allyOf(id);
    std::int32_t& countdown = ally != nullptr ? ally->deathFrames : state_.deathFrames;
    if (flee && *flee >= ending.fleeFrames && !fighter.defeated()) {
      fighter.departure = Departure::Fled;
    } else if (fighter.fainted && fighter.pose == Pose::Fainted && !itemFlyingTo(state_, id) &&
               ++countdown >= ending.reviveFrames) {
      fighter.departure = Departure::KnockedOut;
    } else {
      continue;
    }
    fighter.planned = true;
    fighter.target.reset();
    events_.push_back({.kind = EventKind::PartnerLeft,
                       .fighter = id,
                       .other = std::nullopt,
                       .move = std::nullopt,
                       .amount = 0,
                       .status = StatusEffect::None,
                       .command = Command::None});
  }
  const std::vector<FighterId> in = state_.partnersIn();
  if (in.empty()) {
    const bool anyFled = std::ranges::any_of(state_.fighters, [](const Fighter& fighter) {
      return fighter.departure == Departure::Fled;
    });
    finish(anyFled ? Outcome::Fled : Outcome::Defeat);
    return true;
  }
  const bool standing = std::ranges::any_of(in, [this](FighterId id) {
    const Fighter& fighter = state_.fighters[id];
    return !fighter.fainted && !fighter.defeated();
  });
  if (standing && enemiesDown(state_)) {
    // A partner still down when the team wins counts as knocked out (it did not get up).
    for (const FighterId id : in) {
      if (state_.fighters[id].fainted) {
        state_.fighters[id].departure = Departure::KnockedOut;
      }
    }
    finish(Outcome::Victory);
    return true;
  }
  return false;
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
  // battle_main.c: BTL_handleVictorySequence ticks the battle frames (41 at least) before the end
  // box, so the partner's HP bar drains what damage is still on its way (measured: 300 pending at
  // the win, 727 HP to 427). The win checked that the partner outlives it.
  if (outcome == Outcome::Victory) {
    Fighter& partner = state_.partner();
    partner.hp = std::max(partner.hp - partner.pendingDamage, 0);
    partner.pendingDamage = 0;
    for (const AllyState& ally : state_.allies) {
      Fighter& fighter = state_.fighters[ally.id];
      fighter.hp = std::max(fighter.hp - fighter.pendingDamage, 0);
      fighter.pendingDamage = 0;
    }
  }
  result_ = settleBattle(outcome, setup_, state_, *rules_, *moves_, random_);
  for (const AllyState& ally : state_.allies) {
    allyResults_.push_back(
        {.owner = ally.owner,
         .result = settleAlly(outcome, setup_, state_, ally, *rules_, *moves_, random_)});
  }
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
