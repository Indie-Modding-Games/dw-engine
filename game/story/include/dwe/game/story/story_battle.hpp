#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/screen_history.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/game/story/story_task.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// start_battle: the story's battles and what their outcome does to the script (script_interp.c:
// scriptInstruction64to7E, SCRIPT_OP_START_BATTLE; tamer.c: startBattle). The battle itself and
// what it does to the partner and the bag are the battle system's; the story starts it through a
// port and reads its outcome.
namespace dwe::game::story {

// The original's outcome, as start_battle writes it into a variable: -1, 0 or 1.
enum class BattleOutcome : std::int8_t { Defeat = -1, Fled = 0, Victory = 1 };

// The numbers of the scripted battle, from the ruleset.
struct StoryBattleRules {
  // The variable that gets the outcome, as a byte (-1 is 255).
  std::int64_t outcomeVariable = 0;
  // While this variable is not 0 the enemies are the ones in the variables from firstEnemy on
  // (enemyCount of them), each a script id or noEnemy; start_battle turns them into entities.
  std::int64_t predefinedVariable = 0;
  std::int64_t firstEnemyVariable = 0;
  std::int64_t enemyCount = 0;
  std::int64_t noEnemy = 0;
  // A lost battle moves the script to this section of the map-head script.
  std::uint16_t defeatSection = 0;
  // The counters of battles started and fled stop here.
  std::int64_t counterMax = 0;
  // While this trigger is set the fight cannot be fled and the partner does not walk back after a
  // victory.
  std::int64_t fixedFightTrigger = 0;
  // A fled battle's map change shows no name: the runner's show-name variable is set to never.
  std::int64_t showNameVariable = 0;
  std::int64_t showNameNever = 0;
};

// What the battle needs from the field and the battle system.
struct ScriptedBattle {
  // The entities the predefined enemies are, in order; empty: the one that started the script and
  // those that join it (btl.c: handleBattleStart).
  std::vector<std::int64_t> enemies;
  // The fight cannot be fled (btl.c: handleBattleStart, FLEE_DISABLED).
  bool fixedFight = false;
};

class StoryBattlePort {
public:
  StoryBattlePort() = default;
  StoryBattlePort(const StoryBattlePort&) = delete;
  StoryBattlePort& operator=(const StoryBattlePort&) = delete;
  StoryBattlePort(StoryBattlePort&&) = delete;
  StoryBattlePort& operator=(StoryBattlePort&&) = delete;
  virtual ~StoryBattlePort() = default;

  // The entity a script id names: 0xFD the player, 0xFC the partner, an NPC by its script id;
  // nothing when no entity has it (script_value.c: scriptIdToEntityId).
  [[nodiscard]] virtual std::optional<std::int64_t> entityOf(std::int64_t scriptId) const = 0;
  virtual void startBattle(const ScriptedBattle& battle) = 0;
  // The outcome once the battle and its aftermath are over; nothing while it runs.
  virtual std::optional<BattleOutcome> battleOutcome() = 0;
  // After a victory the partner walks back to the player and the script waits for it
  // (script_interp.c: SCRIPT_MOVEMENT[10] type 7, WAIT_FOR_ENTITY 10).
  virtual void startPartnerReturn() = 0;
  [[nodiscard]] virtual bool partnerReturned() const = 0;
};

// The battles the story counted (BATTLES_STARTED, BATTLES_FLED), for the medals.
struct BattleCounters {
  std::int64_t started = 0;
  std::int64_t fled = 0;

  friend bool operator==(const BattleCounters&, const BattleCounters&) = default;
};

class StoryBattles {
public:
  // The ports and the history must outlive the battles and the tasks they start.
  StoryBattles(StoryStatePort& state, StoryBattlePort& battle, ScreenHistory& screens,
               StoryBattleRules rules);

  // start_battle: counts it, names the predefined enemies, starts it and gives the task the
  // section waits on. Its outcome goes into the outcome variable; then a victory goes on (after the
  // partner walks back), a defeat moves the script to the map-head's defeat section and a flight
  // goes back to the previous screen.
  core::Result<std::shared_ptr<StoryTask>> startBattle();

  [[nodiscard]] const BattleCounters& counters() const { return counters_; }
  void restoreCounters(const BattleCounters& counters) { counters_ = counters; }

private:
  class BattleTask;

  core::Result<ScriptedBattle> enemies();
  TaskStep afterBattle(BattleOutcome outcome);
  TaskStep afterFlight();
  [[nodiscard]] bool fixedFight() const;

  StoryStatePort* state_;
  StoryBattlePort* battle_;
  ScreenHistory* screens_;
  StoryBattleRules rules_;
  BattleCounters counters_;
};

} // namespace dwe::game::story
