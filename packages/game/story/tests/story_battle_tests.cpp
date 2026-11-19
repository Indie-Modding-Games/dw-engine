#include <dwe/game/story/screen_history.hpp>
#include <dwe/game/story/story_battle.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

using namespace dwe::game::story;

namespace {

// The original ruleset's numbers (rules/original/story_scenes.yaml).
constexpr std::int64_t Triggers = 800;
constexpr std::int64_t Variables = 256;
constexpr std::int64_t Predefined = 250;
constexpr std::int64_t FirstEnemy = 251;
constexpr std::int64_t NoEnemy = 255;
constexpr std::int64_t FixedFight = 1;
constexpr StoryBattleRules Rules{.outcomeVariable = 255,
                                 .predefinedVariable = Predefined,
                                 .firstEnemyVariable = FirstEnemy,
                                 .enemyCount = 3,
                                 .noEnemy = NoEnemy,
                                 .defeatSection = 0x4de,
                                 .counterMax = 9999,
                                 .fixedFightTrigger = FixedFight,
                                 .showNameVariable = 6,
                                 .showNameNever = 2};

class FakeState final : public StoryStatePort {
public:
  [[nodiscard]] std::int64_t triggerCount() const override { return Triggers; }
  [[nodiscard]] std::int64_t variableCount() const override { return Variables; }
  [[nodiscard]] dwe::core::Result<bool> trigger(std::int64_t trigger) const override {
    return triggers_.at(static_cast<std::size_t>(trigger));
  }
  dwe::core::Result<void> setTrigger(std::int64_t trigger, bool set) override {
    triggers_.at(static_cast<std::size_t>(trigger)) = set;
    return {};
  }
  [[nodiscard]] dwe::core::Result<std::int64_t> variable(std::int64_t variable) const override {
    return variables_.at(static_cast<std::size_t>(variable));
  }
  dwe::core::Result<void> setVariable(std::int64_t variable, std::int64_t value) override {
    variables_.at(static_cast<std::size_t>(variable)) = value;
    return {};
  }

private:
  std::vector<bool> triggers_ = std::vector<bool>(static_cast<std::size_t>(Triggers));
  std::vector<std::int64_t> variables_ =
      std::vector<std::int64_t>(static_cast<std::size_t>(Variables));
};

// A map whose NPCs have the script ids `entities` names; the player was touched by `touchedBy`.
class FakeBattle final : public StoryBattlePort {
public:
  FakeBattle(std::map<std::int64_t, std::int64_t> entities, std::optional<std::int64_t> touchedBy)
      : entities_{std::move(entities)}, touchedBy_{touchedBy} {}

  [[nodiscard]] std::optional<std::int64_t> entityOf(std::int64_t scriptId) const override {
    const auto found = entities_.find(scriptId);
    return found == entities_.end() ? std::nullopt : std::optional{found->second};
  }
  [[nodiscard]] std::optional<std::int64_t> talkedTo() const override { return touchedBy_; }
  void startBattle(const ScriptedBattle& battle) override { started = battle; }
  std::optional<BattleOutcome> battleOutcome() override { return std::exchange(outcome, {}); }
  void startCameraReturn() override {
    ++cameraReturns;
    cameraMoving = true;
  }
  [[nodiscard]] bool cameraReturned() const override { return !cameraMoving; }
  void holdAfterFixedFight() override {}

  std::optional<ScriptedBattle> started;
  // The outcome the next poll reads, and the camera's pans back onto the player.
  std::optional<BattleOutcome> outcome;
  int cameraReturns = 0;
  bool cameraMoving = false;

private:
  std::map<std::int64_t, std::int64_t> entities_;
  std::optional<std::int64_t> touchedBy_;
};

// The bridge of the forest: NPCs with script ids 5, 6 and 7 at entities 2, 3 and 4; the one with
// script id 7 touched the player and its section asks for a battle with none predefined.
FakeBattle bridge() {
  return FakeBattle{{{5, 2}, {6, 3}, {7, 4}}, 7};
}

} // namespace

TEST_CASE("a battle with none predefined fights the NPC that touched the player") {
  FakeState state;
  FakeBattle battle = bridge();
  ScreenHistory screens;
  StoryBattles battles{state, battle, screens, Rules};
  REQUIRE(battles.startBattle().has_value());
  REQUIRE(battle.started.has_value());
  CHECK(battle.started->enemies == std::vector<std::int64_t>{4});
  CHECK(battle.started->joinable);
  CHECK_FALSE(battle.started->fixedFight);
  CHECK_FALSE(battle.started->predefined);
}

TEST_CASE("a predefined battle fights the NPCs its variables name, turned into entities") {
  FakeState state;
  REQUIRE(state.setVariable(Predefined, 1));
  REQUIRE(state.setVariable(FirstEnemy, 6));
  REQUIRE(state.setVariable(FirstEnemy + 1, NoEnemy));
  REQUIRE(state.setVariable(FirstEnemy + 2, NoEnemy));
  REQUIRE(state.setTrigger(FixedFight, true));
  FakeBattle battle = bridge();
  ScreenHistory screens;
  StoryBattles battles{state, battle, screens, Rules};
  REQUIRE(battles.startBattle().has_value());
  REQUIRE(battle.started.has_value());
  CHECK(battle.started->enemies == std::vector<std::int64_t>{3});
  CHECK_FALSE(battle.started->joinable);
  CHECK(battle.started->fixedFight);
  CHECK(state.variable(FirstEnemy).value_or(-1) == 3);
  CHECK(state.variable(Predefined).value_or(-1) == 1);
  // btl.c: loadBattleData plays the event battle theme for it (IS_PREDEFINED_BATTLE).
  CHECK(battle.started->predefined);
}

TEST_CASE(
    "a predefined setup a fled fight left behind does not keep the next battle from starting") {
  // A fight fled before Run was taken off fixed fights skipped the end of its section: trigger 1,
  // variable 250 at 1 and its enemy turned into entity 3 in variable 251, on another map. The next
  // NPC that touches the player found no enemy, the story read a victory and the NPC's section ran
  // again forever.
  FakeState state;
  REQUIRE(state.setVariable(Predefined, 1));
  REQUIRE(state.setVariable(FirstEnemy, 3));
  REQUIRE(state.setVariable(FirstEnemy + 1, NoEnemy));
  REQUIRE(state.setVariable(FirstEnemy + 2, NoEnemy));
  REQUIRE(state.setTrigger(FixedFight, true));
  FakeBattle battle = bridge();
  ScreenHistory screens;
  StoryBattles battles{state, battle, screens, Rules};
  REQUIRE(battles.startBattle().has_value());
  REQUIRE(battle.started.has_value());
  CHECK(battle.started->enemies == std::vector<std::int64_t>{4});
  CHECK(battle.started->joinable);
  CHECK_FALSE(battle.started->predefined);
  CHECK(state.variable(Predefined).value_or(-1) == 0);
}

TEST_CASE("a victory goes on once the camera has panned back onto the player") {
  // script_interp.c:1949: after a won battle the camera's slot pans to the player (0xFD) in 10
  // frames and the section waits on it; the player ran back in the battle's opening and would stay
  // off the centre of the screen.
  FakeState state;
  FakeBattle battle = bridge();
  ScreenHistory screens;
  StoryBattles battles{state, battle, screens, Rules};
  const auto task = battles.startBattle();
  REQUIRE(task.has_value());
  CHECK(std::holds_alternative<TaskRunning>((*task)->tick()));
  CHECK(battle.cameraReturns == 0);
  battle.outcome = BattleOutcome::Victory;
  CHECK(std::holds_alternative<TaskRunning>((*task)->tick()));
  CHECK(battle.cameraReturns == 1);
  CHECK(std::holds_alternative<TaskRunning>((*task)->tick()));
  battle.cameraMoving = false;
  CHECK(std::holds_alternative<TaskDone>((*task)->tick()));
  CHECK(battle.cameraReturns == 1);
}

TEST_CASE("a fixed fight won leaves the camera where it is") {
  // script_interp.c:1944: with trigger 1 set the section goes on at once, with no pan.
  FakeState state;
  FakeBattle battle = bridge();
  ScreenHistory screens;
  StoryBattles battles{state, battle, screens, Rules};
  const auto task = battles.startBattle();
  REQUIRE(task.has_value());
  REQUIRE(state.setTrigger(FixedFight, true));
  battle.outcome = BattleOutcome::Victory;
  CHECK(std::holds_alternative<TaskDone>((*task)->tick()));
  CHECK(battle.cameraReturns == 0);
}

TEST_CASE("a battle that cannot start is taken as a flight, never as a win") {
  // Before, the game reported a victory when it could not start a story battle (no enemy on the
  // map, a species missing from the content), and the section went on as if the fight was won.
  FakeState state;
  REQUIRE(state.setVariable(Rules.outcomeVariable, 1));
  FakeBattle battle = bridge();
  ScreenHistory screens;
  screens.start(3, 0);
  screens.enter(5, 2);
  StoryBattles battles{state, battle, screens, Rules};
  const auto task = battles.startBattle();
  REQUIRE(task.has_value());
  battle.outcome = BattleOutcome::NotStarted;
  const TaskStep step = (*task)->tick();
  const auto* back = std::get_if<TaskMapChange>(&step);
  REQUIRE(back != nullptr);
  CHECK(back->change.map == 3);
  CHECK(state.variable(Rules.outcomeVariable).value_or(-1) == 0);
  CHECK(battle.cameraReturns == 0);
  // A flight the player never took is not counted.
  CHECK(battles.counters().fled == 0);
}
