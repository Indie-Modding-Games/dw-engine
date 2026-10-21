#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/screens/battle_view.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

#include "game/battle/tests/fixture.hpp"

namespace {

using namespace battle_test;
using dwe::screens::NameKeys;

// The species of the battle fixture's fighters, at made-up indices.
dwe::assets::DigimonRow speciesRow(std::string id, std::int32_t index) {
    dwe::assets::DigimonRow row;
    row.id = std::move(id);
    row.index = index;
    return row;
}

dwe::assets::DigimonTable fixtureSpecies() {
    dwe::assets::DigimonTable table;
    table.digimon.push_back(speciesRow("hero", 3));
    table.digimon.push_back(speciesRow("foe", 12));
    return table;
}

dwe::assets::ItemTable fixtureItems() {
    dwe::assets::ItemEntry tonic;
    tonic.id = "tonic";
    tonic.index = 4;
    dwe::assets::ItemTable table;
    table.items.push_back(tonic);
    return table;
}

const dwe::screens::ScreenSetting* settingNamed(const dwe::screens::ScreenSettings& settings, std::string_view name) {
    const auto found = std::ranges::find(settings, name, &dwe::screens::ScreenSetting::name);
    return found == settings.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("the battle view names the fighters and shows their bars") {
    const auto species = fixtureSpecies();
    const NameKeys names{&species, nullptr, &testCatalog()};
    Battle battle = startBattle(duel(evenStats(900, 100), evenStats(700, 80)), 3);

    const auto view = dwe::screens::battleViewOf(battle, names);
    REQUIRE(view.fighters.size() == 2);
    CHECK(view.fighters[0].nameKey == "main.digimon_names.0003");
    CHECK(view.fighters[0].hp == 900);
    CHECK(view.fighters[0].maxHp == 900);
    CHECK(view.fighters[1].nameKey == "main.digimon_names.0012");
    CHECK(view.fighters[1].maxHp == 700);
    CHECK(view.fighters[1].statusKey.empty());
    CHECK_FALSE(view.reviveSeconds.has_value());
}

TEST_CASE("the battle view's menu is the battle's, with the original's command names") {
    const auto species = fixtureSpecies();
    const NameKeys names{&species, nullptr, &testCatalog()};
    Battle battle = startBattle(duel(evenStats(900, 100), evenStats(700, 80)), 3);

    const auto view = dwe::screens::battleViewOf(battle, names);
    REQUIRE(view.menu.size() == battle.menu().size());
    for (std::size_t index = 0; index < view.menu.size(); ++index) {
        CHECK(view.menu[index].command == battle.menu()[index]);
    }
    const auto flee = std::ranges::find(view.menu, Command::Flee, &dwe::screens::CommandEntry::command);
    REQUIRE(flee != view.menu.end());
    CHECK(flee->labelKey == "btl.command_names.0000");
    const auto yourCall = std::ranges::find(view.menu, Command::YourCall, &dwe::screens::CommandEntry::command);
    REQUIRE(yourCall != view.menu.end());
    CHECK(yourCall->labelKey == "btl.command_names.0002");
    CHECK(yourCall->current);
    // Without a full gauge the finisher waits.
    const auto finisher = std::ranges::find(view.menu, Command::Finisher, &dwe::screens::CommandEntry::command);
    if (finisher != view.menu.end()) {
        CHECK_FALSE(finisher->available);
    }
    CHECK_FALSE(view.finisher.ready);
}

TEST_CASE("a clever partner's menu names its techniques by their moves") {
    const auto species = fixtureSpecies();
    const NameKeys names{&species, nullptr, &testCatalog()};
    Battle battle = startBattle(duel(evenStats(900, 600), evenStats(700, 80), {"punch", "shot"}), 3);

    const auto view = dwe::screens::battleViewOf(battle, names);
    const auto first = std::ranges::find(view.menu, Command::Technique1, &dwe::screens::CommandEntry::command);
    REQUIRE(first != view.menu.end());
    CHECK(first->labelKey == "main.move_names.0000");
    const auto second = std::ranges::find(view.menu, Command::Technique2, &dwe::screens::CommandEntry::command);
    REQUIRE(second != view.menu.end());
    CHECK(second->labelKey == "main.move_names.0001");
}

TEST_CASE("the battle view shows the partner's HP bar always and an enemy's only after damage, and the bag") {
    const auto species = fixtureSpecies();
    const NameKeys names{&species, nullptr, &testCatalog()};
    Battle battle = startBattle(duel(evenStats(900, 100), evenStats(700, 80)), 3);

    auto view = dwe::screens::battleViewOf(battle, names);
    CHECK(view.fighters[0].hpBarShown);
    CHECK_FALSE(view.fighters[1].hpBarShown);
    CHECK(view.bagAvailable);
    CHECK_FALSE(view.bagOpen);
    REQUIRE(battle.openBag());
    CHECK(dwe::screens::battleViewOf(battle, names).bagOpen);
    REQUIRE(battle.throwItem({}));
    view = dwe::screens::battleViewOf(battle, names);
    CHECK(view.itemInFlight);
    CHECK_FALSE(view.bagAvailable);
    CHECK_FALSE(view.bagOpen);
    const auto settings = dwe::screens::settingsOf(view);
    const auto* hpBar = settingNamed(settings, "battle.fighter.1.hp_bar");
    REQUIRE(hpBar != nullptr);
    CHECK_FALSE(std::get<bool>(hpBar->value));
    REQUIRE(settingNamed(settings, "battle.item_in_flight") != nullptr);
}

TEST_CASE("the battle view becomes the settings the battle document reads") {
    const auto species = fixtureSpecies();
    const NameKeys names{&species, nullptr, &testCatalog()};
    Battle battle = startBattle(duel(evenStats(900, 100), evenStats(700, 80)), 3);

    const auto settings = dwe::screens::settingsOf(dwe::screens::battleViewOf(battle, names));
    const auto* count = settingNamed(settings, "battle.fighter.count");
    REQUIRE(count != nullptr);
    CHECK(std::get<double>(count->value) == doctest::Approx(2.0));
    const auto* name = settingNamed(settings, "battle.fighter.1.name");
    REQUIRE(name != nullptr);
    CHECK(std::get<std::string>(name->value) == "main.digimon_names.0012");
    const auto* revive = settingNamed(settings, "battle.revive_seconds");
    REQUIRE(revive != nullptr);
    CHECK(std::get<double>(revive->value) < 0.0);
    CHECK(settingNamed(settings, "battle.menu.0.label") != nullptr);
}

TEST_CASE("the end box lists the drops, the moves learned and what a defeat took") {
    const auto species = fixtureSpecies();
    const auto items = fixtureItems();
    const NameKeys names{&species, &items, &testCatalog()};
    BattleResult result;
    result.outcome = Outcome::Victory;
    result.bits = 120;
    result.drops = {"tonic"};
    result.injured = true;
    dwe::session::BattleAftermath aftermath;
    aftermath.learnedMoves = {"shot"};
    aftermath.statGains[dwe::game::partner::Stat::Offense] = 4;

    const auto view = dwe::screens::battleEndViewOf(result, aftermath, names);
    CHECK(view.outcomeKey == "battle.outcome.victory");
    CHECK(view.bits == 120);
    REQUIRE(view.lines.size() == 3);
    CHECK(view.lines[0] ==
          dwe::screens::EndLine{.key = "battle.result.item_dropped", .nameKey = "main.item_names.0004", .amount = 1});
    CHECK(view.lines[1].key == "battle.result.injured");
    CHECK(view.lines[2] ==
          dwe::screens::EndLine{.key = "battle.result.move_learned", .nameKey = "main.move_names.0001", .amount = 0});
    CHECK(view.statGains.at(2) == 4);

    result.outcome = Outcome::Defeat;
    result.drops.clear();
    result.injured = false;
    result.livesLost = 1;
    aftermath.learnedMoves.clear();
    aftermath.itemsLost = {{.item = 4, .count = 2}};
    const auto defeat = dwe::screens::battleEndViewOf(result, aftermath, names);
    CHECK(defeat.outcomeKey == "battle.outcome.defeat");
    REQUIRE(defeat.lines.size() == 2);
    CHECK(defeat.lines[0] == dwe::screens::EndLine{.key = "battle.result.lives_lost", .nameKey = {}, .amount = 1});
    CHECK(defeat.lines[1] ==
          dwe::screens::EndLine{.key = "battle.result.item_lost", .nameKey = "main.item_names.0004", .amount = 2});
}

TEST_CASE("a name whose row is unknown has no key") {
    const NameKeys names{nullptr, nullptr, nullptr};
    CHECK(names.species("hero").empty());
    CHECK(names.move("punch").empty());
    CHECK(names.item(std::string_view{"tonic"}).empty());
    CHECK(names.item(7).empty());
    CHECK(dwe::screens::commandLabelKey(Command::Change) == "btl.command_names.0006");
    CHECK(dwe::screens::commandLabelKey(Command::Technique1).empty());
}
