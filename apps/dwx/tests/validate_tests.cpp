#include <dwe/assets/file_system.hpp>
#include <dwe/assets/lua_api.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/text_table.hpp>
#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "asset_names.hpp"
#include "audio_cooker.hpp"
#include "glb_builder.hpp"
#include "map_documents.hpp"
#include "media_fixtures.hpp"
#include "validate.hpp"

using namespace dwe::dwx;
using dwe::assets::MemoryFileSystem;
using dwe::test_support::TempFolder;

namespace {

// The message of a failed result. doctest builds a message even when the check passes, so it cannot call error().
template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// A glTF model with a skin of three joints whose parents are -1, 0 and 1 and an animation named walk. Its buffer is
// inside it; withImage adds an image in a file next to it.
std::string skinnedModel(bool withImage) {
    std::string model = dwe::assets::testing::skinnedGltf();
    const std::string buffer = R"("uri": "anim.bin")";
    model.replace(model.find(buffer), buffer.size(),
                  std::format(R"("uri": "data:application/octet-stream;base64,{}=")", std::string(43, 'A')));
    if (!withImage) {
        const std::string image = R"({"uri": "skin.png"}, )";
        model.erase(model.find(image), image.size());
    }
    return model;
}

// A binary model with one joint and one clip, as dwx extracts them. The game refuses a clip whose extras say it has
// no ticks.
std::vector<std::byte> clipModel(int tickCount) {
    dwe::assets::testing::GlbBuilder builder;
    const int times = builder.addFloats({0.0F, 0.05F}, "SCALAR", 1, R"(,"min":[0],"max":[0.05])");
    const int moves = builder.addFloats({0, 0, 0, 0, 0, 1}, "VEC3", 3);
    return builder.build(std::format(
        R"({{"scene":0,"scenes":[{{"nodes":[0]}}],"nodes":[{{"name":"bone_00"}}],"animations":[{{"name":"anim_00",)"
        R"("extras":{{"id":0,"ticksPerSecond":20,"tickCount":{}}},"samplers":[{{"input":{},"output":{}}}],)"
        R"("channels":[{{"sampler":0,"target":{{"node":0,"path":"translation"}}}}]}}]}})",
        tickCount, times, moves));
}

// The schemas of the tests: a sound bank's metadata names its SoundFont and a map, a map has a camera.
void addSchemas(MemoryFileSystem& files) {
    files.addFile("content/schemas/pack.schema.json", R"({"type": "object", "required": ["id", "name"]})");
    files.addFile("content/schemas/sound.schema.json", R"({
        "type": "object", "required": ["bank", "count"], "additionalProperties": false,
        "properties": {
            "bank": {"type": "string", "assetReference": "./{}"},
            "map": {"type": "string", "assetReference": "map/{}/data"},
            "count": {"type": "integer"}}})");
    files.addFile("content/schemas/map.schema.json", R"({"type": "object", "required": ["camera"]})");
    files.addFile("content/schemas/maps.schema.json", R"({"type": "object", "required": ["maps"]})");
    files.addFile("content/schemas/texts.schema.json", R"({"type": "object"})");
    files.addFile("content/schemas/model_contract.schema.json", R"({
        "type": "object", "required": ["model", "joint_parents", "animations"],
        "properties": {
            "model": {"type": "string", "assetReference": "./{}"},
            "joint_parents": {"type": "array", "items": {"type": "integer"}},
            "animations": {"type": "array", "items": {"type": "string"}}}})");
}

// A base pack that passes every check.
void addBasePack(MemoryFileSystem& files) {
    files.addFile("content/base/pack.yaml", R"(id: base
version: 1.0.0
layer: base
name: Base
schemas:
  sound: ["sound/*/bank_meta"]
  map: ["map/*/data"]
  maps: ["data/maps"]
  model_contract: ["digimon/*/contract"]
  texts: ["text/*/*", "localization/*/*"]
)");
    files.addFile("content/base/sound/hit/bank_meta.yaml", "bank: bank.sf2\nmap: town\ncount: 3\n");
    files.addFile("content/base/sound/hit/bank.sf2", dwe::assets::testing::soundFontFile());
    files.addFile("content/base/map/town/data.yaml", dwe::assets::testing::fixtureMap());
    files.addFile("content/base/data/maps.yaml", dwe::assets::testing::FixtureTable);
    files.addFile("content/base/model/door/door00.glb", clipModel(2));
    files.addFile("content/base/digimon/species_a/model.gltf", skinnedModel(false));
    files.addFile("content/base/digimon/species_a/contract.yaml",
                  "model: model.gltf\njoint_parents: [-1, 0, 1]\nanimations: [walk]\n");
}

// The problems of a report as dwx validate prints them, one per line.
std::string textOf(const ValidationReport& report) {
    std::string text;
    for (const ValidationProblem& problem : report.problems) {
        text += problem.line == 0
                    ? std::format("{}: {}\n", problem.file, problem.message)
                    : std::format("{}:{}:{}: {}\n", problem.file, problem.line, problem.column, problem.message);
    }
    return text;
}

std::string readText(const std::string& path) {
    std::ifstream file{path};
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// Memory files whose pack.yaml files can be read only once, as if they were gone after the stack was mounted.
class ManifestOnceFileSystem final : public dwe::assets::FileSystem {
public:
    explicit ManifestOnceFileSystem(const MemoryFileSystem& files) : files_(&files) {}

    [[nodiscard]] dwe::core::Result<std::vector<std::string>>
    listFiles(const std::filesystem::path& folder) const override {
        return files_->listFiles(folder);
    }

    [[nodiscard]] dwe::core::Result<std::vector<std::byte>> readFile(const std::filesystem::path& file) const override {
        if (file.filename() == "pack.yaml" && !manifestsRead_.insert(file).second) {
            return dwe::core::fail("it is gone");
        }
        return files_->readFile(file);
    }

    [[nodiscard]] dwe::core::Result<std::uint64_t> fileSize(const std::filesystem::path& file) const override {
        return files_->fileSize(file);
    }

private:
    const MemoryFileSystem* files_;
    mutable std::set<std::filesystem::path> manifestsRead_;
};

} // namespace

TEST_CASE("a pack whose documents follow their schemas and whose references resolve has no problems") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    const auto report = validatePacks(files, "content/base", "content/schemas", {}, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(textOf(*report).empty());
    CHECK(report->packs == 1);
    CHECK(report->assets == 7);
    CHECK(report->documents == 5);  // pack.yaml and the four YAML assets
    CHECK(report->references == 4); // the bank, the map, the contract's model and what bank_meta describes
    CHECK(report->contracts == 1);
}

TEST_CASE("dwx validate reports every problem of a stack, sorted") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("content/broken/pack.yaml", R"(id: broken
version: 1.0.0
layer: mods
schemas:
  other: ["notes/*"]
  map: ["sound/hit/bank_meta"]
)");
    files.addFile("content/broken/notes/a.yaml", "a: 1\n");
    files.addFile("content/broken/loose/b.yaml", "b: 2\n");
    files.addFile("content/broken/map/swamp/data.yaml", "name: [swamp\n");
    files.addFile("content/broken/sound/odd/bank_meta.yaml", "bank: bank.wav\nmap: nowhere\ncount: many\n");
    files.addFile("content/broken/sound/odd/bank.sf2", dwe::assets::testing::soundFontFile());
    files.addFile("content/broken/sound/lone_meta.yaml", "x: 1\n");
    files.addFile("content/broken/digimon/species_c/model.gltf", skinnedModel(true));
    files.addFile("content/broken/digimon/species_a/contract.yaml",
                  "model: model.gltf\njoint_parents: [-1, 0, 0]\nanimations: [walk, run]\n");
    const auto report = validatePacks(files, "content", "content/schemas", {}, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(report->packs == 2);
    const std::string expected =
        R"(content/base/digimon/species_a/model.gltf: breaks its contract content/broken/digimon/species_a/contract.yaml: it lacks the animations run
content/base/digimon/species_a/model.gltf: breaks its contract content/broken/digimon/species_a/contract.yaml: joint 2 has the parent 1, the contract says 0
content/base/sound/hit/bank_meta.yaml: the rules of several schemas match this document: map (sound/hit/bank_meta), sound (sound/*/bank_meta)
content/broken/digimon/species_c/model.gltf: the model has no contract: digimon/species_c/contract is missing
content/broken/digimon/species_c/model.gltf: the model uses 'skin.png', which names digimon/species_c/skin, which no pack has
content/broken/loose/b.yaml: no schema rule of the packs matches this document
content/broken/map/swamp/data.yaml: the engine cannot load map/swamp/data: line 2, column 1: did not find expected ',' or ']' (while parsing a flow sequence at line 1, column 7)
content/broken/pack.yaml: schema 'other' cannot be used: cannot read 'content/schemas/other.schema.json': it does not exist
content/broken/pack.yaml:1:1: $: the key 'name' is missing
content/broken/sound/lone_meta.yaml: no schema rule of the packs matches this document
content/broken/sound/lone_meta.yaml: the metadata of sound/lone, which no pack has
content/broken/sound/odd/bank_meta.yaml:1:7: $.bank: 'bank.wav' names a .wav file, but sound/odd/bank is content/broken/sound/odd/bank.sf2
content/broken/sound/odd/bank_meta.yaml:2:6: $.map: 'nowhere' names map/nowhere/data, which no pack has
content/broken/sound/odd/bank_meta.yaml:3:8: $.count: expected integer, found string
)";
    CHECK(textOf(*report) == expected);
}

TEST_CASE("a pack in a .pak archive is checked like a folder") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    const TempFolder folder;
    const std::filesystem::path content = folder.path() / "content";
    std::filesystem::create_directories(content);
    const auto written = dwe::assets::writePak(files, "content/base", content / "base.pak");
    REQUIRE_MESSAGE(written.has_value(), errorOf(written));
    const auto schemas = files.listFiles("content/schemas");
    REQUIRE(schemas.has_value());
    for (const std::string& schema : *schemas) {
        const auto bytes = files.readFile(std::filesystem::path{"content/schemas"} / schema);
        REQUIRE(bytes.has_value());
        folder.write(std::filesystem::path{"content/schemas"} / schema,
                     std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    }
    const dwe::assets::DiskFileSystem disk;
    const auto report = validatePacks(disk, content, content / "schemas", {}, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(textOf(*report).empty());
    CHECK(report->packs == 1);
    CHECK(report->assets == 7);
    CHECK(report->documents == 5);
    CHECK(report->references == 4);
    CHECK(report->contracts == 1);
}

TEST_CASE("a reference may name the type that a cooked file was made from") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    const auto opus = cookAudio(dwe::assets::testing::wavFile(4800), {});
    REQUIRE_MESSAGE(opus.has_value(), errorOf(opus));
    files.addFile("content/base/sound/cooked/bank_meta.yaml", "bank: bank.wav\nmap: town\ncount: 1\n");
    files.addFile("content/base/sound/cooked/bank.opus", *opus);
    files.addFile("content/base/sound/odd/bank_meta.yaml", "bank: bank.png\nmap: town\ncount: 1\n");
    files.addFile("content/base/sound/odd/bank.opus", *opus);
    const auto report = validatePacks(files, "content/base", "content/schemas", {}, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    // dwx cook makes .opus from .wav, never from .png.
    CHECK(textOf(*report) == "content/base/sound/odd/bank_meta.yaml:1:7: $.bank: 'bank.png' names a .png file, but "
                             "sound/odd/bank is content/base/sound/odd/bank.opus\n");
}

TEST_CASE("a manifest that cannot be read for its schema is a problem") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    const ManifestOnceFileSystem once{files};
    const auto report = validatePacks(once, "content/base", "content/schemas", {}, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(textOf(*report) == "content/base/pack.yaml: cannot be read: it is gone\n");
    CHECK(report->documents == 4);
}

TEST_CASE("a pack that needs another one is checked with --with") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("packs/extra/pack.yaml", R"(id: extra
version: 1.0.0
layer: mods
name: Extra
dependencies:
  - id: base
    min_version: 1.0.0
)");
    CHECK(errorOf(validatePacks(files, "packs/extra", "content/schemas")).contains("not mounted"));
    const std::array<std::filesystem::path, 1> with{"content/base"};
    const auto report = validatePacks(files, "packs/extra", "content/schemas", with);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(textOf(*report).empty());
    CHECK(report->packs == 2);
    // A pack named by --with and by the folder is mounted once.
    const auto twice = validatePacks(files, "content/base", "content/schemas", with, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(twice.has_value(), errorOf(twice));
    CHECK(twice->packs == 1);
}

TEST_CASE("a mod adds a map by a namespaced id: its entry composes with the base's table and references find it") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("packs/my_mod/pack.yaml", R"(id: my_mod
version: 1.0.0
layer: mods
name: My mod
dependencies:
  - id: base
    min_version: 1.0.0
schemas:
  map: ["map/*/*/data"]
)");
    files.addFile("packs/my_mod/data/maps.yaml", R"(maps:
- id: "my_mod:meadow"
  filename: "MEADOW"
  images_8bpp: 0
  images_4bpp: 0
  flags: 64
  sound_id: 0
  fixed_time_of_day: true
  has_npcs: false
  doors: null
  toilet: null
  loading_name: 0
)");
    files.addFile("packs/my_mod/map/my_mod/meadow/data.yaml", dwe::assets::testing::fixtureMap());
    files.addFile("packs/my_mod/sound/meadow/bank_meta.yaml", "bank: bank.sf2\nmap: my_mod:meadow\ncount: 1\n");
    files.addFile("packs/my_mod/sound/meadow/bank.sf2", dwe::assets::testing::soundFontFile());
    const std::array<std::filesystem::path, 1> with{"content/base"};
    const auto report = validatePacks(files, "packs/my_mod", "content/schemas", with);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(textOf(*report).empty());

    // A new entry without the pack's namespace is refused, as the game refuses it.
    files.addFile("packs/my_mod/data/maps.yaml", "maps:\n- id: meadow\n");
    const auto unnamed = validatePacks(files, "packs/my_mod", "content/schemas", with);
    REQUIRE_MESSAGE(unnamed.has_value(), errorOf(unnamed));
    CHECK(textOf(*unnamed).contains("'my_mod:<name>'"));
}

TEST_CASE("dwx validate reports a map of the evolution rules that is no map of data/maps") {
    // The game pack's evolution section, whose special rule asks for mayo02, over a base whose maps lack it.
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("content/schemas/ruleset_evolution.schema.json",
                  readText(std::format("{}/ruleset_evolution.schema.json", DWE_SCHEMA_DIR)));
    files.addFile("packs/my_mod/pack.yaml", R"(id: my_mod
version: 1.0.0
layer: mods
name: My mod
schemas:
  ruleset_evolution: ["rules/*/evolution"]
)");
    const std::string section = readText(std::format("{}/rules/original/evolution.yaml", DWE_GAME_PACK));
    REQUIRE(section.contains("map: { equals: mayo02 }"));
    const auto problems = [&](std::string_view map) {
        std::string text = section;
        const std::string_view rule = "equals: mayo02";
        text.replace(text.find(rule), rule.size(), std::format("equals: {}", map));
        files.addFile("packs/my_mod/rules/original/evolution.yaml", text);
        const std::array<std::filesystem::path, 1> with{"content/base"};
        const auto report = validatePacks(files, "packs/my_mod", "content/schemas", with);
        REQUIRE_MESSAGE(report.has_value(), errorOf(report));
        return textOf(*report);
    };
    CHECK(problems("mayo02").contains("'mayo02' is no entry of data/maps"));
    CHECK_FALSE(problems("twna01").contains("is no entry of data/maps"));
}

TEST_CASE("dwx validate names a mod map's dead warp, with the hint that an editor may have dropped its id") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("packs/my_mod/pack.yaml", R"(id: my_mod
version: 1.0.0
layer: mods
name: My mod
dependencies:
  - id: base
    min_version: 1.0.0
schemas:
  map: ["map/*/*/data"]
)");
    files.addFile("packs/my_mod/data/maps.yaml", R"(maps:
- id: "my_mod:meadow"
  filename: "MEADOW"
  images_8bpp: 0
  images_4bpp: 0
  flags: 64
  sound_id: 0
  fixed_time_of_day: true
  has_npcs: false
  doors: null
  toilet: null
  loading_name: 0
)");
    const std::string_view fixtureExit = "  target_map: 118\n  target_map_id: \"room08\"\n";
    const auto withExit = [&](std::string_view exit) {
        std::string map = dwe::assets::testing::fixtureMap();
        map.replace(map.find(fixtureExit), fixtureExit.size(), exit);
        files.addFile("packs/my_mod/map/my_mod/meadow/data.yaml", map);
        const std::array<std::filesystem::path, 1> with{"content/base"};
        const auto report = validatePacks(files, "packs/my_mod", "content/schemas", with);
        REQUIRE_MESSAGE(report.has_value(), errorOf(report));
        return textOf(*report);
    };
    // The editor's shape: target_map -1 and no id, though the collision has the exit's trigger.
    const std::string dead = withExit("  target_map: -1\n  target_map_id: null\n");
    CHECK(dead.contains("map 'my_mod:meadow', exit 0"));
    CHECK(dead.contains("may have dropped target_map_id"));
    // The problem is at the line of the exit's item.
    const std::string map = dwe::assets::testing::fixtureMap();
    const auto exitLine = std::ranges::count(std::string_view{map}.substr(0, map.find("- exit: 0\n")), '\n') + 1;
    CHECK(dead.contains(std::format("meadow/data.yaml:{}:0: ", exitLine)));
    // As a Windows editor saves it, with CRLF line ends and the items indented: the same line.
    {
        std::string unix = dwe::assets::testing::fixtureMap();
        unix.replace(unix.find(fixtureExit), fixtureExit.size(), "  target_map: -1\n  target_map_id: null\n");
        std::string windows;
        bool inWarps = false;
        for (const auto line : std::views::split(std::string_view{unix}, '\n')) {
            const std::string_view text{line.begin(), line.end()};
            if (!text.empty() && text.front() != ' ' && text.front() != '-') {
                inWarps = text == "warps:";
            } else if (inWarps && !text.empty()) {
                windows += "  ";
            }
            windows += text;
            windows += "\r\n";
        }
        files.addFile("packs/my_mod/map/my_mod/meadow/data.yaml", windows);
        const std::array<std::filesystem::path, 1> bases{"content/base"};
        const auto crlf = validatePacks(files, "packs/my_mod", "content/schemas", bases);
        REQUIRE_MESSAGE(crlf.has_value(), errorOf(crlf));
        CHECK_MESSAGE(textOf(*crlf).contains(std::format("meadow/data.yaml:{}:0: ", exitLine)), textOf(*crlf));
    }
    // An id of no map next to an index of the original leads there, but the id is reported.
    CHECK(withExit("  target_map: 118\n  target_map_id: \"my_mod:nowhere\"\n").contains("'my_mod:nowhere' is no map"));
    // By id alone, even to the mod's own map, or by the original's index alone, the warp leads somewhere.
    CHECK(withExit("  target_map: -1\n  target_map_id: \"my_mod:meadow\"\n").empty());
    CHECK(withExit("  target_map: 118\n  target_map_id: null\n").empty());
    CHECK(withExit("  target_map: 104\n  target_map_id: \"room08\"\n").contains("name different maps"));
}

TEST_CASE("an id a schema says names a table's entry must be one, in the table as the packs compose it") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("content/schemas/trips.schema.json", R"({
        "type": "object",
        "properties": {
            "stops": {"type": "array", "items": {"type": "string", "entryReference": "data/maps"}},
            "rooms": {"type": "array", "items": {"type": "string", "entryReference": "data/rooms"}}}})");
    files.addFile("packs/my_mod/pack.yaml", R"(id: my_mod
version: 1.0.0
layer: mods
name: My mod
schemas:
  map: ["map/*/*/data"]
  trips: ["rules/trips"]
)");
    files.addFile("packs/my_mod/data/maps.yaml",
                  "maps:\n- id: \"my_mod:meadow\"\n  filename: \"MEADOW\"\n  images_8bpp: 0\n"
                  "  images_4bpp: 0\n  flags: 64\n  sound_id: 0\n  fixed_time_of_day: true\n"
                  "  has_npcs: false\n  doors: null\n  toilet: null\n  loading_name: 0\n");
    files.addFile("packs/my_mod/map/my_mod/meadow/data.yaml", dwe::assets::testing::fixtureMap());
    files.addFile("packs/my_mod/rules/trips.yaml", "stops: [twna01, \"my_mod:meadow\"]\n");
    const std::array<std::filesystem::path, 1> with{"content/base"};
    const auto report = validatePacks(files, "packs/my_mod", "content/schemas", with);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK_MESSAGE(textOf(*report).empty(), textOf(*report));

    files.addFile("packs/my_mod/rules/trips.yaml", "stops: [twna01, \"my_mod:swamp\"]\nrooms: [hall]\n");
    const auto unknown = validatePacks(files, "packs/my_mod", "content/schemas", with);
    REQUIRE_MESSAGE(unknown.has_value(), errorOf(unknown));
    const std::string text = textOf(*unknown);
    CHECK(text.contains("rules/trips.yaml:1:"));
    CHECK(text.contains("'my_mod:swamp' is no entry of data/maps"));
    CHECK(text.contains("entryReference 'data/rooms' is no content table"));
    CHECK_FALSE(text.contains("twna01"));
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it).
TEST_CASE("the user's disc, extracted: the example mod passes dwx validate over the base and the game pack") {
    const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
    if (!content) {
        MESSAGE("DWE_CONTENT is not set: skipping the example mod's validation");
        return;
    }
    const dwe::assets::DiskFileSystem disk;
    const std::array<std::filesystem::path, 2> with{*content / "base", DWE_GAME_PACK};
    const auto report = validatePacks(disk, DWE_EXAMPLE_MOD, DWE_SCHEMA_DIR, with);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK_MESSAGE(report->problems.empty(), textOf(*report));
    // Every model of the base against its contract, and the mod's new one against its own.
    std::size_t baseContracts = 0;
    for (const auto& species : std::filesystem::directory_iterator{*content / "base" / "digimon"}) {
        baseContracts += std::filesystem::exists(species.path() / "contract.yaml") ? 1 : 0;
    }
    CHECK(baseContracts > 0);
    CHECK(report->contracts == baseContracts + 1);
}

TEST_CASE("dwx validate checks the Lua API a pack asks for as the game does") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    const auto validateWithApi = [&files](std::string_view api) {
        files.addFile("packs/extra/pack.yaml", std::format(R"(id: extra
version: 1.0.0
layer: mods
name: Extra
api: "{}"
dependencies:
  - id: base
    min_version: 1.0.0
)",
                                                           api));
        const std::array<std::filesystem::path, 1> with{"content/base"};
        return validatePacks(files, "packs/extra", "content/schemas", with);
    };
    for (const std::string_view supported : {"1.x", "1.0"}) {
        CAPTURE(supported);
        const auto report = validateWithApi(supported);
        REQUIRE_MESSAGE(report.has_value(), errorOf(report));
        CHECK(textOf(*report).empty());
    }
    // The manifest reader refuses the grammar, so the pack does not mount.
    CHECK(errorOf(validateWithApi("one")).contains("api 'one' is not MAJOR.x or MAJOR.MINOR"));
    for (const std::string_view unsupported : {"2.x", "1.99"}) {
        CAPTURE(unsupported);
        const auto report = validateWithApi(unsupported);
        REQUIRE_MESSAGE(report.has_value(), errorOf(report));
        CHECK(textOf(*report) ==
              std::format(
                  "packs/extra/pack.yaml: pack 'extra': the scripts need the Lua API {}, and this engine has {}\n",
                  unsupported, dwe::assets::formatVersion(dwe::assets::SupportedLuaApi)));
    }
}

TEST_CASE("a pack that the game's readers refuse fails validate") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("packs/mod/pack.yaml", R"(id: mod
version: 1.0.0
layer: mods
name: Mod
dependencies:
  - id: base
    min_version: 1.0.0
)");
    // Each file follows its schema, but the game cannot load it.
    files.addFile("packs/mod/model/door/door00.glb", clipModel(0));
    files.addFile("packs/mod/map/town/data.yaml", dwe::assets::testing::fixtureMap() + "weather: rain\n");
    std::string missingKey = dwe::assets::testing::fixtureMap();
    const std::string digimonCount = "digimon_count: 1\n";
    missingKey.erase(missingKey.find(digimonCount), digimonCount.size());
    files.addFile("packs/mod/map/swamp/data.yaml", missingKey);
    files.addFile("packs/mod/data/maps.yaml", std::string{dwe::assets::testing::FixtureTable} + "weather: []\n");
    const std::array<std::filesystem::path, 1> with{"content/base"};
    const auto report = validatePacks(files, "packs/mod", "content/schemas", with);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    const std::string text = textOf(*report);
    CHECK(report->problems.size() == 4);
    CHECK(text.contains("packs/mod/data/maps.yaml: the engine cannot load data/maps: "));
    CHECK(text.contains("unknown key 'weather'"));
    CHECK(text.contains("packs/mod/map/swamp/data.yaml: the engine cannot load map/swamp/data: "));
    CHECK(text.contains("'digimon_count'"));
    CHECK(text.contains("packs/mod/map/town/data.yaml: the engine cannot load map/town/data: "));
    CHECK(text.contains("packs/mod/model/door/door00.glb: the engine cannot load model/door/door00: "));
    CHECK(text.contains("extras.tickCount must not be 0"));
}

TEST_CASE("dwx validate compiles every Lua file of a pack and names the one that does not") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("packs/mod/pack.yaml", R"(id: mod
version: 1.0.0
layer: mods
name: Mod
dependencies:
  - id: base
    min_version: 1.0.0
)");
    files.addFile("packs/mod/story/900.lua", "local story <const> = story\nreturn { script = 900, sections = {} }\n");
    files.addFile("packs/mod/story/901.lua", "return { script = 901, sections = {\n");
    const std::array<std::filesystem::path, 1> with{"content/base"};
    const auto report = validatePacks(files, "packs/mod", "content/schemas", with);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    const std::string text = textOf(*report);
    CHECK_MESSAGE(report->problems.size() == 1, text);
    CHECK(text.contains("packs/mod/story/901.lua: the Lua does not compile: story/901:"));
}

TEST_CASE("dwx validate checks every language against English") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("content/base/text/en/main.yaml", "map_names.0001: File City\nmove_names.0000: \"{color:2}Fire\"\n");
    files.addFile("packs/es/pack.yaml", R"(id: es
version: 1.0.0
layer: mods
name: Spanish
dependencies:
  - id: base
    min_version: 1.0.0
)");
    // A partial translation of the original's text is fine; a key English lacks and other placeholders are not.
    files.addFile("packs/es/text/es/main.yaml", "map_names.0001: Ciudad Archivo\nmap_names.0002: Extra\n"
                                                "move_names.0000: Fuego\n");
    files.addFile("packs/es/localization/en/menu.yaml", "title: Pause\nquit: Quit\n");
    files.addFile("packs/es/localization/es/menu.yaml", "title: \"{name}\"\n");
    const std::array<std::filesystem::path, 1> with{"content/base"};
    const auto report = validatePacks(files, "packs/es", "content/schemas", with);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    const std::string text = textOf(*report);
    MESSAGE(text);
    CHECK(report->problems.size() == 4);
    CHECK(text.contains("packs/es/text/es/main.yaml: the translation text/es/main has the key main.map_names.0002"));
    CHECK(text.contains("has the key main.move_names.0000 with the placeholders {}, where en has {color:2}"));
    CHECK(text.contains("packs/es/localization/es/menu.yaml: the translation localization/es/menu misses the key "
                        "menu.quit"));
    CHECK(text.contains("has the key menu.title with the placeholders {name}, where en has {}"));
}

TEST_CASE("dwx validate reads a translation's lock and finds what changed in English since") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("content/base/text/en/main.yaml", "map_names.0001: File City\nmap_names.0002: Native Forest\n"
                                                    "move_names.0000: Fire\nmove_names.0001: Spit Fire\n");
    files.addFile("packs/es/pack.yaml", R"(id: es
version: 1.0.0
layer: mods
name: Spanish
dependencies:
  - id: base
    min_version: 1.0.0
)");
    files.addFile("packs/es/text/es/main.yaml", "map_names.0001: Ciudad Archivo\nmap_names.0002: Bosque Nativo\n"
                                                "move_names.0001: Spit Fire\n");
    files.addFile("packs/es/text/es/.sources.lock",
                  std::format("sources:\n  main.map_names.0001: {}\n  main.map_names.0002: {}\n"
                              "  main.nothing.0000: {}\nkept_english: [main.move_names.0000, main.move_names.0001]\n",
                              dwe::assets::sourceHash("File City"), dwe::assets::sourceHash("Native forest"),
                              dwe::assets::sourceHash("x")));
    const std::array<std::filesystem::path, 1> with{"content/base"};
    const auto report = validatePacks(files, "packs/es", "content/schemas", with);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    const std::string text = textOf(*report);
    MESSAGE(text);
    CHECK(report->problems.size() == 3);
    CHECK(text.contains(std::format("packs/es/text/es/.sources.lock: main.map_names.0002 was translated from another "
                                    "English text: check it and write the new hash, {}",
                                    dwe::assets::sourceHash("Native Forest"))));
    CHECK(text.contains("main.move_names.0001 is kept as it is, but the translation has it"));
    CHECK(text.contains("names main.nothing.0000, which the English text does not have"));
}

TEST_CASE("dwx validate cannot check a folder without packs") {
    MemoryFileSystem files;
    files.addFile("content/readme.yaml", "a: 1\n");
    CHECK(errorOf(validatePacks(files, "content", "content/schemas", {}, dwe::assets::PackOrigin::Engine))
              .contains("has no pack.yaml"));
}

TEST_CASE("the schemas are next to the packs by default") {
    MemoryFileSystem files;
    addBasePack(files);
    CHECK(defaultSchemaFolder(files, "content/base") == "content/schemas");
    CHECK(defaultSchemaFolder(files, "content/base/") == "content/schemas");
    CHECK(defaultSchemaFolder(files, "content") == "content/schemas");
}

TEST_CASE("every schema the base pack names compiles with the annotation keywords of dwx validate") {
    std::vector<std::string_view> schemas{ManifestSchema};
    for (const auto& rule : names::documentSchemas()) {
        schemas.push_back(rule.schema);
        for (const std::string_view pattern : rule.ids) {
            CHECK_MESSAGE(dwe::assets::checkIdPattern(pattern).has_value(), pattern);
        }
    }
    const std::array<std::string_view, 2> keywords{AssetReferenceKeyword, EntryReferenceKeyword};
    for (const std::string_view name : schemas) {
        const auto text = dwe::core::parseYaml(readText(std::format("{}/{}.schema.json", DWE_SCHEMA_DIR, name)));
        REQUIRE_MESSAGE(text.has_value(), name);
        const auto schema = dwe::core::JsonSchema::compile(*text, keywords);
        CHECK_MESSAGE(schema.has_value(), std::format("{}: {}", name, errorOf(schema)));
    }
}

TEST_CASE("the user's disc, extracted: dwx validate finds no problem in the game pack on the extracted base pack") {
    const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
    if (!content) {
        MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
        return;
    }
    const std::array<std::filesystem::path, 1> with{*content / "base"};
    const auto report = validatePacks(dwe::assets::DiskFileSystem{}, DWE_GAME_PACK, DWE_SCHEMA_DIR, with,
                                      dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(report->packs == 2);
    CHECK_MESSAGE(report->problems.empty(), textOf(*report));
    MESSAGE(report->documents, " documents, ", report->assets, " assets, ", report->references, " references");
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it).
TEST_CASE("the user's disc, extracted: a mod with copies of every original map's data passes dwx validate") {
    const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
    if (!content) {
        MESSAGE("DWE_CONTENT is not set: skipping the copied maps' validation");
        return;
    }
    // A mod that edits the original's maps in place: each map's data copied as it is, as an editor saves it.
    const TempFolder folder;
    folder.write("copies/pack.yaml", R"(id: copies
version: 1.0.0
layer: mods
name: Copies
dependencies:
  - id: base
    min_version: 0.1.0
schemas:
  map: ["map/*/data"]
)");
    std::size_t copied = 0;
    for (const auto& map : std::filesystem::directory_iterator{*content / "base" / "map"}) {
        const std::filesystem::path data = map.path() / "data.yaml";
        if (!std::filesystem::exists(data)) {
            continue;
        }
        const std::filesystem::path target = folder.path() / "copies" / "map" / map.path().filename();
        std::filesystem::create_directories(target);
        std::filesystem::copy_file(data, target / "data.yaml");
        ++copied;
    }
    REQUIRE(copied > 0);
    const std::array<std::filesystem::path, 1> with{*content / "base"};
    const auto report = validatePacks(dwe::assets::DiskFileSystem{}, folder.path() / "copies", DWE_SCHEMA_DIR, with);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK_MESSAGE(report->problems.empty(), textOf(*report));
    // Every copy was checked against the map schema, and so had its warps resolved as a mod's.
    CHECK(report->documents >= copied);
    MESSAGE(copied, " maps copied, ", report->documents, " documents, ", report->problems.size(), " problems");
}

TEST_CASE("dwx validate checks a pack as a mod unless told it is the game's: no engine layer, no base rights") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    files.addFile("packs/fake/pack.yaml", R"(id: fake
version: 1.0.0
layer: base
name: Fake
dependencies:
  - id: base
    min_version: 1.0.0
)");
    files.addFile("packs/fake/data/maps.yaml", "maps:\n- id: meadow\n");
    const std::array<std::filesystem::path, 1> with{"content/base"};
    const std::string_view layerProblem =
        "packs/fake/pack.yaml: pack 'fake' is a mod and declares the base layer, which "
        "only the engine's own packs have: a mod's layer is mods or user\n";
    const std::string_view namespaceProblem = "'fake:<name>'";
    // A mod by default, as dwx validate checks a folder without --engine: the layer and the new entry without the
    // pack's namespace are both refused, as the game refuses them.
    const auto asMod = validatePacks(files, "packs/fake", "content/schemas", with);
    REQUIRE_MESSAGE(asMod.has_value(), errorOf(asMod));
    CHECK(textOf(*asMod).contains(layerProblem));
    CHECK(textOf(*asMod).contains(namespaceProblem));
    // Checked as one of the engine's own packs, the base layer and its rights are its own.
    const auto asEngine = validatePacks(files, "packs/fake", "content/schemas", with, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(asEngine.has_value(), errorOf(asEngine));
    CHECK_FALSE(textOf(*asEngine).contains(layerProblem));
    CHECK_FALSE(textOf(*asEngine).contains(namespaceProblem));
}
