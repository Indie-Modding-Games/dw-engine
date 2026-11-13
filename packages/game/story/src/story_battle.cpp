#include <dwe/core/log.hpp>
#include <dwe/game/story/story_battle.hpp>

#include <algorithm>
#include <variant>

#include "routine_messages.hpp"

namespace dwe::game::story {

// Waits for the battle's outcome, then for the partner's walk back after a victory.
class StoryBattles::BattleTask final : public StoryTask {
public:
  explicit BattleTask(StoryBattles& battles) : battles_{&battles} {}

  TaskStep tick() override {
    if (walkingBack_) {
      if (!battles_->battle_->partnerReturned()) {
        return TaskRunning{};
      }
      return TaskDone{};
    }
    const std::optional<BattleOutcome> outcome = battles_->battle_->battleOutcome();
    if (!outcome) {
      return TaskRunning{};
    }
    TaskStep next = battles_->afterBattle(*outcome);
    if (*outcome == BattleOutcome::Victory && std::holds_alternative<TaskRunning>(next)) {
      walkingBack_ = true;
    }
    return next;
  }

private:
  StoryBattles* battles_;
  bool walkingBack_ = false;
};

StoryBattles::StoryBattles(StoryStatePort& state, StoryBattlePort& battle, ScreenHistory& screens,
                           StoryBattleRules rules)
    : state_{&state}, battle_{&battle}, screens_{&screens}, rules_{rules} {}

core::Result<std::shared_ptr<StoryTask>> StoryBattles::startBattle() {
  counters_.started = std::min(counters_.started + 1, rules_.counterMax);
  auto battle = enemies();
  if (!battle) {
    return std::unexpected{battle.error()};
  }
  core::logDebug("{}", messages::formatMessage(messages::PortStartBattle, counters_.started,
                                               !battle->enemies.empty()));
  battle_->startBattle(*battle);
  return std::make_shared<BattleTask>(*this);
}

core::Result<ScriptedBattle> StoryBattles::enemies() {
  ScriptedBattle battle{.enemies = {}, .fixedFight = fixedFight(), .joinable = false};
  const auto predefined = state_->variable(rules_.predefinedVariable);
  if (!predefined) {
    return std::unexpected{predefined.error()};
  }
  if (*predefined == 0) {
    fightTalkedTo(battle);
    return battle;
  }
  // script_interp.c: scriptInstruction64to7E turns each script id into its entity in place, and
  // btl.c: handleBattleStart reads them back.
  for (std::int64_t slot = 0; slot < rules_.enemyCount; ++slot) {
    const std::int64_t variable = rules_.firstEnemyVariable + slot;
    const auto scriptId = state_->variable(variable);
    if (!scriptId) {
      return std::unexpected{scriptId.error()};
    }
    if (*scriptId == rules_.noEnemy) {
      continue;
    }
    const std::int64_t entity = battle_->entityOf(*scriptId).value_or(rules_.noEnemy);
    core::logDebug("{}",
                   messages::formatMessage(messages::PortBattleEnemy, variable, *scriptId, entity));
    if (auto written = state_->setVariable(variable, entity); !written) {
      return std::unexpected{written.error()};
    }
    if (entity != rules_.noEnemy) {
      battle.enemies.push_back(entity);
    }
  }
  if (battle.enemies.empty()) {
    // A recovery of dwe's own: every script that asks for a predefined battle names enemies on its
    // map just before start_battle, and puts the variable back to 0 after it. One whose enemies are
    // all missing was left behind by a fight whose section never ended (a flight from a fixed
    // fight, which dwe allowed before, or the jump of a defeat), and would leave the battle with
    // no enemy: the touching NPC's section would run again forever. The battle this section asks
    // for is the one with the NPC talked to.
    core::logWarning("{}", messages::formatMessage(messages::PortBattleStalePredefined,
                                                   rules_.predefinedVariable));
    if (auto cleared = state_->setVariable(rules_.predefinedVariable, 0); !cleared) {
      return std::unexpected{cleared.error()};
    }
    fightTalkedTo(battle);
  }
  return battle;
}

void StoryBattles::fightTalkedTo(ScriptedBattle& battle) const {
  // btl.c: handleBattleStart: without a predefined battle the enemy is the NPC the player talked
  // to; the other NPCs on screen may join it by the flee table's roll, which the battle screen
  // makes (it knows who is on screen).
  battle.joinable = true;
  if (const auto talked = battle_->talkedTo()) {
    const auto entity = battle_->talkedEntity().or_else([&] { return battle_->entityOf(*talked); });
    if (entity) {
      core::logDebug("{}", messages::formatMessage(messages::PortBattleTalkedTo, *talked, *entity));
      battle.enemies.push_back(*entity);
    }
  }
}

bool StoryBattles::fixedFight() const {
  return state_->trigger(rules_.fixedFightTrigger).value_or(false);
}

TaskStep StoryBattles::afterBattle(BattleOutcome outcome) {
  // writePStat keeps the low byte: a defeat's -1 is 255.
  constexpr std::int64_t ByteMask = 0xFF;
  const std::int64_t value = static_cast<std::int64_t>(outcome) & ByteMask;
  core::logDebug(
      "{}", messages::formatMessage(messages::PortBattleOutcome, value, rules_.outcomeVariable));
  if (auto written = state_->setVariable(rules_.outcomeVariable, value); !written) {
    core::logError("{}", written.error().message);
  }
  switch (outcome) {
  case BattleOutcome::Defeat:
    // The battle system already took the life and the items (handleItemLoss).
    core::logDebug("{}",
                   messages::formatMessage(messages::PortBattleDefeat, 0, rules_.defeatSection));
    return TaskJump{.target = {.script = 0, .section = rules_.defeatSection}};
  case BattleOutcome::Fled:
    return afterFlight();
  case BattleOutcome::Victory:
    break;
  }
  const bool fixed = fixedFight();
  core::logDebug("{}", messages::formatMessage(messages::PortBattleWon, rules_.fixedFightTrigger,
                                               fixed, !fixed));
  if (fixed) {
    battle_->holdAfterFixedFight();
    return TaskDone{};
  }
  battle_->startPartnerReturn();
  return TaskRunning{};
}

TaskStep StoryBattles::afterFlight() {
  counters_.fled = std::min(counters_.fled + 1, rules_.counterMax);
  const ScreenPlace back = screens_->place();
  screens_->leaveByFlight();
  core::logDebug("{}", messages::formatMessage(messages::PortBattleFled, counters_.fled,
                                               back.previousScreen, back.previousExit));
  // SCRIPT_MAP_CHANGE_SHOW_NAME = 0: the runner shows no name when the variable says never.
  if (auto written = state_->setVariable(rules_.showNameVariable, rules_.showNameNever); !written) {
    core::logError("{}", written.error().message);
  }
  return TaskMapChange{
      .change = {.map = back.previousScreen, .exit = back.previousExit, .arrival = std::nullopt}};
}

} // namespace dwe::game::story
