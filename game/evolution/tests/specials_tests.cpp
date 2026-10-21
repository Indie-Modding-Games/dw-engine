#include <dwe/game/evolution/evolution.hpp>

#include <doctest/doctest.h>

#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include "fixture.hpp"

namespace dwe::game::evolution {

namespace {

using testing::errorOf;
using testing::inputsOf;
using testing::TableFixture;
using testing::valueOf;
using testing::withRolls;

constexpr std::uint64_t Seed = 11;

Evolution alwaysRolling(const TableFixture& fixture = TableFixture{}) {
    return fixture.makeEvolution(withRolls(testing::originalRules(), true));
}

std::optional<Species> wakeUpTarget(const Evolution& evolution, const EvolutionInputs& inputs) {
    ruleset::Random random{Seed};
    const auto result = evolution.wakeUp(inputs, {}, random);
    if (result) {
        CHECK(valueOf(result).reason == EvolutionReason::Special);
        return valueOf(result).target;
    }
    return std::nullopt;
}

std::optional<Species> praiseScoldTarget(const Evolution& evolution, const EvolutionInputs& inputs,
                                         const PraiseScold& event) {
    ruleset::Random random{Seed};
    const auto result = evolution.praiseOrScold(inputs, {}, event, random);
    return result ? std::optional<Species>{valueOf(result).target} : std::nullopt;
}

NaturalCheck checkAt(const Evolution& evolution, const EvolutionInputs& inputs) {
    ruleset::Random random{Seed};
    return evolution.checkNatural(inputs, {}, 1, random);
}

constexpr PraiseScold Praise{.scolded = false, .disciplineBefore = 50, .happinessBefore = 50};

} // namespace

// --- Special evolutions (evolution.c: handleSpecialEvolutions) ---

TEST_CASE("waking up: the listed forms at full discipline and happiness and rested take the discipline special") {
    const Evolution evolution = alwaysRolling();
    EvolutionInputs inputs = inputsOf("bird");
    inputs.discipline = 100;
    inputs.happiness = 100;
    CHECK(wakeUpTarget(evolution, inputs) == TableFixture::index("aird"));
    inputs.species = TableFixture::index("sead");
    CHECK(wakeUpTarget(evolution, inputs) == TableFixture::index("aird"));
    inputs.tiredness = 1;
    CHECK_FALSE(wakeUpTarget(evolution, inputs));
    inputs.tiredness = 0;
    inputs.species = TableFixture::index("species_b");
    CHECK_FALSE(wakeUpTarget(evolution, inputs));
}

TEST_CASE("waking up: the battles, defense and map specials") {
    const Evolution evolution = alwaysRolling();
    EvolutionInputs vegi = inputsOf("vegi");
    vegi.discipline = 100;
    vegi.battles = 49;
    CHECK_FALSE(wakeUpTarget(evolution, vegi));
    vegi.battles = 50;
    CHECK(wakeUpTarget(evolution, vegi) == TableFixture::index("igam"));

    EvolutionInputs dori = inputsOf("dori");
    dori.discipline = 100;
    dori.stats.defense = 500;
    CHECK(wakeUpTarget(evolution, dori) == TableFixture::index("mono"));
    dori.discipline = 99;
    CHECK_FALSE(wakeUpTarget(evolution, dori));

    for (const std::string_view from : {"koro", "toko", "tuno", "tane"}) {
        EvolutionInputs inputs = inputsOf(from);
        CHECK_FALSE(wakeUpTarget(evolution, inputs));
        inputs.map = "mayo02";
        CHECK(wakeUpTarget(evolution, inputs) == TableFixture::index("kune"));
    }
}

TEST_CASE("a special asks for a map by its id, so a pack's map can be one") {
    EvolutionRules rules = testing::originalRules();
    rules.specials.wakeUp.at(3).requirements.front().mapId = "my_mod:meadow";
    const Evolution evolution = TableFixture{}.makeEvolution(withRolls(rules, true));
    EvolutionInputs inputs = inputsOf("koro");
    inputs.map = "mayo02";
    CHECK_FALSE(wakeUpTarget(evolution, inputs));
    inputs.map = "my_mod:meadow";
    CHECK(wakeUpTarget(evolution, inputs) == TableFixture::index("kune"));
    inputs.map.clear();
    CHECK_FALSE(wakeUpTarget(evolution, inputs));
}

TEST_CASE("a special needs its roll and a free tamer") {
    EvolutionInputs inputs = inputsOf("koro");
    inputs.map = "mayo02";
    const Evolution never = TableFixture{}.makeEvolution(withRolls(testing::originalRules(), false));
    CHECK_FALSE(wakeUpTarget(never, inputs));
    const Evolution always = alwaysRolling();
    inputs.tamerFree = false;
    CHECK_FALSE(wakeUpTarget(always, inputs));
    // With the real roll some of many tries pass and some do not.
    const Evolution original = TableFixture{}.makeEvolution();
    inputs.tamerFree = true;
    ruleset::Random random{Seed};
    std::int32_t passed = 0;
    constexpr std::int32_t Tries = 200;
    for (std::int32_t i = 0; i < Tries; ++i) {
        passed += original.wakeUp(inputs, {}, random) ? 1 : 0;
    }
    CHECK(passed > 0);
    CHECK(passed < Tries);
}

TEST_CASE("one roll for every rule, and the last rule that holds wins") {
    EvolutionRules rules = withRolls(testing::originalRules(), true);
    rules.specials.wakeUp.push_back(
        {.target = "species_d", .rollBelow = std::nullopt, .from = {"bird"}, .requirements = {}});
    const Evolution evolution = TableFixture{}.makeEvolution(rules);
    EvolutionInputs inputs = inputsOf("bird");
    inputs.discipline = 100;
    inputs.happiness = 100;
    CHECK(wakeUpTarget(evolution, inputs) == TableFixture::index("species_d"));
}

TEST_CASE("praise or scold: the timer, scold and late-champion specials") {
    const Evolution evolution = alwaysRolling();
    EvolutionInputs shel = inputsOf("shel", 200);
    CHECK(praiseScoldTarget(evolution, shel, Praise) == TableFixture::index("sira"));
    shel.evolutionTimer = 201;
    CHECK_FALSE(praiseScoldTarget(evolution, shel, Praise));

    EvolutionInputs speciesB = inputsOf("species_b", 240);
    CHECK(praiseScoldTarget(evolution, speciesB, Praise) == TableFixture::index("veda"));
    speciesB.evolutionTimer = 239;
    CHECK_FALSE(praiseScoldTarget(evolution, speciesB, Praise));

    // partner_impl.c: handlePraiseScold: a scold of a rookie with no discipline and the lowest happiness.
    const EvolutionInputs agum = inputsOf("agum");
    const PraiseScold scold{.scolded = true, .disciplineBefore = 0, .happinessBefore = -100};
    CHECK(praiseScoldTarget(evolution, agum, scold) == TableFixture::index("nani"));
    CHECK_FALSE(praiseScoldTarget(evolution, agum, {.scolded = false, .disciplineBefore = 0, .happinessBefore = -100}));
    CHECK_FALSE(praiseScoldTarget(evolution, agum, {.scolded = true, .disciplineBefore = 1, .happinessBefore = -100}));
    CHECK_FALSE(praiseScoldTarget(evolution, inputsOf("species_b"), scold));
}

TEST_CASE("a champion at the timer hours runs the praise and scold specials, without the scold ones") {
    const Evolution evolution = alwaysRolling();
    const NaturalCheck check = checkAt(evolution, inputsOf("species_b", 360));
    REQUIRE(check.evolution);
    CHECK(valueOf(check.evolution).target == TableFixture::index("veda"));
    CHECK(valueOf(check.evolution).reason == EvolutionReason::Special);
}

TEST_CASE("quirk timer_special_overrides_normal: at the timer hours a failed special also drops the normal target") {
    TableFixture fixture;
    fixture.setPath("species_b", {"species_e"});
    fixture.requirement("species_e").offense = 0;
    EvolutionInputs inputs = inputsOf("species_b", 360);
    inputs.stats.offense = 10;
    const EvolutionRules never = withRolls(testing::originalRules(), false);
    CHECK_FALSE(checkAt(fixture.makeEvolution(never), inputs).evolution);
    // One hour later the normal check is back.
    inputs.evolutionTimer = 361;
    CHECK(checkAt(fixture.makeEvolution(never), inputs).evolution);

    inputs.evolutionTimer = 360;
    EvolutionRules fixed = withRolls(testing::originalRulesWithQuirk(TimerSpecialOverridesNormal, false), false);
    const NaturalCheck check = checkAt(fixture.makeEvolution(fixed), inputs);
    REQUIRE(check.evolution);
    CHECK(valueOf(check.evolution).target == TableFixture::index("species_e"));
    CHECK(valueOf(check.evolution).reason == EvolutionReason::Normal);
}

// --- The virus form (partner_impl.c: tickConditions; partner.c: callDigimonRoutine) ---

TEST_CASE("the partner's full virus bar turns it into the virus form, keeping its form and stats") {
    const Evolution evolution = TableFixture{}.makeEvolution();
    EvolutionInputs inputs = inputsOf("species_b", 50);
    inputs.stats = {.hp = 1000, .mp = 900, .offense = 100, .defense = 90, .speed = 80, .brains = 70};
    inputs.tamerFree = false;
    const auto result = evolution.fillVirusBar(inputs, {});
    REQUIRE(result);
    CHECK(valueOf(result).target == TableFixture::index("scum"));
    CHECK(valueOf(result).reason == EvolutionReason::Virus);
    CHECK(valueOf(result).restartsVirusBar);
    CHECK(valueOf(result).evolutionTimer == 50);
    CHECK(valueOf(valueOf(result).virusBackup) == VirusBackup{.species = inputs.species, .stats = inputs.stats});

    inputs.species = TableFixture::index("scum");
    CHECK_FALSE(evolution.fillVirusBar(inputs, {}));
    // A ruleset whose care section names no virus form.
    inputs = inputsOf("species_b", 50);
    inputs.virusForm = std::nullopt;
    CHECK_FALSE(evolution.fillVirusBar(inputs, {}));
}

TEST_CASE("back from the virus form the partner is its kept form, with stats rebuilt from the kept ones") {
    const Evolution evolution = TableFixture{}.makeEvolution();
    EvolutionInputs inputs = inputsOf("scum");
    inputs.stats = {.hp = 600, .mp = 400, .offense = 100, .defense = 100, .speed = 50, .brains = 20};
    CHECK_FALSE(evolution.returnFromVirus(inputs, {}));
    inputs.virusBackup =
        VirusBackup{.species = TableFixture::index("species_b"),
                    .stats = {.hp = 1000, .mp = 1000, .offense = 100, .defense = 100, .speed = 100, .brains = 100}};
    const auto result = evolution.returnFromVirus(inputs, {});
    REQUIRE(result);
    CHECK(valueOf(result).target == TableFixture::index("species_b"));
    CHECK(valueOf(result).reason == EvolutionReason::Script);
    // evl.c: EVL_scaleBaseStats: kept + (current - kept / 2) / 2, restored as they are rather than added.
    CHECK(valueOf(result).restoredStats ==
          BaseStats{.hp = 1050, .mp = 950, .offense = 125, .defense = 125, .speed = 100, .brains = 85});
    CHECK(valueOf(result).statChange == BaseStats{});
    CHECK(valueOf(result).restartsVirusBar);
    CHECK_FALSE(valueOf(result).virusBackup);
}

TEST_CASE("the stats rebuilt from the virus backup are left to the session to clamp") {
    const Evolution evolution = TableFixture{}.makeEvolution();
    EvolutionInputs inputs = inputsOf("scum");
    inputs.stats = {.hp = 9999, .mp = 9999, .offense = 999, .defense = 999, .speed = 999, .brains = 999};
    inputs.virusBackup = VirusBackup{.species = TableFixture::index("species_b"), .stats = inputs.stats};
    const auto result = evolution.returnFromVirus(inputs, {});
    REQUIRE(result);
    CHECK(valueOf(result).restoredStats ==
          BaseStats{.hp = 12499, .mp = 12499, .offense = 1249, .defense = 1249, .speed = 1249, .brains = 1249});
}

// --- Evolution items (item.c: handleEvoItems) ---

TEST_CASE("a fixed evolution item works at any level and gives no stats") {
    const Evolution evolution = TableFixture{}.makeEvolution();
    EvolutionInputs inputs = inputsOf("koro", 10);
    inputs.stats = {.hp = 100, .mp = 100, .offense = 10, .defense = 10, .speed = 10, .brains = 10};
    const auto result = evolution.useItem(inputs, {}, "giga_hand");
    REQUIRE(result);
    CHECK(valueOf(result).target == TableFixture::index("ggdr"));
    CHECK(valueOf(result).reason == EvolutionReason::Item);
    CHECK(valueOf(result).statChange == BaseStats{});
    CHECK(valueOf(result).lifetimeBonusHours == 0);
    CHECK(valueOf(result).evolutionItemUsed);
    CHECK(valueOf(result).evolutionTimer == 0);
    CHECK(valueOf(evolution.useItem(inputs, {}, "noble_mane")).target == TableFixture::index("panj"));
    CHECK(valueOf(evolution.useItem(inputs, {}, "metalbanana")).target == TableFixture::index("mtet"));
}

TEST_CASE("an item of the item table works only one level below its form") {
    TableFixture fixture;
    fixture.evolution.itemTargets.push_back({.item = "test_claws", .index = 71, .target = "species_b"});
    const Evolution evolution = fixture.makeEvolution();
    const auto result = evolution.useItem(inputsOf("agum"), {}, "test_claws");
    REQUIRE(result);
    CHECK(valueOf(result).target == TableFixture::index("species_b"));
    CHECK_FALSE(evolution.useItem(inputsOf("koro"), {}, "test_claws"));
    CHECK_FALSE(evolution.useItem(inputsOf("species_d"), {}, "test_claws"));
    CHECK_FALSE(evolution.useItem(inputsOf("agum"), {}, "meat"));
}

TEST_CASE("the evolution items' targets come from the item effects, by id, and only the evolution handler's") {
    TableFixture fixture;
    fixture.itemEffects.effects = {
        {.item = "test_claws", .index = 71, .handler = assets::ItemHandler::Evolution, .evolvesTo = "species_b"},
        {.item = "meat", .index = 0, .handler = assets::ItemHandler::Food, .evolvesTo = "species_b"},
        // A raw number outside the Digimon table leads nowhere.
        {.item = "odd_charm", .index = 72, .handler = assets::ItemHandler::Evolution, .evolvesTo = "250"},
    };
    const Evolution evolution = fixture.makeEvolution();
    CHECK(evolution.tables().itemTarget("test_claws") == TableFixture::index("species_b"));
    CHECK_FALSE(evolution.tables().itemTarget("meat"));
    CHECK_FALSE(evolution.tables().itemTarget("odd_charm"));
    const auto result = evolution.useItem(inputsOf("agum"), {}, "test_claws");
    REQUIRE(result);
    CHECK(valueOf(result).target == TableFixture::index("species_b"));
    CHECK(valueOf(result).reason == EvolutionReason::Item);
}

TEST_CASE("data/evolution's item_targets override the item effects item by item") {
    TableFixture fixture;
    fixture.itemEffects.effects = {
        {.item = "test_claws", .index = 71, .handler = assets::ItemHandler::Evolution, .evolvesTo = "species_b"},
        {.item = "tyra_scale", .index = 73, .handler = assets::ItemHandler::Evolution, .evolvesTo = "species_d"},
    };
    fixture.evolution.itemTargets.push_back({.item = "test_claws", .index = 71, .target = "nume"});
    const EvolutionTables tables = fixture.build();
    CHECK(tables.itemTarget("test_claws") == TableFixture::index("nume"));
    CHECK(tables.itemTarget("tyra_scale") == TableFixture::index("species_d"));
}

TEST_CASE("a form's move that is no move of the table, by id or by the original's number, is refused") {
    for (const std::string_view move : {"nothing", "200"}) {
        CAPTURE(move);
        TableFixture fixture;
        fixture.form("species_b").moves.at(3) = std::string{move};
        const auto tables = EvolutionTables::build(fixture.digimon, fixture.raise, assets::MoveTable{fixture.moves},
                                                   fixture.evolution, fixture.itemEffects);
        REQUIRE_FALSE(tables.has_value());
        CHECK(errorOf(tables).contains(std::format("has unknown move '{}'", move)));
    }
}

TEST_CASE("an item effect that names an unknown form is refused") {
    TableFixture fixture;
    fixture.itemEffects.effects = {
        {.item = "test_claws", .index = 71, .handler = assets::ItemHandler::Evolution, .evolvesTo = "nobody"},
    };
    const auto tables = EvolutionTables::build(fixture.digimon, fixture.raise, assets::MoveTable{fixture.moves},
                                               fixture.evolution, fixture.itemEffects);
    REQUIRE_FALSE(tables.has_value());
    CHECK(errorOf(tables).contains("item_effects: unknown digimon 'nobody'"));
}

TEST_CASE("quirk evolution_item_sticks: after an item, later evolutions give no stats either") {
    const EvolutionInputs inputs = inputsOf("agum");
    EvolutionState state;
    state.evolutionItemUsed = true;
    const auto sticking =
        TableFixture{}.makeEvolution().evolveByScript(inputs, state, TableFixture::index("species_b"));
    REQUIRE(sticking);
    CHECK(valueOf(sticking).statChange == BaseStats{});
    CHECK(valueOf(sticking).evolutionItemUsed);

    const Evolution fixed = TableFixture{}.makeEvolution(testing::originalRulesWithQuirk(EvolutionItemSticks, false));
    const auto result = fixed.evolveByScript(inputs, state, TableFixture::index("species_b"));
    REQUIRE(result);
    CHECK(valueOf(result).statChange != BaseStats{});
    CHECK_FALSE(valueOf(result).evolutionItemUsed);
}

} // namespace dwe::game::evolution
