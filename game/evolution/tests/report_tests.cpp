#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/strategies.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <ostream>

#include "fixture.hpp"

namespace dwe::game::evolution {

namespace {

using testing::inputsOf;
using testing::TableFixture;
using testing::valueOf;

constexpr std::uint64_t Seed = 11;

// What the periodic check does with the partner as it is: the target it evolves into, if any.
std::optional<Species> checkedTarget(const Evolution& evolution, const EvolutionInputs& inputs) {
  ruleset::Random random{Seed};
  const NaturalCheck check = evolution.checkNatural(inputs, {}, CheckSpan::hourly(1), random);
  return check.evolution ? std::optional{check.evolution->target} : std::nullopt;
}

std::int32_t scoreOf(const Evolution& evolution, const EvolutionInputs& inputs, Species target) {
  const EvolutionState state;
  const EvolutionQuery query{.tables = &evolution.tables(),
                             .rules = &evolution.rules(),
                             .inputs = &inputs,
                             .state = &state};
  return OriginalScoring{}.score(query, target, std::nullopt);
}

// A champion target asking every kind of thing: care at most 3, weight 30, offense 100 and HP 50
// (500 of the stat), and the bonus by the previous form, discipline 60, battles at most 10 and 20
// learned moves.
TableFixture championFixture() {
  TableFixture fixture;
  fixture.setPath("agum", {"species_b", "aird"});
  assets::EvolutionRequirementEntry& row = fixture.requirement("species_b");
  row.care = 3;
  row.weight = 30;
  row.hp = 50;
  row.offense = 100;
  row.digimon = "kune";
  row.discipline = 60;
  row.battles = 10;
  row.techs = 20;
  row.flags = CareIsMaximumFlag | BattlesAreMaximumFlag;
  // aird asks for a weight the partner never has, so it never passes.
  fixture.requirement("aird").weight = 500;
  return fixture;
}

// A partner meeting every group of championFixture's species_b.
EvolutionInputs meetingAll() {
  EvolutionInputs inputs = inputsOf("agum", 72);
  inputs.careMistakes = 2;
  inputs.weight = 31;
  inputs.stats = {.hp = 520, .mp = 0, .offense = 120, .defense = 0, .speed = 0, .brains = 0};
  inputs.discipline = 70;
  inputs.battles = 4;
  inputs.learnedMoves = 3;
  return inputs;
}

} // namespace

TEST_CASE("the report of a target lists each group as the scoring counts it") {
  const Evolution evolution = championFixture().makeEvolution();
  const Species target = TableFixture::index("species_b");
  const auto report = evolution.report(meetingAll(), {}, target);
  REQUIRE(report);
  const RequirementReport& all = valueOf(report);
  CHECK(all.careMistakes == 3);
  CHECK(all.careIsMaximum);
  CHECK(all.weight == 30);
  CHECK(all.weightMin == 25);
  CHECK(all.weightMax == 35);
  CHECK_FALSE(all.favouredStats);
  CHECK(all.stats.at(0).required == 50);
  CHECK(all.stats.at(0).value == 52);
  CHECK(all.stats.at(0).stat == 520);
  CHECK(all.stats.at(0).divisor == 10);
  CHECK(all.stats.at(2).required == 100);
  CHECK(all.stats.at(2).divisor == 1);
  CHECK(all.stats.at(4).required == NoRequirement);
  // Only the bonuses asked for, in the original's order.
  REQUIRE(all.bonus.size() == 4);
  CHECK(all.bonus.at(0).kind == BonusKind::From);
  CHECK(all.bonus.at(0).required == TableFixture::index("kune"));
  CHECK_FALSE(all.bonus.at(0).met);
  CHECK(all.bonus.at(1).kind == BonusKind::Discipline);
  CHECK(all.bonus.at(1).met);
  CHECK(all.bonus.at(2).kind == BonusKind::Battles);
  CHECK(all.bonus.at(2).isMaximum);
  CHECK(all.bonus.at(3).kind == BonusKind::LearnedMoves);
  CHECK_FALSE(all.bonus.at(3).met);
  CHECK(all.careMet);
  CHECK(all.weightMet);
  CHECK(all.statsMet);
  CHECK(all.bonusMet);
  CHECK(all.points == 4);
  CHECK(all.passPoints == 3);
  CHECK(all.passes());
}

TEST_CASE("the report's points are the score, for every mix of met and unmet groups") {
  const Evolution evolution = championFixture().makeEvolution();
  const Species target = TableFixture::index("species_b");
  // Each bit breaks one group: care, weight, stats, bonus.
  for (int broken = 0; broken < 16; ++broken) {
    CAPTURE(broken);
    EvolutionInputs inputs = meetingAll();
    if ((broken & 1) != 0) {
      inputs.careMistakes = 4;
    }
    if ((broken & 2) != 0) {
      inputs.weight = 36;
    }
    if ((broken & 4) != 0) {
      inputs.stats.hp = 499;
    }
    if ((broken & 8) != 0) {
      inputs.discipline = 59;
      inputs.battles = 11;
    }
    const auto report = evolution.report(inputs, {}, target);
    REQUIRE(report);
    CHECK(valueOf(report).careMet == ((broken & 1) == 0));
    CHECK(valueOf(report).weightMet == ((broken & 2) == 0));
    CHECK(valueOf(report).statsMet == ((broken & 4) == 0));
    CHECK(valueOf(report).bonusMet == ((broken & 8) == 0));
    CHECK(valueOf(report).points == scoreOf(evolution, inputs, target));
    CHECK(valueOf(report).points == 4 - std::popcount(static_cast<unsigned>(broken)));
  }
}

TEST_CASE("a rookie target reports its favoured stats against the partner's highest") {
  TableFixture fixture;
  fixture.setPath("koro", {"agum"});
  assets::EvolutionRequirementEntry& row = fixture.requirement("agum");
  row.offense = FavouredStat;
  row.speed = FavouredStat;
  const Evolution evolution = fixture.makeEvolution();
  EvolutionInputs inputs = inputsOf("koro", 24);
  inputs.weight = 100;
  inputs.stats = {.hp = 500, .mp = 0, .offense = 60, .defense = 70, .speed = 10, .brains = 10};
  const Species target = TableFixture::index("agum");
  auto report = evolution.report(inputs, {}, target);
  REQUIRE(report);
  CHECK(valueOf(report).favouredStats);
  CHECK(valueOf(report).highestStat == 3);
  CHECK_FALSE(valueOf(report).statsMet);
  CHECK(valueOf(report).points == scoreOf(evolution, inputs, target));
  inputs.stats.speed = 80;
  report = evolution.report(inputs, {}, target);
  REQUIRE(report);
  CHECK(valueOf(report).highestStat == 4);
  CHECK(valueOf(report).stats.at(4).met);
  CHECK_FALSE(valueOf(report).stats.at(2).met);
  CHECK(valueOf(report).statsMet);
  CHECK(valueOf(report).points == scoreOf(evolution, inputs, target));
}

TEST_CASE("a target without a requirement row has no report") {
  TableFixture fixture;
  fixture.evolution.requirements.erase(fixture.evolution.requirements.begin() +
                                       TableFixture::index("species_d"));
  const Evolution evolution = fixture.makeEvolution();
  CHECK_FALSE(evolution.report(inputsOf("agum"), {}, TableFixture::index("species_d")));
}

TEST_CASE("the outlook's next form is the one the check evolves into once it is due") {
  const Evolution evolution = championFixture().makeEvolution();
  const Species passing = TableFixture::index("species_b");

  SUBCASE("before the level's hours the check waits, the outlook says what it would pick") {
    EvolutionInputs inputs = meetingAll();
    inputs.evolutionTimer = 71;
    const EvolutionOutlook outlook = evolution.outlook(inputs, {});
    CHECK_FALSE(outlook.due());
    CHECK(valueOf(outlook.checkHours) == 72);
    CHECK(outlook.next == passing);
    CHECK_FALSE(checkedTarget(evolution, inputs));
  }
  SUBCASE("due: the next form is the check's") {
    const EvolutionInputs inputs = meetingAll();
    const EvolutionOutlook outlook = evolution.outlook(inputs, {});
    CHECK(outlook.due());
    REQUIRE(outlook.targets.size() == 2);
    CHECK(outlook.targets.at(0).target == passing);
    CHECK(outlook.targets.at(0).passes());
    CHECK_FALSE(outlook.targets.at(1).passes());
    CHECK(outlook.next == passing);
    CHECK(checkedTarget(evolution, inputs) == outlook.next);
  }
  SUBCASE("two groups short: nothing passes and nothing is picked") {
    EvolutionInputs inputs = meetingAll();
    inputs.weight = 50;
    inputs.careMistakes = 9;
    const EvolutionOutlook outlook = evolution.outlook(inputs, {});
    CHECK(outlook.targets.at(0).points == 2);
    CHECK_FALSE(outlook.targets.at(0).passes());
    CHECK_FALSE(outlook.next);
    CHECK_FALSE(checkedTarget(evolution, inputs));
  }
  SUBCASE("three of four still passes") {
    EvolutionInputs inputs = meetingAll();
    inputs.weight = 50;
    const EvolutionOutlook outlook = evolution.outlook(inputs, {});
    CHECK(outlook.targets.at(0).points == 3);
    CHECK(outlook.next == passing);
    CHECK(checkedTarget(evolution, inputs) == passing);
  }
}

TEST_CASE("with several passing targets the outlook picks as the check does") {
  TableFixture fixture;
  fixture.setPath("agum", {"species_b", "aird"});
  for (const std::string_view target : {"species_b", "aird"}) {
    fixture.requirement(target).care = 10;
    fixture.requirement(target).flags = CareIsMaximumFlag;
    fixture.requirement(target).weight = 20;
  }
  fixture.requirement("species_b").offense = 50;
  fixture.requirement("aird").speed = 10;
  const Evolution evolution = fixture.makeEvolution();
  EvolutionInputs inputs = inputsOf("agum", 72);
  inputs.weight = 20;
  inputs.stats = {.hp = 0, .mp = 0, .offense = 100, .defense = 0, .speed = 150, .brains = 0};
  // Both pass; the best average of the stats each asks for wins.
  EvolutionOutlook outlook = evolution.outlook(inputs, {});
  CHECK(std::ranges::all_of(outlook.targets, &RequirementReport::passes));
  CHECK(outlook.next == TableFixture::index("aird"));
  CHECK(checkedTarget(evolution, inputs) == outlook.next);
  inputs.stats.speed = 99;
  outlook = evolution.outlook(inputs, {});
  CHECK(outlook.next == TableFixture::index("species_b"));
  CHECK(checkedTarget(evolution, inputs) == outlook.next);
}

TEST_CASE("the outlook of an in-training partner prefers a form not raised yet, as the check") {
  TableFixture fixture;
  fixture.setPath("koro", {"agum", "kune"});
  fixture.requirement("agum").offense = FavouredStat;
  fixture.requirement("kune").offense = FavouredStat;
  const Evolution evolution = fixture.makeEvolution();
  // Both targets pass with care, weight (none asked) and the favoured offense.
  EvolutionInputs inputs = inputsOf("koro", 24);
  inputs.stats.offense = 10;
  EvolutionState state;
  state.raised.insert(TableFixture::index("agum"));
  const EvolutionOutlook outlook = evolution.outlook(inputs, state);
  CHECK(outlook.next == TableFixture::index("kune"));
  ruleset::Random random{Seed};
  const NaturalCheck check = evolution.checkNatural(inputs, state, CheckSpan::hourly(1), random);
  REQUIRE(check.evolution);
  CHECK(check.evolution->target == outlook.next);
}

TEST_CASE("a rookie's outlook names the fallback form and its hours") {
  const Evolution evolution = championFixture().makeEvolution();
  EvolutionInputs inputs = meetingAll();
  inputs.weight = 50;
  inputs.careMistakes = 9;
  inputs.evolutionTimer = 96;
  const EvolutionOutlook outlook = evolution.outlook(inputs, {});
  CHECK(outlook.fallback == TableFixture::index("nume"));
  CHECK(outlook.fallbackHours == 96);
  CHECK(outlook.next == TableFixture::index("nume"));
  CHECK(checkedTarget(evolution, inputs) == outlook.next);
}

TEST_CASE("a baby's outlook is its fixed form at the baby's hours; an ultimate has none") {
  const Evolution evolution = TableFixture{}.makeEvolution();
  const EvolutionOutlook baby = evolution.outlook(inputsOf("bota", 5), {});
  CHECK(baby.fixedTarget == TableFixture::index("koro"));
  CHECK(valueOf(baby.checkHours) == 6);
  CHECK_FALSE(baby.due());
  CHECK(baby.targets.empty());
  const EvolutionOutlook ultimate = evolution.outlook(inputsOf("veda", 500), {});
  CHECK_FALSE(ultimate.checkHours);
  CHECK_FALSE(ultimate.next);
  CHECK_FALSE(ultimate.due());
}

} // namespace dwe::game::evolution
