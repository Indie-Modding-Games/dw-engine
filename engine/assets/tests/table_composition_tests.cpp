#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using dwe::assets::AssetId;
using dwe::assets::EntrySection;
using dwe::assets::Layer;
using dwe::assets::MemoryFileSystem;
using dwe::assets::TableComposition;
using dwe::assets::TableContribution;
using dwe::assets::VirtualFileSystem;
using dwe::core::YamlNode;

namespace {

constexpr std::array<EntrySection, 2> FixtureSections{{
    {.section = "rows", .entryKey = "id"},
    {.section = "notes", .entryKey = "digimon", .numbered = false, .ownsIds = false},
}};
constexpr TableComposition FixtureComposition{.sections = FixtureSections};

constexpr std::string_view BaseTable = R"(rows:
- id: "agum"
  index: 3
  power: 10
- id: "gabu"
  index: 7
  power: 12
notes:
- digimon: "agum"
  text: "orange"
names:
- "Agumon"
- "Gabumon"
)";

TableContribution contribution(std::string_view packId, Layer layer, std::string_view yaml) {
    auto document = dwe::core::parseYaml(yaml);
    REQUIRE(document.has_value());
    return {.packId = std::string{packId},
            .layer = layer,
            .file = std::format("{}/data/table.yaml", packId),
            .document = std::move(*document)};
}

YamlNode composed(const std::vector<TableContribution>& contributions) {
    auto table = dwe::assets::composeTable(contributions, FixtureComposition);
    REQUIRE_MESSAGE(table.has_value(), table.error().message);
    return *table;
}

std::string failure(const std::vector<TableContribution>& contributions) {
    auto table = dwe::assets::composeTable(contributions, FixtureComposition);
    REQUIRE_FALSE(table.has_value());
    return table.error().message;
}

YamlNode parsed(std::string_view yaml) {
    auto node = dwe::core::parseYaml(yaml);
    REQUIRE(node.has_value());
    return *node;
}

AssetId idOf(std::string_view text) {
    auto id = AssetId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

void addPack(MemoryFileSystem& memory, std::string_view id, std::string_view layer, int priority = 0) {
    memory.addFile(std::format("packs/{}/pack.yaml", id),
                   std::format("id: {}\nversion: 1.0.0\nlayer: {}\npriority: {}\n", id, layer, priority));
}

VirtualFileSystem mounted(const MemoryFileSystem& memory, const std::vector<std::filesystem::path>& roots) {
    auto vfs = VirtualFileSystem::mount(memory, roots);
    REQUIRE_MESSAGE(vfs.has_value(), vfs.error().message);
    return std::move(*vfs);
}

} // namespace

TEST_CASE("a table of one pack composes to that pack's table") {
    const YamlNode table = composed({contribution("base", Layer::Base, BaseTable)});
    CHECK(table == parsed(BaseTable));
}

TEST_CASE("a pack's entry replaces the entry with its id in its place and keeps its index") {
    const YamlNode table = composed({contribution("base", Layer::Base, BaseTable),
                                     contribution("tweak", Layer::Mods, "rows:\n- id: agum\n  power: 99\n")});
    CHECK(table.find("rows")->items().size() == 2);
    CHECK(table.find("rows")->items()[0] == parsed("id: agum\nindex: 3\npower: 99"));
    CHECK(table.find("rows")->items()[1] == parsed(BaseTable).find("rows")->items()[1]);
    CHECK(*table.find("names") == *parsed(BaseTable).find("names"));
}

TEST_CASE("a pack's new entries go at the end with the next free indices") {
    const YamlNode table =
        composed({contribution("base", Layer::Base, BaseTable),
                  contribution("my_mod", Layer::Mods,
                               "rows:\n- id: my_mod:pebble\n  power: 1\n- id: my_mod:stone\n  power: 2\n")});
    const auto rows = table.find("rows")->items();
    REQUIRE(rows.size() == 4);
    CHECK(rows[2] == parsed("id: my_mod:pebble\nindex: 8\npower: 1"));
    CHECK(rows[3] == parsed("id: my_mod:stone\nindex: 9\npower: 2"));
}

TEST_CASE("a pack that has the whole table with the same entries gives that pack's table") {
    // What an editor that writes the whole table back leaves: every entry, a section changed.
    const std::string whole = std::string{BaseTable}.replace(std::string{BaseTable}.find("Gabumon"), 7, "Gabu");
    const YamlNode table =
        composed({contribution("base", Layer::Base, BaseTable), contribution("editor", Layer::Mods, whole)});
    CHECK(table == parsed(whole));
}

TEST_CASE("a section that is no entry list is replaced whole, and a new section is added") {
    const YamlNode table = composed({contribution("base", Layer::Base, BaseTable),
                                     contribution("tweak", Layer::Mods, "names:\n- \"Only\"\nextra: 1\n")});
    CHECK(*table.find("names") == parsed("- Only"));
    CHECK(*table.find("extra") == parsed("1"));
    CHECK(table.entries().back().key == "extra");
}

TEST_CASE("a section keyed by another table's id adds rows for any id, without indices") {
    const YamlNode table =
        composed({contribution("base", Layer::Base, BaseTable),
                  contribution("other", Layer::Mods, "notes:\n- digimon: my_mod:pebble\n  text: grey\n")});
    const auto notes = table.find("notes")->items();
    REQUIRE(notes.size() == 2);
    CHECK(notes[1] == parsed("digimon: my_mod:pebble\ntext: grey"));
}

TEST_CASE("entries compose in stack order, whatever packs come first") {
    const YamlNode table = composed(
        {contribution("base", Layer::Base, BaseTable),
         contribution("first", Layer::Mods, "rows:\n- id: first:a\n  power: 1\n"),
         contribution("second", Layer::Mods, "rows:\n- id: second:b\n  power: 2\n- id: first:a\n  power: 3\n")});
    const auto rows = table.find("rows")->items();
    REQUIRE(rows.size() == 4);
    CHECK(rows[2] == parsed("id: first:a\nindex: 8\npower: 3"));
    CHECK(rows[3] == parsed("id: second:b\nindex: 9\npower: 2"));
}

TEST_CASE("a pack outside the base names its new entries in its own namespace and leaves their index out") {
    const auto base = contribution("base", Layer::Base, BaseTable);
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "rows:\n- id: pebble\n  power: 1\n")})
              .contains("'my_mod:<name>'"));
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "rows:\n- id: other:pebble\n  power: 1\n")})
              .contains("namespace is not the pack's id"));
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "rows:\n- id: my_mod:pebble\n  index: 50\n")})
              .contains("leave it out"));
}

TEST_CASE("an entry that replaces another keeps its index, and a file lists each id once with its key") {
    const auto base = contribution("base", Layer::Base, BaseTable);
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "rows:\n- id: agum\n  index: 4\n")})
              .contains("keeps its index"));
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "rows:\n- id: agum\n- id: agum\n")}).contains("twice"));
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "rows:\n- power: 1\n")}).contains("its 'id'"));
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "rows:\n- id: Agum\n")}).contains("'Agum'"));
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "- not a table\n")}).contains("mapping of sections"));
}

TEST_CASE("the packs' digimon and raise tables compose for the game's readers") {
    MemoryFileSystem memory;
    addPack(memory, "base", "base");
    addPack(memory, "my_mod", "mods");
    const std::string row = R"(  name: "{}"
  file_name: "{}"
  bone_count: 1
  radius: 100
  height: 200
  type: 1
  level: 3
  special: [0, null, null]
  drop_item: null
  drop_chance: 0
  moves: [null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null]
)";
    memory.addFile("packs/base/data/digimon.yaml",
                   "digimon:\n- id: agum\n  index: 3\n" + std::vformat(row, std::make_format_args("Agumon", "AGUM")));
    memory.addFile("packs/my_mod/data/digimon.yaml",
                   "digimon:\n- id: my_mod:pebble\n" + std::vformat(row, std::make_format_args("Pebble", "PEBB")));
    const VirtualFileSystem vfs = mounted(memory, {"packs/base", "packs/my_mod"});
    // A new Digimon comes with its model contract.
    const auto unchecked = dwe::assets::loadDigimonTable(vfs, idOf("data/digimon"));
    REQUIRE_FALSE(unchecked.has_value());
    CHECK(unchecked.error().message.contains("digimon/my_mod/pebble/contract"));
    memory.addFile("packs/my_mod/digimon/my_mod/pebble/contract.yaml", "model: model.glb\n");
    const VirtualFileSystem withContract = mounted(memory, {"packs/base", "packs/my_mod"});

    const auto table = dwe::assets::loadDigimonTable(withContract, idOf("data/digimon"));
    REQUIRE_MESSAGE(table.has_value(), table.error().message);
    REQUIRE(table->digimon.size() == 2);
    CHECK(table->find("agum")->index == 3);
    CHECK(table->find("my_mod:pebble")->index == 4);
    CHECK(table->find("my_mod:pebble")->name == "Pebble");
}

TEST_CASE("a table only one pack has is composed too, and read as that pack's file when that changes nothing") {
    MemoryFileSystem memory;
    addPack(memory, "base", "base");
    memory.addFile("packs/base/data/maps.yaml", "maps: []\n# a comment the composition would drop\n");
    const VirtualFileSystem vfs = mounted(memory, {"packs/base"});
    const auto text = dwe::assets::readComposedTable(vfs, idOf("data/maps"), dwe::assets::MapComposition);
    REQUIRE(text.has_value());
    CHECK(*text == "maps: []\n# a comment the composition would drop\n");

    // Entries without their index get one, as the readers need it; a mod alone is checked as any other.
    addPack(memory, "my_mod", "mods");
    memory.addFile("packs/base/data/moves.yaml", "rows:\n- id: a\n- id: b\n  index: 4\n");
    memory.addFile("packs/my_mod/data/items.yaml", "rows:\n- id: c\n");
    const VirtualFileSystem both = mounted(memory, {"packs/base", "packs/my_mod"});
    const auto numbered = dwe::assets::readComposedTable(both, idOf("data/moves"), FixtureComposition);
    REQUIRE(numbered.has_value());
    CHECK(parsed(*numbered) == parsed("rows:\n- id: a\n  index: 0\n- id: b\n  index: 4\n"));
    const auto alone = dwe::assets::readComposedTable(both, idOf("data/items"), FixtureComposition);
    REQUIRE_FALSE(alone.has_value());
    CHECK(alone.error().message.contains("'my_mod:<name>'"));
}

TEST_CASE("two entries of the base never share an index") {
    CHECK(failure({contribution("base", Layer::Base, "rows:\n- id: a\n  index: 3\n- id: b\n  index: 3\n")})
              .contains("which 'a' already has"));
    CHECK(failure({contribution("base", Layer::Base, BaseTable),
                   contribution("more", Layer::Base, "rows:\n- id: c\n  index: 7\n")})
              .contains("which 'gabu' already has"));
}

TEST_CASE("a pack cannot empty an entry list with a null or a scalar") {
    const auto base = contribution("base", Layer::Base, BaseTable);
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "rows: null\n")}).contains("'rows' must be a list"));
    CHECK(failure({base, contribution("my_mod", Layer::Mods, "notes: 1\n")}).contains("'notes' must be a list"));
}

TEST_CASE("a row keyed by another table keeps the base's index when replaced, and a new row gets none") {
    const YamlNode table =
        composed({contribution("base", Layer::Base, "notes:\n- digimon: agum\n  index: 3\n  text: orange\n"),
                  contribution("my_mod", Layer::Mods,
                               "notes:\n- digimon: agum\n  text: red\n- digimon: my_mod:pebble\n  text: grey\n")});
    const auto notes = table.find("notes")->items();
    REQUIRE(notes.size() == 2);
    CHECK(notes[0] == parsed("digimon: agum\nindex: 3\ntext: red"));
    CHECK(notes[1] == parsed("digimon: my_mod:pebble\ntext: grey"));
    CHECK(
        failure({contribution("my_mod", Layer::Mods, "notes:\n- digimon: x\n  index: 2\n")}).contains("leave it out"));
    // A higher pack cannot give an index to a row a lower pack added without one, in either kind of section.
    CHECK(failure({contribution("base", Layer::Base, BaseTable),
                   contribution("my_mod", Layer::Mods, "notes:\n- digimon: my_mod:pebble\n"),
                   contribution("other", Layer::Mods, "notes:\n- digimon: my_mod:pebble\n  index: 9\n")})
              .contains("leave it out"));
    CHECK(failure({contribution("base", Layer::Base, BaseTable),
                   contribution("my_mod", Layer::Mods, "rows:\n- id: my_mod:pebble\n"),
                   contribution("other", Layer::Mods, "rows:\n- id: my_mod:pebble\n  index: 8\n")})
              .contains("leave it out"));
}
