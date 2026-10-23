#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/strategies.hpp>

#include <doctest/doctest.h>

#include <ostream>

#include "fixture.hpp"

namespace dwe::game::evolution {

namespace {

using testing::inputsOf;
using testing::TableFixture;
using testing::valueOf;

constexpr std::uint64_t Seed = 7;

// The original's scoring of one target, with no best candidate so far.
std::int32_t scoreOf(const EvolutionTables& tables, const EvolutionInputs& inputs,
                     std::string_view target, const EvolutionState& state = {},
                     std::optional<Species> currentBest = std::nullopt,
                     const EvolutionRules& rules = testing::originalRules()) {
  const EvolutionQuery query{
      .tables = &tables, .rules = &rules, .inputs = &inputs, .state = &state};
  return OriginalScoring{}.score(query, TableFixture::index(target), currentBest);
}

std::optional<Species> selectionOf(const EvolutionTables& tables, const EvolutionInputs& inputs,
                                   const EvolutionRules& rules = testing::originalRules()) {
  const EvolutionState state;
  const EvolutionQuery query{
      .tables = &tables, .rules = &rules, .inputs = &inputs, .state = &state};
  return OriginalSelection{}.select(query, OriginalScoring{});
}

// A target that asks for the stat given and nothing else, so that any weight and care pass.
void asksFor(TableFixture& fixture, std::string_view target, std::int32_t BaseStats::* stat,
             std::int32_t minimum) {
  assets::EvolutionRequirementEntry& row = fixture.requirement(target);
  BaseStats wanted{.hp = NoRequirement,
                   .mp = NoRequirement,
                   .offense = NoRequirement,
                   .defense = NoRequirement,
                   .speed = NoRequirement,
                   .brains = NoRequirement};
  wanted.*stat = minimum;
  row.hp = wanted.hp;
  row.mp = wanted.mp;
  row.offense = wanted.offense;
  row.defense = wanted.defense;
  row.speed = wanted.speed;
  row.brain = wanted.brains;
}

NaturalCheck checkAt(const Evolution& evolution, const EvolutionInputs& inputs,
                     std::int64_t hoursCrossed = 1) {
  ruleset::Random random{Seed};
  return evolution.checkNatural(inputs, {}, CheckSpan::hourly(hoursCrossed), random);
}

} // namespace

// --- When the checks happen and the baby rule (partner_impl.c: tickConditions;
// getFreshEvolutionTarget) ---

TEST_CASE("a baby becomes its in-training form once the counter reaches the baby's hours") {
  const Evolution evolution = TableFixture{}.makeEvolution();
  CHECK_FALSE(checkAt(evolution, inputsOf("bota", 5)).evolution);
  const NaturalCheck check = checkAt(evolution, inputsOf("bota", 6));
  REQUIRE(check.evolution);
  CHECK(valueOf(check.evolution).target == TableFixture::index("koro"));
  CHECK(valueOf(check.evolution).reason == EvolutionReason::Normal);
  // The level changed, so the counter starts again.
  CHECK(valueOf(check.evolution).evolutionTimer == 0);
  CHECK(check.evolutionTimer == 0);
}

TEST_CASE("each level checks from its own hours, and the ultimate never") {
  TableFixture fixture;
  fixture.setPath("koro", {"agum"});
  fixture.setPath("agum", {"species_b"});
  fixture.setPath("species_b", {"species_e"});
  asksFor(fixture, "agum", &BaseStats::brains, FavouredStat);
  asksFor(fixture, "species_b", &BaseStats::offense, 0);
  asksFor(fixture, "species_e", &BaseStats::offense, 0);
  const Evolution evolution = fixture.makeEvolution();
  const auto withOffense = [](std::string_view id, std::int32_t timer) {
    EvolutionInputs inputs = inputsOf(id, timer);
    inputs.stats.offense = 10;
    return inputs;
  };
  CHECK_FALSE(checkAt(evolution, inputsOf("koro", 23)).evolution);
  CHECK(checkAt(evolution, inputsOf("koro", 24)).evolution);
  CHECK_FALSE(checkAt(evolution, withOffense("agum", 71)).evolution);
  CHECK(checkAt(evolution, withOffense("agum", 72)).evolution);
  CHECK_FALSE(checkAt(evolution, withOffense("species_b", 143)).evolution);
  CHECK(checkAt(evolution, withOffense("species_b", 144)).evolution);
  CHECK_FALSE(checkAt(evolution, withOffense("species_e", 1000)).evolution);
}

TEST_CASE("no natural evolution while the tamer is busy") {
  const Evolution evolution = TableFixture{}.makeEvolution();
  EvolutionInputs inputs = inputsOf("bota", 6);
  inputs.tamerFree = false;
  const NaturalCheck check = checkAt(evolution, inputs);
  CHECK_FALSE(check.evolution);
  CHECK(check.evolutionTimer == 6);
}

TEST_CASE(
    "quirk unlisted_baby_restarts_timer: a baby with no in-training form starts its counter again") {
  EvolutionRules rules = testing::originalRules();
  rules.babyTargets.erase(rules.babyTargets.find("bota"));
  const Evolution evolution = TableFixture{}.makeEvolution(rules);
  const NaturalCheck check = checkAt(evolution, inputsOf("bota", 8));
  CHECK_FALSE(check.evolution);
  CHECK(check.evolutionTimer == 0);

  rules.quirks = testing::originalRulesWithQuirk(UnlistedBabyRestartsTimer, false).quirks;
  const Evolution fixed = TableFixture{}.makeEvolution(rules);
  CHECK(checkAt(fixed, inputsOf("bota", 8)).evolutionTimer == 8);
}

TEST_CASE(
    "a check over several hours evolves at the first hour that passes and keeps the hours after it") {
  TableFixture fixture;
  fixture.setPath("koro", {"agum"});
  asksFor(fixture, "agum", &BaseStats::brains, FavouredStat);
  const Evolution evolution = fixture.makeEvolution();
  // Hours 21 to 30 are crossed: the in-training check passes at hour 24, and 6 hours follow it.
  const NaturalCheck check = checkAt(evolution, inputsOf("koro", 30), 10);
  REQUIRE(check.evolution);
  CHECK(valueOf(check.evolution).target == TableFixture::index("agum"));
  CHECK(check.evolutionTimer == 6);
  CHECK(valueOf(check.evolution).evolutionTimer == 6);
  // One hour crossed checks only the counter's own value.
  CHECK(checkAt(evolution, inputsOf("koro", 30)).evolutionTimer == 0);
}

// --- Candidate scoring (evolution.c: calculateRequirementScore) ---

TEST_CASE("a target scores a point each for care, weight, stats and one bonus") {
  TableFixture fixture;
  asksFor(fixture, "species_b", &BaseStats::hp, 100);
  assets::EvolutionRequirementEntry& row = fixture.requirement("species_b");
  row.care = 5;
  row.flags = CareIsMaximumFlag;
  row.weight = 30;
  row.discipline = 50;
  const EvolutionTables tables = fixture.build();

  EvolutionInputs inputs = inputsOf("agum");
  inputs.stats.hp = 1000;
  inputs.careMistakes = 3;
  inputs.weight = 30;
  inputs.discipline = 50;
  CHECK(scoreOf(tables, inputs, "species_b") == 4);

  SUBCASE("care mistakes: at most the requirement with the flag") {
    inputs.careMistakes = 5;
    CHECK(scoreOf(tables, inputs, "species_b") == 4);
    inputs.careMistakes = 6;
    CHECK(scoreOf(tables, inputs, "species_b") == 3);
  }
  SUBCASE("care mistakes: at least the requirement without it") {
    fixture.requirement("species_b").flags = 0;
    const EvolutionTables minimum = fixture.build();
    CHECK(scoreOf(minimum, inputs, "species_b") == 3);
    inputs.careMistakes = 6;
    CHECK(scoreOf(minimum, inputs, "species_b") == 4);
  }
  SUBCASE("weight: within the margin either way") {
    for (const std::int32_t weight : {25, 35}) {
      inputs.weight = weight;
      CHECK(scoreOf(tables, inputs, "species_b") == 4);
    }
    for (const std::int32_t weight : {24, 36}) {
      inputs.weight = weight;
      CHECK(scoreOf(tables, inputs, "species_b") == 3);
    }
  }
  SUBCASE("stats: HP and MP are compared divided by 10") {
    inputs.stats.hp = 999;
    CHECK(scoreOf(tables, inputs, "species_b") == 3);
  }
  SUBCASE("bonus: the previous form instead of the discipline") {
    inputs.discipline = 49;
    CHECK(scoreOf(tables, inputs, "species_b") == 3);
    fixture.requirement("species_b").digimon = "agum";
    CHECK(scoreOf(fixture.build(), inputs, "species_b") == 4);
  }
}

TEST_CASE("the battles bonus is a minimum, or a maximum with the flag") {
  TableFixture fixture;
  fixture.requirement("species_b").battles = 10;
  const EvolutionTables minimum = fixture.build();
  fixture.requirement("species_b").flags = BattlesAreMaximumFlag;
  const EvolutionTables maximum = fixture.build();
  EvolutionInputs inputs = inputsOf("agum");
  inputs.weight = 100;
  inputs.battles = 10;
  CHECK(scoreOf(minimum, inputs, "species_b") == 3);
  CHECK(scoreOf(maximum, inputs, "species_b") == 3);
  inputs.battles = 11;
  CHECK(scoreOf(minimum, inputs, "species_b") == 3);
  CHECK(scoreOf(maximum, inputs, "species_b") == 2);
  inputs.battles = 9;
  CHECK(scoreOf(minimum, inputs, "species_b") == 2);
  CHECK(scoreOf(maximum, inputs, "species_b") == 3);
}

TEST_CASE("the happiness and learned moves bonuses") {
  TableFixture fixture;
  fixture.requirement("species_b").happiness = 60;
  fixture.requirement("species_d").techs = 12;
  const EvolutionTables tables = fixture.build();
  EvolutionInputs inputs = inputsOf("agum");
  inputs.weight = 100;
  CHECK(scoreOf(tables, inputs, "species_b") == 2);
  CHECK(scoreOf(tables, inputs, "species_d") == 2);
  inputs.happiness = 60;
  inputs.learnedMoves = 12;
  CHECK(scoreOf(tables, inputs, "species_b") == 3);
  CHECK(scoreOf(tables, inputs, "species_d") == 3);
}

TEST_CASE(
    "a rookie target scores its stat point when the partner's highest stat is one it favours") {
  TableFixture fixture;
  asksFor(fixture, "agum", &BaseStats::offense, FavouredStat);
  const EvolutionTables tables = fixture.build();
  EvolutionInputs inputs = inputsOf("koro");
  inputs.weight = 100;
  inputs.stats = {.hp = 500, .mp = 0, .offense = 60, .defense = 10, .speed = 10, .brains = 10};
  // HP counts divided by 10: 500 is 50, below the offense.
  CHECK(scoreOf(tables, inputs, "agum") == 2);
  inputs.stats.speed = 61;
  CHECK(scoreOf(tables, inputs, "agum") == 1);
}

TEST_CASE("quirk rookie_stat_tie_picks_last: on a tie of highest stats the last one counts") {
  TableFixture fixture;
  asksFor(fixture, "agum", &BaseStats::offense, FavouredStat);
  const EvolutionTables tables = fixture.build();
  EvolutionInputs inputs = inputsOf("koro");
  inputs.weight = 100;
  inputs.stats = {.hp = 0, .mp = 0, .offense = 60, .defense = 0, .speed = 60, .brains = 0};
  CHECK(scoreOf(tables, inputs, "agum") == 1);
  const EvolutionRules first = testing::originalRulesWithQuirk(RookieStatTiePicksLast, false);
  CHECK(scoreOf(tables, inputs, "agum", {}, std::nullopt, first) == 2);
}

TEST_CASE(
    "against a best candidate, a passing target the partner was already raised as is dropped") {
  const EvolutionTables tables = TableFixture{}.build();
  const EvolutionInputs inputs = inputsOf("agum");
  const Species speciesB = TableFixture::index("species_b");
  const Species speciesD = TableFixture::index("species_d");
  CHECK(scoreOf(tables, inputs, "species_b") == 3);
  EvolutionState raisedB;
  raisedB.raised.insert(speciesB);
  CHECK(scoreOf(tables, inputs, "species_b", raisedB) == 3);
  CHECK(scoreOf(tables, inputs, "species_b", raisedB, speciesD) == 0);
  CHECK(scoreOf(tables, inputs, "species_d", raisedB, speciesB) == 4);
  EvolutionState raisedBoth = raisedB;
  raisedBoth.raised.insert(speciesD);
  CHECK(scoreOf(tables, inputs, "species_b", raisedBoth, speciesD) == 3);
}

// --- Choosing among candidates (getInTrainingEvolutionTarget, getRookie/ChampionEvolutionTarget)
// ---

TEST_CASE("an in-training partner takes the candidate with the most points, the first on a tie") {
  TableFixture fixture;
  fixture.setPath("koro", {"agum", "species_c", "kune"});
  for (const std::string_view rookie : {"agum", "species_c", "kune"}) {
    asksFor(fixture, rookie, &BaseStats::brains, FavouredStat);
  }
  fixture.requirement("species_c").discipline = 10;
  EvolutionInputs inputs = inputsOf("koro", 24);
  inputs.discipline = 10;
  CHECK(selectionOf(fixture.build(), inputs) == TableFixture::index("species_c"));
  inputs.discipline = 0;
  CHECK(selectionOf(fixture.build(), inputs) == TableFixture::index("agum"));
  // No candidate reaches the passing points.
  inputs.weight = 100;
  CHECK_FALSE(selectionOf(fixture.build(), inputs));
}

TEST_CASE(
    "rookies and champions take the passing candidate with the best average of the stats it asks for") {
  TableFixture fixture;
  fixture.setPath("agum", {"species_b", "aird"});
  asksFor(fixture, "species_b", &BaseStats::offense, 50);
  asksFor(fixture, "aird", &BaseStats::speed, 10);
  EvolutionInputs inputs = inputsOf("agum", 72);
  inputs.stats = {.hp = 0, .mp = 0, .offense = 100, .defense = 0, .speed = 150, .brains = 0};
  CHECK(selectionOf(fixture.build(), inputs) == TableFixture::index("aird"));
  inputs.stats.speed = 99;
  CHECK(selectionOf(fixture.build(), inputs) == TableFixture::index("species_b"));
  // A candidate that does not pass is not averaged.
  inputs.stats.offense = 49;
  CHECK(selectionOf(fixture.build(), inputs) == TableFixture::index("aird"));
}

TEST_CASE(
    "quirk selection_average_carries_over: a candidate that is not the best leaves its sum to the next") {
  TableFixture fixture;
  fixture.setPath("agum", {"species_b", "species_d", "aird"});
  asksFor(fixture, "species_b", &BaseStats::offense, 50);
  asksFor(fixture, "species_d", &BaseStats::defense, 10);
  asksFor(fixture, "aird", &BaseStats::speed, 10);
  EvolutionInputs inputs = inputsOf("agum", 72);
  // species_b averages 100 and becomes the best; species_d's 50 stays in the sum, so aird's 120
  // averages 85.
  inputs.stats = {.hp = 0, .mp = 0, .offense = 100, .defense = 50, .speed = 120, .brains = 0};
  CHECK(selectionOf(fixture.build(), inputs) == TableFixture::index("species_b"));
  const EvolutionRules apart = testing::originalRulesWithQuirk(SelectionAverageCarriesOver, false);
  CHECK(selectionOf(fixture.build(), inputs, apart) == TableFixture::index("aird"));
}

TEST_CASE("a rookie with no candidate becomes the fallback form after the fallback hours") {
  const Evolution evolution = TableFixture{}.makeEvolution();
  CHECK_FALSE(checkAt(evolution, inputsOf("agum", 95)).evolution);
  const NaturalCheck check = checkAt(evolution, inputsOf("agum", 96));
  REQUIRE(check.evolution);
  CHECK(valueOf(check.evolution).target == TableFixture::index("nume"));
  CHECK(valueOf(check.evolution).reason == EvolutionReason::Normal);
}

} // namespace dwe::game::evolution
