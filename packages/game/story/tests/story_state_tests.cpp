#include <dwe/core/yaml.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/game/story/story_state_yaml.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <utility>

using dwe::game::story::BaseTriggerCount;
using dwe::game::story::BaseVariableCount;
using dwe::game::story::fromYaml;
using dwe::game::story::StoryCapacity;
using dwe::game::story::StoryState;
using dwe::game::story::storyStateFromVersion1;
using dwe::game::story::toYaml;

namespace {

// The 35 saved base variables, all 0, as a YAML flow sequence.
std::string zeroVariables() {
  std::string variables;
  for (int index = 0; index < 35; ++index) {
    variables += index == 0 ? "0" : ", 0";
  }
  return "[" + variables + "]";
}

dwe::core::Result<StoryState> parsed(const std::string& text, StoryCapacity capacity = {}) {
  auto tree = dwe::core::parseYaml(text);
  REQUIRE(tree.has_value());
  return fromYaml(*tree, capacity);
}

} // namespace

TEST_CASE("the story state keeps triggers 0-799 and variables 0-255, and refuses the rest") {
  StoryState state;
  CHECK(state.trigger(203).value() == false);
  REQUIRE(state.setTrigger(203, true).has_value());
  CHECK(state.trigger(203).value());
  CHECK(state.triggersSet() == 1);
  REQUIRE(state.setTrigger(203, false).has_value());
  CHECK(state.triggersSet() == 0);
  CHECK_FALSE(state.setTrigger(BaseTriggerCount, true).has_value());
  CHECK_FALSE(state.setTrigger(-1, true).has_value());
  CHECK_FALSE(state.trigger(BaseTriggerCount).has_value());
  CHECK(state.triggersSet() == 0);

  REQUIRE(state.setVariable(34, 7).has_value());
  CHECK(state.variable(34).value() == 7);
  CHECK_FALSE(state.setVariable(BaseVariableCount, 1).has_value());
  CHECK_FALSE(state.variable(-1).has_value());
}

TEST_CASE(
    "the saved story state holds the trigger bytes as the original lays them out, and the saved variables") {
  StoryState state;
  REQUIRE(state.setTrigger(0, true).has_value());
  REQUIRE(state.setTrigger(9, true).has_value());
  REQUIRE(state.setTrigger(799, true).has_value());
  REQUIRE(state.setVariable(0, 3).has_value());
  REQUIRE(state.setVariable(34, 255).has_value());
  REQUIRE(state.setVariable(35, 9).has_value());

  const dwe::core::YamlNode node = toYaml(state);
  const auto digits = node.find("triggers");
  REQUIRE(digits != nullptr);
  const std::string text = digits->text();
  REQUIRE(text.size() == 200);
  // Trigger 0 is bit 0 of byte 0, trigger 9 bit 1 of byte 1, trigger 799 bit 7 of byte 99.
  CHECK(text.substr(0, 4) == "0102");
  CHECK(text.substr(198) == "80");
  REQUIRE(node.find("variables") != nullptr);
  CHECK(node.find("variables")->items().size() == 35);

  const auto read = fromYaml(node);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  CHECK(read->trigger(9).value());
  CHECK(read->trigger(799).value());
  CHECK(read->triggersSet() == 3);
  CHECK(read->variable(34).value() == 255);
  // Variables past the saved ones live only while the game runs.
  CHECK(read->variable(35).value() == 0);
}

TEST_CASE("a saved story state with wrong trigger digits or variables is an error") {
  const std::string zeros(200, '0');
  const std::string variables = zeroVariables();
  const std::string extra = "\nextra_variables: []\n";
  CHECK_FALSE(parsed("triggers: \"" + zeros + "\"\nvariables: []" + extra).has_value());
  CHECK_FALSE(parsed("triggers: \"00\"\nvariables: " + variables + extra).has_value());
  CHECK(parsed("triggers: \"" + zeros + "\"\nvariables: " + variables + extra).has_value());
  // Version 1's shape, without extra_variables, is the migration's to bring up.
  CHECK_FALSE(parsed("triggers: \"" + zeros + "\"\nvariables: " + variables + "\n").has_value());
  CHECK_FALSE(
      parsed("triggers: \"" + std::string(199, '0') + "g\"\nvariables: " + variables + extra)
          .has_value());
  CHECK_FALSE(parsed("triggers: \"" + std::string(201, '0') + "\"\nvariables: " + variables + extra)
                  .has_value());
  CHECK_FALSE(parsed("triggers: \"" + zeros + "\"\nvariables: [256" + variables.substr(2) + extra)
                  .has_value());
  CHECK_FALSE(
      parsed("triggers: \"" + zeros + "\"\nvariables: " + variables + "\nextra_variables: [-1]\n",
             StoryCapacity{.extraVariables = 1})
          .has_value());
}

TEST_CASE("a ruleset's capacity adds triggers and variables after the base set") {
  StoryState state{StoryCapacity{.extraTriggers = 20, .extraVariables = 3}};
  CHECK(state.triggerCount() == BaseTriggerCount + 20);
  CHECK(state.variableCount() == BaseVariableCount + 3);
  REQUIRE(state.setTrigger(BaseTriggerCount + 19, true).has_value());
  CHECK_FALSE(state.setTrigger(BaseTriggerCount + 20, true).has_value());
  // An extra variable is a plain integer, not a byte.
  REQUIRE(state.setVariable(BaseVariableCount + 2, 100'000).has_value());
  CHECK(state.variable(BaseVariableCount + 2).value() == 100'000);

  // Shrinking drops what no longer fits; growing again starts it clear.
  REQUIRE(state.setTrigger(5, true).has_value());
  state.setCapacity({});
  CHECK(state.triggerCount() == BaseTriggerCount);
  CHECK(state.trigger(5).value());
  CHECK_FALSE(state.trigger(BaseTriggerCount + 19).has_value());
  state.setCapacity({.extraTriggers = 20, .extraVariables = 3});
  CHECK_FALSE(state.trigger(BaseTriggerCount + 19).value());
  CHECK(state.variable(BaseVariableCount + 2).value() == 0);
}

TEST_CASE("the saved story state keeps the extra triggers and every extra variable") {
  const StoryCapacity capacity{.extraTriggers = 10, .extraVariables = 2};
  StoryState state{capacity};
  REQUIRE(state.setTrigger(BaseTriggerCount + 9, true).has_value());
  REQUIRE(state.setVariable(BaseVariableCount + 1, 70'000).has_value());

  const dwe::core::YamlNode node = toYaml(state);
  // 810 triggers take 102 bytes.
  CHECK(node.find("triggers")->text().size() == 204);
  REQUIRE(node.find("extra_variables") != nullptr);
  CHECK(node.find("extra_variables")->items().size() == 2);

  const auto read = fromYaml(node, capacity);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  CHECK(*read == state);

  // Loaded without the mod that added them, the extras are dropped and the base set stays.
  const auto base = fromYaml(node);
  REQUIRE(base.has_value());
  CHECK(base->triggerCount() == BaseTriggerCount);
  CHECK(base->triggersSet() == 0);
  // And a save without them loads into a bigger capacity with them clear.
  const auto grown = fromYaml(toYaml(StoryState{}), capacity);
  REQUIRE(grown.has_value());
  CHECK(grown->variable(BaseVariableCount + 1).value() == 0);
}

TEST_CASE("a version 1 story state is brought up to version 2 and reads the same") {
  StoryState state;
  REQUIRE(state.setTrigger(799, true).has_value());
  REQUIRE(state.setVariable(34, 200).has_value());
  // Version 1: exactly the base's 100 trigger bytes and the 35 saved variables.
  std::string variables = zeroVariables();
  variables.replace(variables.size() - 2, 1, "200");
  auto tree = dwe::core::parseYaml("triggers: \"" + std::string(198, '0') +
                                   "80\"\nvariables: " + variables + "\n");
  REQUIRE(tree.has_value());
  const auto migrated = storyStateFromVersion1(std::move(*tree));
  REQUIRE_MESSAGE(migrated.has_value(), (migrated ? std::string{} : migrated.error().message));
  const auto read = fromYaml(*migrated);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  CHECK(*read == state);
  CHECK_FALSE(storyStateFromVersion1(*migrated).has_value());
}
