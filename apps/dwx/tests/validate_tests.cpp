#include <dwe/assets/file_system.hpp>
#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "asset_names.hpp"
#include "media_fixtures.hpp"
#include "validate.hpp"

using namespace dwe::dwx;
using dwe::assets::MemoryFileSystem;

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

// The schemas of the tests: a sound bank's metadata names its SoundFont and a map, a map has a name.
void addSchemas(MemoryFileSystem& files) {
    files.addFile("content/schemas/pack.schema.json", R"({"type": "object", "required": ["id", "name"]})");
    files.addFile("content/schemas/sound.schema.json", R"({
        "type": "object", "required": ["bank", "count"], "additionalProperties": false,
        "properties": {
            "bank": {"type": "string", "assetReference": "./{}"},
            "map": {"type": "string", "assetReference": "map/{}/data"},
            "count": {"type": "integer"}}})");
    files.addFile("content/schemas/map.schema.json", R"({"type": "object", "required": ["name"]})");
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
  model_contract: ["digimon/*/contract"]
)");
    files.addFile("content/base/sound/hit/bank_meta.yaml", "bank: bank.sf2\nmap: town\ncount: 3\n");
    files.addFile("content/base/sound/hit/bank.sf2", dwe::assets::testing::soundFontFile());
    files.addFile("content/base/map/town/data.yaml", "name: town\n");
    files.addFile("content/base/digimon/agum/model.gltf", skinnedModel(false));
    files.addFile("content/base/digimon/agum/contract.yaml",
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

} // namespace

TEST_CASE("a pack whose documents follow their schemas and whose references resolve has no problems") {
    MemoryFileSystem files;
    addSchemas(files);
    addBasePack(files);
    const auto report = validatePacks(files, "content/base", "content/schemas");
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(textOf(*report).empty());
    CHECK(report->packs == 1);
    CHECK(report->assets == 5);
    CHECK(report->documents == 4);  // pack.yaml and the three YAML assets
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
    files.addFile("content/broken/digimon/gabu/model.gltf", skinnedModel(true));
    files.addFile("content/broken/digimon/agum/contract.yaml",
                  "model: model.gltf\njoint_parents: [-1, 0, 0]\nanimations: [walk, run]\n");
    const auto report = validatePacks(files, "content", "content/schemas");
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    CHECK(report->packs == 2);
    const std::string expected =
        R"(content/base/digimon/agum/model.gltf: breaks its contract content/broken/digimon/agum/contract.yaml: it lacks the animations run
content/base/digimon/agum/model.gltf: breaks its contract content/broken/digimon/agum/contract.yaml: joint 2 has the parent 1, the contract says 0
content/base/sound/hit/bank_meta.yaml: the rules of several schemas match this document: map (sound/hit/bank_meta), sound (sound/*/bank_meta)
content/broken/digimon/gabu/model.gltf: the model has no contract: digimon/gabu/contract is missing
content/broken/digimon/gabu/model.gltf: the model uses 'skin.png', which names digimon/gabu/skin, which no pack has
content/broken/loose/b.yaml: no schema rule of the packs matches this document
content/broken/map/swamp/data.yaml: line 2, column 1: did not find expected ',' or ']' (while parsing a flow sequence at line 1, column 7)
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

TEST_CASE("dwx validate cannot check a folder without packs") {
    MemoryFileSystem files;
    files.addFile("content/readme.yaml", "a: 1\n");
    CHECK(errorOf(validatePacks(files, "content", "content/schemas")).contains("has no pack.yaml"));
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
    const std::array<std::string_view, 1> keywords{AssetReferenceKeyword};
    for (const std::string_view name : schemas) {
        const auto text = dwe::core::parseYaml(readText(std::format("{}/{}.schema.json", DWE_SCHEMA_DIR, name)));
        REQUIRE_MESSAGE(text.has_value(), name);
        const auto schema = dwe::core::JsonSchema::compile(*text, keywords);
        CHECK_MESSAGE(schema.has_value(), std::format("{}: {}", name, errorOf(schema)));
    }
}
