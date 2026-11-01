#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/species_extra.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "ruleset_fixture.hpp"

namespace dwe::ruleset {

namespace {

using testing::errorOf;
using testing::FixturePacks;

constexpr std::string_view Extra = R"(species:
  newmon:
    requirement: { from: rookie_b, offense: 150, care: 2, care_is_maximum: true, weight: 25, techs: 30 }
    gate: { triggers: [248], hours: { from: 20, to: 6 }, cup: 16 }
    evolves_from: [rookie_a, rookie_b]
    evolves_to: [ultimate_a]
    stats_gains: { hp: 6000, mp: 7000, offense: 600, defense: 500, speed: 700, brains: 700 }
    raise: { copy: rookie_a, favorite_food: meat, default_weight: 70 }
    moves: [claw]
    partner_type: 3
    partner_moves: { cannon: { power: 240, iframes: 1 } }
    clips: { 3: 2, 5: 42 }
    field_scale: 0.6
    chart: { x: 189, y: 176, icon: rookie_a, palette: 2 }
extra_prizes:
  - { cup: 16, item: 125 }
)";

constexpr std::string_view Mega = R"(check_hours: 120
lifetime_hours: 72
tamer_level_chance: 100
mega: [newmon]
species:
  newmon:
    evolves_from: [ultimate_b]
    gate: { triggers: [50] }
)";

// The `small` ruleset of the fixture with the hidden content's sections, and `small_mega`, which
// extends it with the Mega step.
void addExtra(FixturePacks& packs) {
  packs.files.addFile("rules/rules/small/ruleset.yaml", R"(id: small
name: Small
version: 1.0.0
description: A ruleset for the tests.
tables:
  digimon: data/digimon
  raise: data/raise
sections: [clock, care, species_extra]
)");
  packs.files.addFile("rules/rules/small/species_extra.yaml", std::string{Extra});
  packs.files.addFile("rules/rules/small_mega/ruleset.yaml", R"(id: small_mega
name: Small Mega
version: 1.0.0
description: With the Mega step.
extends: small
sections: [mega_step]
)");
  packs.files.addFile("rules/rules/small_mega/mega_step.yaml", std::string{Mega});
}

assets::EvolutionTable pathsOf() {
  assets::EvolutionTable table;
  table.paths.push_back({.digimon = "rookie_a",
                         .index = 1,
                         .from = {},
                         .to = {std::nullopt, std::string{"champ_a"}, std::nullopt, std::nullopt,
                                std::nullopt, std::nullopt}});
  table.paths.push_back({.digimon = "rookie_b",
                         .index = 2,
                         .from = {},
                         .to = {std::string{"x1"}, std::string{"x2"}, std::string{"x3"},
                                std::string{"x4"}, std::string{"x5"}, std::nullopt}});
  table.paths.push_back({.digimon = "ultimate_b", .index = 3, .from = {}, .to = {}});
  return table;
}

// The game pack's rulesets with the hidden content, and each one's sections.
struct GamePackRuleset {
  std::string_view id;
  std::string_view section;
};
constexpr std::array<GamePackRuleset, 1> HiddenRulesets{
    {{.id = "oculto", .section = "species_extra"}}};

std::string_view textOf(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

} // namespace

TEST_CASE("the game pack's hidden content rulesets follow their schemas and read") {
  const assets::DiskFileSystem disk;
  // A stand-in base pack with empty tables lets the game pack mount without the disc.
  assets::MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
  for (const std::string_view role :
       {"digimon", "evolution", "raise", "training", "moves", "items", "item_effects", "shops"}) {
    memory.addFile(std::format("base/data/{}.yaml", role), std::format("{}: []\n", role));
  }
  const std::array<assets::PackLocation, 2> packs{
      assets::PackLocation{.fileSystem = &memory,
                           .root = "base",
                           .fillsGaps = false,
                           .origin = assets::PackOrigin::Engine},
      assets::PackLocation{.fileSystem = &disk,
                           .root = DWE_GAME_PACK,
                           .fillsGaps = false,
                           .origin = assets::PackOrigin::Engine}};
  const auto vfs = assets::VirtualFileSystem::mount(packs);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  for (const GamePackRuleset& hidden : HiddenRulesets) {
    CAPTURE(hidden.id);
    const auto schemaBytes = disk.readFile(std::filesystem::path{DWE_SCHEMA_DIR} /
                                           std::format("ruleset_{}.schema.json", hidden.section));
    REQUIRE_MESSAGE(schemaBytes.has_value(), errorOf(schemaBytes));
    const auto schemaText = core::parseYaml(textOf(*schemaBytes));
    REQUIRE_MESSAGE(schemaText.has_value(), errorOf(schemaText));
    constexpr std::array<std::string_view, 1> Annotations{"entryReference"};
    const auto schema = core::JsonSchema::compile(*schemaText, Annotations);
    REQUIRE_MESSAGE(schema.has_value(), errorOf(schema));
    const auto bytes = disk.readFile(std::filesystem::path{DWE_GAME_PACK} /
                                     std::format("rules/{}/{}.yaml", hidden.id, hidden.section));
    REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
    const auto document = core::parseYaml(textOf(*bytes));
    REQUIRE_MESSAGE(document.has_value(), errorOf(document));
    for (const core::SchemaProblem& problem : schema->check(*document).problems) {
      FAIL_CHECK(std::format("{}:{}: {}: {}", problem.line, problem.column, problem.path,
                             problem.message));
    }
    const auto ruleset = loadRuleset(*vfs, hidden.id);
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    CHECK(ruleset->info().chain.back() == "original");
    const auto extra = readSpeciesExtra(*ruleset);
    REQUIRE_MESSAGE(extra.has_value(), errorOf(extra));
    CHECK_FALSE(extra->empty());
  }
}

TEST_CASE("species_extra reads every patch of a species, and the Mega step replaces its fields") {
  FixturePacks packs;
  addExtra(packs);
  const auto& vfs = packs.mount();
  const auto plain = loadRuleset(vfs, "small");
  REQUIRE_MESSAGE(plain.has_value(), errorOf(plain));
  const auto extra = readSpeciesExtra(*plain);
  REQUIRE_MESSAGE(extra.has_value(), errorOf(extra));
  const SpeciesPatch* patch = extra->find("newmon");
  REQUIRE(patch != nullptr);
  REQUIRE(patch->requirement);
  CHECK(patch->requirement->offense == 150);
  CHECK(patch->requirement->hp == -1);
  CHECK(patch->requirement->flags == 0x10);
  CHECK(patch->requirement->digimon == std::optional<std::string>{"rookie_b"});
  REQUIRE(patch->gate);
  CHECK(patch->gate->triggers == std::vector<std::int32_t>{248});
  CHECK(patch->gate->hours->holds(23));
  CHECK(patch->gate->hours->holds(2));
  CHECK_FALSE(patch->gate->hours->holds(12));
  CHECK(patch->gate->cup == std::optional<std::int32_t>{16});
  CHECK(patch->clips.at(5) == 42);
  CHECK(patch->fieldScale == doctest::Approx(0.6));
  CHECK(patch->partnerMoves.at("cannon").power == 240);
  CHECK(extra->extraPrizes);
  CHECK_FALSE(extra->megaStep);
  CHECK_FALSE(extra->isMega("newmon"));

  const auto mega = loadRuleset(vfs, "small_mega");
  REQUIRE_MESSAGE(mega.has_value(), errorOf(mega));
  const auto stepped = readSpeciesExtra(*mega);
  REQUIRE_MESSAGE(stepped.has_value(), errorOf(stepped));
  REQUIRE(stepped->megaStep);
  CHECK(stepped->megaStep->checkHours == 120);
  CHECK(stepped->isMega("newmon"));
  const SpeciesPatch* over = stepped->find("newmon");
  CHECK(*over->evolvesFrom == std::vector<std::string>{"ultimate_b"});
  CHECK(over->gate->triggers == std::vector<std::int32_t>{50});
  // What the step leaves alone stays species_extra's.
  CHECK(over->requirement->offense == 150);
}

TEST_CASE("a ruleset without the sections has no patches") {
  FixturePacks packs;
  const auto ruleset = loadRuleset(packs.mount(), "small");
  REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
  const auto extra = readSpeciesExtra(*ruleset);
  REQUIRE_MESSAGE(extra.has_value(), errorOf(extra));
  CHECK(extra->empty());
}

TEST_CASE("the patches put a new target after the last one of each source and keep the rest") {
  SpeciesExtra extra;
  extra.species["newmon"] = SpeciesPatch{.evolvesFrom = std::vector<std::string>{"rookie_a"},
                                         .evolvesTo = std::vector<std::string>{"ultimate_a"}};
  auto table = pathsOf();
  REQUIRE(applySpeciesExtra(extra, table).has_value());
  CHECK(table.paths.at(0).to.at(1) == std::optional<std::string>{"champ_a"});
  CHECK(table.paths.at(0).to.at(2) == std::optional<std::string>{"newmon"});
  REQUIRE(table.paths.size() == 4);
  CHECK(table.paths.at(3).digimon == std::optional<std::string>{"newmon"});
  CHECK(table.paths.at(3).to.at(0) == std::optional<std::string>{"ultimate_a"});
  CHECK(table.paths.at(3).from.at(0) == std::optional<std::string>{"rookie_a"});
  // Applying again changes nothing.
  REQUIRE(applySpeciesExtra(extra, table).has_value());
  CHECK(std::count(table.paths.at(0).to.begin(), table.paths.at(0).to.end(),
                   std::optional<std::string>{"newmon"}) == 1);

  SpeciesExtra full;
  full.species["newmon"] = SpeciesPatch{.evolvesFrom = std::vector<std::string>{"rookie_b"}};
  auto other = pathsOf();
  // rookie_b's only free slot is the last one.
  REQUIRE(applySpeciesExtra(full, other).has_value());
  CHECK(other.paths.at(1).to.at(5) == std::optional<std::string>{"newmon"});
  full.species["othermon"] = SpeciesPatch{.evolvesFrom = std::vector<std::string>{"rookie_b"}};
  auto crowded = pathsOf();
  CHECK_FALSE(applySpeciesExtra(full, crowded).has_value());
}

TEST_CASE("the patches add requirement, stat, raise rows and moves in empty slots") {
  SpeciesExtra extra;
  assets::EvolutionRequirementEntry requirement;
  requirement.target = "newmon";
  requirement.offense = 150;
  assets::EvolutionStatsGainEntry gains;
  gains.target = "newmon";
  gains.hp = 6000;
  gains.targetDigimon = "newmon";
  extra.species["newmon"] = SpeciesPatch{
      .requirement = requirement,
      .statsGains = gains,
      .raise = RaisePatch{.copy = "rookie_a", .favoriteFood = "meat", .defaultWeight = 70},
      .moves = {"claw", "bite"}};
  assets::EvolutionTable evolution;
  REQUIRE(applySpeciesExtra(extra, evolution).has_value());
  REQUIRE(evolution.requirements.size() == 1);
  CHECK(evolution.requirements.front().offense == 150);
  CHECK(evolution.statsGains.front().hp == 6000);

  assets::RaiseTable raise;
  assets::RaiseRow source;
  source.digimon = "rookie_a";
  source.index = 3;
  source.favoriteFood = "apple";
  source.defaultWeight = 10;
  raise.raise.push_back(source);
  REQUIRE(applySpeciesExtra(extra, raise).has_value());
  const assets::RaiseRow* row = raise.find("newmon");
  REQUIRE(row != nullptr);
  CHECK(row->favoriteFood == std::optional<std::string>{"meat"});
  CHECK(row->defaultWeight == 70);
  CHECK_FALSE(row->index);

  assets::DigimonTable digimon;
  assets::DigimonRow species;
  species.id = "newmon";
  species.index = 66;
  digimon.digimon.push_back(species);
  digimon.digimon.back().moves.at(0) = "punch";
  digimon.digimon.back().moves.at(2) = "kick";
  REQUIRE(applySpeciesExtra(extra, digimon).has_value());
  CHECK(digimon.digimon.back().moves.at(0) == std::optional<std::string>{"punch"});
  CHECK(digimon.digimon.back().moves.at(1) == std::optional<std::string>{"claw"});
  CHECK(digimon.digimon.back().moves.at(3) == std::optional<std::string>{"bite"});

  SpeciesExtra missing;
  missing.species["newmon"] = SpeciesPatch{.raise = RaisePatch{.copy = "nobody"}};
  CHECK_FALSE(applySpeciesExtra(missing, raise).has_value());
}

} // namespace dwe::ruleset
