// The options that change the game's rules (rule_options.hpp; docs/modulos/apps/game.md,
// «Dificultad» and «Rarezas como opciones»): the game pack's own difficulty levels and quirks,
// applied to and taken back from a running game's ruleset.

#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_store.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <utility>

#include "rule_options.hpp"
#include "session_fixture.hpp"

namespace dwe::app {
namespace {

using session::testing::errorOf;
using session::testing::TestPacks;

// The value at a path of a section of the store's ruleset, as its YAML text.
std::string valueAt(const ruleset::RulesetStore& store, std::string_view section,
                    std::string_view path) {
  const ruleset::Section* found = store.current().section(section);
  REQUIRE_MESSAGE(found != nullptr, section);
  const core::YamlNode* node = ruleset::findValue(found->node, path);
  REQUIRE_MESSAGE(node != nullptr, path);
  return node->text();
}

ruleset::RulesetStore originalRules(const TestPacks& packs) {
  auto store = ruleset::RulesetStore::load(packs.files(), "original");
  REQUIRE_MESSAGE(store.has_value(), errorOf(store));
  return std::move(*store);
}

} // namespace

TEST_CASE("normal changes nothing and the game pack's easy names values its ruleset has") {
  const TestPacks packs;
  const ruleset::RulesetStore store = originalRules(packs);
  const auto normal = difficultyChanges(store.current(), DifficultyNormal);
  REQUIRE_MESSAGE(normal.has_value(), errorOf(normal));
  CHECK(normal->empty());
  const auto easy = difficultyChanges(store.current(), DifficultyEasy);
  REQUIRE_MESSAGE(easy.has_value(), errorOf(easy));
  REQUIRE_FALSE(easy->empty());
  for (const RuleChange& change : *easy) {
    CAPTURE(change.section);
    CAPTURE(change.path);
    const ruleset::Section* section = store.current().section(change.section);
    REQUIRE(section != nullptr);
    CHECK(ruleset::findValue(section->node, change.path) != nullptr);
  }
  // A level the section does not name changes nothing either.
  const auto unknown = difficultyChanges(store.current(), "nightmare");
  REQUIRE(unknown.has_value());
  CHECK(unknown->empty());
}

TEST_CASE("easy and the fair quirks change the running ruleset and give its values back") {
  const TestPacks packs;
  ruleset::RulesetStore store = originalRules(packs);
  const std::string window = valueAt(store, "care", "hunger.window_minutes");
  const std::string dealt = valueAt(store, "battle", "damage.partner_dealt_percent");
  REQUIRE(valueAt(store, "care", "quirks.sickness_chance_wraps") == "true");
  REQUIRE(valueAt(store, "evolution", "quirks.selection_average_carries_over") == "true");
  REQUIRE(valueAt(store, "partner_moves", "quirks.forget_mask_16_bits") == "true");
  REQUIRE(valueAt(store, "card_shop", "quirks.stock_repeats") == "true");
  REQUIRE(valueAt(store, "life", "rebirth.random_egg_without_lives") == "true");

  RuleOptions options;
  options.keep(store); // the original's choices: nothing changes
  CHECK(valueAt(store, "care", "hunger.window_minutes") == window);
  const auto unchanged = store.revision();
  options.keep(store);
  CHECK(store.revision() == unchanged);

  options.choose({.difficulty = std::string{DifficultyEasy},
                  .fairSicknessChance = true,
                  .fairEvolutionAverage = true,
                  .fairMoveForgetting = true,
                  .fairCardStock = true,
                  .chooseEggAfterLivesLost = true});
  options.keep(store);
  CHECK(valueAt(store, "care", "hunger.window_minutes") == std::to_string(std::stoi(window) * 2));
  CHECK(valueAt(store, "battle", "damage.partner_dealt_percent") ==
        std::to_string(std::stoi(dealt) * 3 / 2));
  CHECK(valueAt(store, "care", "quirks.sickness_chance_wraps") == "false");
  CHECK(valueAt(store, "evolution", "quirks.selection_average_carries_over") == "false");
  CHECK(valueAt(store, "partner_moves", "quirks.forget_mask_16_bits") == "false");
  CHECK(valueAt(store, "card_shop", "quirks.stock_repeats") == "false");
  CHECK(valueAt(store, "life", "rebirth.random_egg_without_lives") == "false");
  // Kept every frame, the changes are made once: the percent is of the ruleset's own value.
  options.keep(store);
  options.keep(store);
  CHECK(valueAt(store, "care", "hunger.window_minutes") == std::to_string(std::stoi(window) * 2));

  // A reload drops the tuned values; the next keep makes them again.
  REQUIRE(store.reload().has_value());
  CHECK(valueAt(store, "care", "hunger.window_minutes") == window);
  options.keep(store);
  CHECK(valueAt(store, "care", "hunger.window_minutes") == std::to_string(std::stoi(window) * 2));

  // Back to normal: every value is the ruleset's own again.
  options.choose({});
  options.keep(store);
  CHECK(valueAt(store, "care", "hunger.window_minutes") == window);
  CHECK(valueAt(store, "battle", "damage.partner_dealt_percent") == dealt);
  CHECK(valueAt(store, "care", "quirks.sickness_chance_wraps") == "true");
  CHECK(valueAt(store, "evolution", "quirks.selection_average_carries_over") == "true");
  CHECK(valueAt(store, "partner_moves", "quirks.forget_mask_16_bits") == "true");
  CHECK(valueAt(store, "card_shop", "quirks.stock_repeats") == "true");
  CHECK(valueAt(store, "life", "rebirth.random_egg_without_lives") == "true");
}

TEST_CASE("a value the tuning panel changed after a choice is not taken back with it") {
  const TestPacks packs;
  ruleset::RulesetStore store = originalRules(packs);
  const std::string window = valueAt(store, "care", "hunger.window_minutes");
  RuleOptions options;
  options.choose({.difficulty = std::string{DifficultyEasy},
                  .fairSicknessChance = false,
                  .fairEvolutionAverage = false});
  options.keep(store);
  REQUIRE(valueAt(store, "care", "hunger.window_minutes") != window);
  REQUIRE(store.setValue("care", "hunger.window_minutes", "100").has_value());
  options.choose({});
  options.keep(store);
  CHECK(valueAt(store, "care", "hunger.window_minutes") == "100");
}

} // namespace dwe::app
