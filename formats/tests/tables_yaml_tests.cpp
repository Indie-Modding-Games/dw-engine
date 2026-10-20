#include <dwe/core/yaml.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/exe/tables_yaml.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <variant>
#include <vector>

#include "exe_fixture.hpp"
#include "schema_check.hpp"

using dwe::core::emitYaml;
using dwe::core::YamlNode;
using dwe::formats::exe::escapeGameText;
using dwe::formats::exe::ExecutableTables;
using dwe::formats::exe::makeIds;
using dwe::formats::exe::MemoryImage;
using dwe::formats::exe::readExecutableTables;
using dwe::formats::exe::tablesToYaml;
using dwe::formats::exe::TablesYaml;
using dwe::formats::test::checkAgainstSchema;
using dwe::formats::test::makeExecutableFixture;

namespace {

ExecutableTables fixtureTables() {
    const auto fixture = makeExecutableFixture();
    const auto image = MemoryImage::fromExecutable(fixture.file);
    REQUIRE(image.has_value());
    auto tables = readExecutableTables(*image, fixture.layout);
    REQUIRE(tables.has_value());
    return std::move(*tables);
}

const YamlNode& documentNamed(const TablesYaml& yaml, std::string_view name) {
    const auto found = std::ranges::find(yaml.documents, name, &dwe::formats::exe::TableDocument::name);
    REQUIRE(found != yaml.documents.end());
    return found->document;
}

// Follows a path of mapping keys and sequence indices.
const YamlNode& at(const YamlNode& node, std::initializer_list<std::variant<std::string_view, std::size_t>> path) {
    const YamlNode* current = &node;
    for (const auto& step : path) {
        if (const auto* key = std::get_if<std::string_view>(&step)) {
            current = current->find(*key);
            REQUIRE(current != nullptr);
        } else {
            const std::size_t index = std::get<std::size_t>(step);
            REQUIRE(index < current->items().size());
            current = &current->items()[index];
        }
    }
    return *current;
}

} // namespace

TEST_CASE("texts keep printable ASCII and escape everything else") {
    CHECK(escapeGameText("Meat") == "Meat");
    CHECK(escapeGameText("a\\b") == "a\\\\b");
    CHECK(escapeGameText("\x82\xA0") == "\\x82\\xA0");
    CHECK(escapeGameText(std::string{"\n\0", 2}) == "\\x0A\\x00");
}

TEST_CASE("ids are lowercase slugs, unique and stable") {
    const std::vector<std::string> names = {"Spit Fire", "Spit Fire", "", "  Big--Bang!", "spit_fire", "\x82\xA0"};
    CHECK(makeIds(names, "move") ==
          std::vector<std::string>{"spit_fire", "spit_fire_1", "move_2", "big_bang", "spit_fire_4", "move_5"});
    CHECK(makeIds(names, "move") == makeIds(names, "move"));
}

TEST_CASE("the tables become documents with ids and resolved references") {
    const TablesYaml yaml = tablesToYaml(fixtureTables());
    CHECK(yaml.documents.size() == 13);

    const YamlNode& digimon = documentNamed(yaml, "digimon");
    CHECK(at(digimon, {"digimon", std::size_t{0}, "id"}).text() == "tsta");
    CHECK(at(digimon, {"digimon", std::size_t{0}, "name"}).text() == "Testmon");
    CHECK(at(digimon, {"digimon", std::size_t{0}, "drop_item"}).text() == "odd");
    CHECK(at(digimon, {"digimon", std::size_t{1}, "drop_item"}).text() == "null");
    CHECK(at(digimon, {"digimon", std::size_t{0}, "moves", std::size_t{0}}).text() == "spit_fire_1");
    CHECK(at(digimon, {"digimon", std::size_t{0}, "special", std::size_t{1}}).text() == "null");

    const YamlNode& moves = documentNamed(yaml, "moves");
    CHECK(at(moves, {"moves", std::size_t{0}, "learn_chances", std::size_t{1}}).text() == "16");
    CHECK(moves.find("moves")->items()[1].find("learn_chances") == nullptr);

    const YamlNode& items = documentNamed(yaml, "items");
    CHECK(at(items, {"items", std::size_t{1}, "name"}).text() == "Odd\\x82\\xA0\\\\");
    CHECK(at(items, {"items", std::size_t{1}, "description"}).text() == "Strange.");

    const YamlNode& effects = documentNamed(yaml, "item_effects");
    CHECK(at(effects, {"item_effects", std::size_t{0}, "item"}).text() == "meat");
    CHECK(at(effects, {"item_effects", std::size_t{0}, "handler"}).text() == "food");
    CHECK(at(effects, {"item_effects", std::size_t{0}, "values", "energy"}).text() == "12");
    CHECK(at(effects, {"item_effects", std::size_t{0}, "healing"}).text() == "null");
    CHECK(at(effects, {"item_effects", std::size_t{1}, "handler"}).text() == "hp_healing");
    CHECK(at(effects, {"item_effects", std::size_t{1}, "healing"}).text() == "500");
    CHECK(at(effects, {"item_effects", std::size_t{1}, "values"}).text() == "null");

    const YamlNode& evolution = documentNamed(yaml, "evolution");
    CHECK(at(evolution, {"requirements", std::size_t{0}, "digimon"}).text() == "null");
    CHECK(at(evolution, {"requirements", std::size_t{1}, "digimon"}).text() == "othr");
    CHECK(at(evolution, {"stats_gains", std::size_t{1}, "target_digimon"}).text() == "77");
    CHECK(at(evolution, {"paths", std::size_t{0}, "digimon"}).text() == "tstb");
    CHECK(at(evolution, {"paths", std::size_t{0}, "to", std::size_t{0}}).text() == "othr");

    const YamlNode& tournaments = documentNamed(yaml, "tournaments");
    CHECK(at(tournaments, {"schedule", std::size_t{0}, "tournaments"}).items().size() == 2);
    CHECK(at(tournaments, {"schedule", std::size_t{0}, "tournaments", std::size_t{0}}).text() == "wind_cup");
    CHECK(at(tournaments, {"schedule", std::size_t{1}, "tournaments"}).items().empty());

    const YamlNode& maps = documentNamed(yaml, "maps");
    CHECK(at(maps, {"maps", std::size_t{0}, "id"}).text() == "start");
    CHECK(at(maps, {"maps", std::size_t{1}, "id"}).text() == "map_1");
    CHECK(at(maps, {"maps", std::size_t{0}, "sound_id"}).text() == "3");
    CHECK(at(maps, {"maps", std::size_t{0}, "has_npcs"}).text() == "true");
    CHECK(at(maps, {"maps", std::size_t{0}, "doors"}).text() == "0");
    CHECK(at(maps, {"maps", std::size_t{0}, "toilet"}).text() == "null");
    CHECK(at(maps, {"light_updates", std::size_t{0}, "trigger"}).text() == "null");

    const YamlNode& shops = documentNamed(yaml, "shops");
    CHECK(at(shops, {"shop_maps", std::size_t{0}}).text() == "map_1");
    CHECK(at(shops, {"shop_maps", std::size_t{1}}).text() == "-1");
    CHECK(at(shops, {"shopkeeper_digimon"}).items().size() == 1);

    const YamlNode& medals = documentNamed(yaml, "medals");
    CHECK(at(medals, {"medals", std::size_t{0}, "description"}).items().size() == 3);

    const auto noted = [&](std::string_view text) {
        return std::ranges::any_of(yaml.notes, [&](const std::string& note) { return note.contains(text); });
    };
    CHECK(noted("ITEM_PARA[1]: text kept with escaped bytes"));
    CHECK(noted("EVOLUTION_STATS_GAINS[1]: reference 77"));
    CHECK(noted("SHOP_MAPS[1]: reference -1"));
}

TEST_CASE("every document matches its schema and is written the same way twice") {
    const ExecutableTables tables = fixtureTables();
    const TablesYaml first = tablesToYaml(tables);
    const TablesYaml second = tablesToYaml(tables);
    REQUIRE(first.documents.size() == second.documents.size());
    for (std::size_t i = 0; i < first.documents.size(); ++i) {
        CAPTURE(first.documents[i].name);
        CHECK(checkAgainstSchema(first.documents[i].document, first.documents[i].name).empty());
        const auto text = emitYaml(first.documents[i].document);
        const auto again = emitYaml(second.documents[i].document);
        REQUIRE(text.has_value());
        REQUIRE(again.has_value());
        CHECK(*text == *again);
    }
}

TEST_CASE("the schema check reports unknown and missing keys") {
    YamlNode document = YamlNode::mapping();
    document.set("prices", YamlNode::sequence());
    document.set("extra", YamlNode::scalar("1"));
    const auto problems = checkAgainstSchema(document, "cards");
    CHECK(std::ranges::any_of(problems, [](const std::string& p) { return p.contains("'extra'"); }));
    CHECK(std::ranges::any_of(problems, [](const std::string& p) { return p.contains("'cards' is missing"); }));
}
