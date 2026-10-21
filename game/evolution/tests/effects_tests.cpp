#include <dwe/game/evolution/evolution.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <set>

#include "fixture.hpp"

namespace dwe::game::evolution {

namespace {

using testing::inputsOf;
using testing::TableFixture;
using testing::valueOf;

EvolutionResult evolveTo(const Evolution& evolution, const EvolutionInputs& inputs, std::string_view target,
                         const EvolutionState& state = {}) {
    const auto result = evolution.evolveByScript(inputs, state, TableFixture::index(target));
    REQUIRE(result);
    return valueOf(result);
}

} // namespace

// --- Stat gains (evl.c: EVL_applyEvolution, EVL_scaleBaseStats) ---

TEST_CASE("each stat that reaches the reference gains a tenth of it, and one below becomes the average") {
    const Evolution evolution = TableFixture{}.makeEvolution();
    EvolutionInputs inputs = inputsOf("agum", 40);
    inputs.stats = {.hp = 1200, .mp = 400, .offense = 100, .defense = 50, .speed = 999, .brains = 0};
    inputs.weight = 7;
    inputs.careMistakes = 3;
    inputs.battles = 5;
    const EvolutionResult result = evolveTo(evolution, inputs, "grey");
    CHECK(result.statChange ==
          BaseStats{.hp = 100, .mp = 300, .offense = 10, .defense = 25, .speed = 10, .brains = 50});
    CHECK_FALSE(result.restoredStats);
    CHECK(result.weight == testing::FixtureDefaultWeight);
    CHECK(result.careMistakes == 0);
    CHECK(result.battles == 0);
    CHECK(result.evolutionTimer == 0);
    CHECK(result.reason == EvolutionReason::Script);

    // The change is not capped: the partner caps it when it adds it.
    inputs.stats.hp = 9990;
    CHECK(evolveTo(evolution, inputs, "grey").statChange.hp == 100);
}

TEST_CASE("an evolution within the level keeps the counter") {
    const Evolution evolution = TableFixture{}.makeEvolution();
    CHECK(evolveTo(evolution, inputsOf("grey", 50), "tyra").evolutionTimer == 50);
}

TEST_CASE("the listed forms and those below the scaled level multiply the stats by the row's brains, as a byte") {
    TableFixture fixture;
    fixture.gains("nume").brains = 5;
    fixture.gains("devi").brains = 200;
    const Evolution evolution = fixture.makeEvolution();
    EvolutionInputs inputs = inputsOf("agum");
    inputs.stats = {.hp = 1000, .mp = 500, .offense = 100, .defense = 60, .speed = 40, .brains = 20};
    CHECK(evolveTo(evolution, inputs, "nume").statChange ==
          BaseStats{.hp = -500, .mp = -250, .offense = -50, .defense = -30, .speed = -20, .brains = -10});
    // 200 read as a signed byte is -56: the offense goes from 100 to -560.
    CHECK(evolveTo(evolution, inputs, "devi").statChange.offense == -660);

    EvolutionInputs baby = inputsOf("bota");
    baby.stats = {.hp = 10, .mp = 10, .offense = 1, .defense = 1, .speed = 1, .brains = 1};
    CHECK(evolveTo(evolution, baby, "koro").statChange ==
          BaseStats{.hp = 90, .mp = 90, .offense = 9, .defense = 9, .speed = 9, .brains = 9});
}

TEST_CASE("an ultimate adds lifetime, and a paused script gives the tamer level chance") {
    const Evolution evolution = TableFixture{}.makeEvolution();
    EvolutionInputs inputs = inputsOf("grey");
    CHECK(evolveTo(evolution, inputs, "mtgr").lifetimeBonusHours == 96);
    CHECK(evolveTo(evolution, inputs, "tyra").lifetimeBonusHours == 0);
    CHECK(evolveTo(evolution, inputs, "mtgr").tamerLevelChance == 0);
    inputs.scriptPaused = true;
    CHECK(evolveTo(evolution, inputs, "mtgr").tamerLevelChance == 100);
    CHECK(evolveTo(evolution, inputsOf("agum"), "grey").tamerLevelChance == 0);
    EvolutionInputs rookie = inputsOf("agum");
    rookie.scriptPaused = true;
    CHECK(evolveTo(evolution, rookie, "grey").tamerLevelChance == 20);
    EvolutionInputs inTraining = inputsOf("koro");
    inTraining.scriptPaused = true;
    CHECK(evolveTo(evolution, inTraining, "agum").tamerLevelChance == 0);
}

TEST_CASE("a target whose stats row loads another form becomes that form") {
    TableFixture fixture;
    fixture.gains("tyra").targetDigimon = "grey";
    CHECK(evolveTo(fixture.makeEvolution(), inputsOf("agum"), "tyra").target == TableFixture::index("grey"));
}

// --- Moves (evl.c: EVL_applyEvolution) ---

TEST_CASE("the form learns the weakest move of its specialty's block and equips it with its finisher") {
    TableFixture fixture;
    const EvolutionResult result = evolveTo(fixture.makeEvolution(), inputsOf("agum"), "grey");
    // The block of specialty 0 is moves 0 to 8: move_1 (power 11) is weaker than move_2 (power 12).
    CHECK(result.learnedMove == 1);
    CHECK(result.equippedMoves == EquippedMoves{1, std::nullopt, std::nullopt, 2});

    SUBCASE("a move of power 0 is never the weakest") {
        fixture.moves.moves.at(1).power = 0;
        CHECK(evolveTo(fixture.makeEvolution(), inputsOf("agum"), "grey").learnedMove == 2);
    }
    SUBCASE("below the basic slot level the first slot holds the basic attack") {
        const EvolutionResult koro = evolveTo(fixture.makeEvolution(), inputsOf("bota"), "koro");
        CHECK(koro.learnedMove == 1);
        CHECK(koro.equippedMoves.front() == 0);
    }
    SUBCASE("a specialty with no move of its block learns nothing") {
        fixture.form("grey").special.front() = 1;
        const EvolutionResult none = evolveTo(fixture.makeEvolution(), inputsOf("agum"), "grey");
        CHECK_FALSE(none.learnedMove);
        CHECK_FALSE(none.equippedMoves.front());
    }
}

// --- Rebirth (evolution.c: reincarnatePartner; dooa.c) ---

TEST_CASE("rebirth turns the partner into one of the babies, with the basic attack equipped") {
    const Evolution evolution = TableFixture{}.makeEvolution();
    EvolutionInputs inputs = inputsOf("mtgr", 90);
    inputs.stats.hp = 3000;
    ruleset::Random random{3};
    std::set<Species> babies;
    constexpr std::int32_t Rebirths = 100;
    for (std::int32_t i = 0; i < Rebirths; ++i) {
        const auto result = evolution.rebirth(inputs, random);
        REQUIRE(result);
        CHECK(valueOf(result).reason == EvolutionReason::Rebirth);
        CHECK(valueOf(result).statChange == BaseStats{});
        CHECK_FALSE(valueOf(result).restoredStats);
        CHECK(valueOf(result).equippedMoves == EquippedMoves{0, std::nullopt, std::nullopt, std::nullopt});
        babies.insert(valueOf(result).target);
    }
    CHECK(babies == std::set<Species>{TableFixture::index("bota"), TableFixture::index("puni"),
                                      TableFixture::index("poyo"), TableFixture::index("yura")});
}

} // namespace dwe::game::evolution
