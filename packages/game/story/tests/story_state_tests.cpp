#include <dwe/core/yaml.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/game/story/story_state_yaml.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <string>

using dwe::game::story::fromYaml;
using dwe::game::story::StoryState;
using dwe::game::story::toYaml;
using dwe::game::story::TriggerCount;
using dwe::game::story::VariableCount;

TEST_CASE("the story state keeps triggers 0-799 and variables 0-255, and refuses the rest") {
  StoryState state;
  CHECK(state.trigger(203).value() == false);
  REQUIRE(state.setTrigger(203, true).has_value());
  CHECK(state.trigger(203).value());
  CHECK(state.triggersSet() == 1);
  REQUIRE(state.setTrigger(203, false).has_value());
  CHECK(state.triggersSet() == 0);
  CHECK_FALSE(state.setTrigger(TriggerCount, true).has_value());
  CHECK_FALSE(state.setTrigger(-1, true).has_value());
  CHECK_FALSE(state.trigger(TriggerCount).has_value());
  CHECK(state.triggersSet() == 0);

  REQUIRE(state.setVariable(34, 7).has_value());
  CHECK(state.variable(34).value() == 7);
  CHECK_FALSE(state.setVariable(VariableCount, 1).has_value());
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
  const auto parsed = [](const char* text) {
    auto tree = dwe::core::parseYaml(text);
    REQUIRE(tree.has_value());
    return fromYaml(*tree);
  };
  const std::string zeros(200, '0');
  CHECK_FALSE(parsed(("triggers: \"" + zeros + "\"\nvariables: []\n").c_str()).has_value());
  CHECK_FALSE(parsed("triggers: \"00\"\nvariables: []\n").has_value());
  std::string variables;
  for (int index = 0; index < 35; ++index) {
    variables += index == 0 ? "0" : ", 0";
  }
  CHECK(parsed(("triggers: \"" + zeros + "\"\nvariables: [" + variables + "]\n").c_str())
            .has_value());
  CHECK_FALSE(
      parsed(("triggers: \"" + std::string(199, '0') + "g\"\nvariables: [" + variables + "]\n")
                 .c_str())
          .has_value());
  CHECK_FALSE(
      parsed(("triggers: \"" + zeros + "\"\nvariables: [256" + variables.substr(1) + "]\n").c_str())
          .has_value());
}
