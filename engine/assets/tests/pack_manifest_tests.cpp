#include <dwe/assets/pack_manifest.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using dwe::assets::ApiRequirement;
using dwe::assets::formatApiRequirement;
using dwe::assets::formatVersion;
using dwe::assets::Layer;
using dwe::assets::PackManifest;
using dwe::assets::parseApiRequirement;
using dwe::assets::parsePackManifest;
using dwe::assets::parseVersion;
using dwe::assets::Version;

namespace {

std::string errorOf(std::string_view text) {
  const auto manifest = parsePackManifest(text);
  REQUIRE_FALSE(manifest.has_value());
  return manifest.error().message;
}

} // namespace

TEST_CASE("versions are MAJOR.MINOR.PATCH") {
  CHECK(*parseVersion("1.4.0") == Version{.major = 1, .minor = 4, .patch = 0});
  CHECK(*parseVersion("0.0.12") < *parseVersion("0.1.0"));
  CHECK(*parseVersion("2.0.0") > *parseVersion("1.99.99"));
  CHECK(formatVersion(Version{.major = 3, .minor = 0, .patch = 7}) == "3.0.7");
  for (const char* text : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.2.x", "1.2.3-beta", "-1.2.3",
                           "1..3", "4294967296.0.0"}) {
    CHECK_FALSE_MESSAGE(parseVersion(text).has_value(), text);
  }
}

TEST_CASE("a full manifest is read") {
  const auto manifest = parsePackManifest("id: hd_backgrounds\n"
                                          "version: 1.2.0\n"
                                          "layer: remaster\n"
                                          "priority: -3\n"
                                          "name: HD backgrounds\n"
                                          "description: Upscaled map backgrounds.\n"
                                          "dependencies:\n"
                                          "  - id: base\n"
                                          "    min_version: 1.0.0\n"
                                          "  - id: shared_palettes\n"
                                          "    min_version: \"0.3.1\"\n");
  REQUIRE_MESSAGE(manifest.has_value(), manifest.error().message);
  CHECK(manifest->id == "hd_backgrounds");
  CHECK(manifest->version == Version{.major = 1, .minor = 2, .patch = 0});
  CHECK(manifest->layer == Layer::Remaster);
  CHECK(manifest->priority == -3);
  CHECK(manifest->name == "HD backgrounds");
  CHECK(manifest->description == "Upscaled map backgrounds.");
  REQUIRE(manifest->dependencies.size() == 2);
  CHECK(manifest->dependencies[1].id == "shared_palettes");
  CHECK(manifest->dependencies[1].minVersion == Version{.major = 0, .minor = 3, .patch = 1});
}

TEST_CASE("an API requirement is a major version and .x, or a major and a minimum minor version") {
  CHECK(*parseApiRequirement("1.x") == ApiRequirement{.major = 1, .minimumMinor = std::nullopt});
  CHECK(*parseApiRequirement("0.x") == ApiRequirement{.major = 0, .minimumMinor = std::nullopt});
  CHECK(*parseApiRequirement("1.3") == ApiRequirement{.major = 1, .minimumMinor = 3});
  CHECK(*parseApiRequirement("2.0") == ApiRequirement{.major = 2, .minimumMinor = 0});
  CHECK(formatApiRequirement(ApiRequirement{.major = 12, .minimumMinor = std::nullopt}) == "12.x");
  CHECK(formatApiRequirement(ApiRequirement{.major = 1, .minimumMinor = 4}) == "1.4");
  for (const char* text : {"", "1", "x", ".x", "1.", "1.0.0", "1.x.x", "01.x", "1.01", "1.X",
                           "-1.x", "1.-1", "4294967296.x", "1.4294967296", "x.1"}) {
    CHECK_FALSE_MESSAGE(parseApiRequirement(text).has_value(), text);
  }
}

TEST_CASE("a manifest may declare the Lua API of its scripts") {
  const auto manifest =
      parsePackManifest("id: my_mod\nversion: 1.0.0\nlayer: mods\napi: \"1.x\"\n");
  REQUIRE_MESSAGE(manifest.has_value(), manifest.error().message);
  CHECK(manifest->api == ApiRequirement{.major = 1, .minimumMinor = std::nullopt});
  CHECK(errorOf("id: a\nversion: 1.0.0\nlayer: mods\napi: 1.2.0\n") ==
        "line 4, column 6: api '1.2.0' is not MAJOR.x or MAJOR.MINOR, such as 1.x or 1.2");
}

TEST_CASE("a minimal manifest gets the defaults") {
  const auto manifest = parsePackManifest("id: base\nversion: 0.1.0\nlayer: base\n");
  REQUIRE(manifest.has_value());
  CHECK(manifest->priority == 0);
  CHECK(manifest->name.empty());
  CHECK(manifest->dependencies.empty());
  CHECK_FALSE(manifest->api.has_value());
}

TEST_CASE("manifest errors say what is wrong and where") {
  CHECK(errorOf("version: 1.0.0\nlayer: base\n") ==
        "line 1, column 1: a manifest needs 'id', 'version' and 'layer'");
  CHECK(errorOf("- id: base\n") == "line 1, column 1: a manifest must be a mapping");
  CHECK(errorOf("id: Base\nversion: 1.0.0\nlayer: base\n") ==
        "line 1, column 5: pack id 'Base' may only use a-z, 0-9 and '_'");
  CHECK(errorOf("id: base\nversion: 1.0\nlayer: base\n") ==
        "line 2, column 10: version '1.0' is not MAJOR.MINOR.PATCH");
  CHECK(errorOf("id: base\nversion: 1.0.0\nlayer: dlc\n") ==
        "line 3, column 8: unknown layer 'dlc': use base, remaster, mods or user");
  CHECK(errorOf("id: base\nversion: 1.0.0\nlayer: base\npriority: high\n") ==
        "line 4, column 11: priority 'high' is not a whole number");
  CHECK(errorOf("id: base\nversion: 1.0.0\nlayer: base\nauthor: me\n") ==
        "line 4, column 9: unknown key 'author'");
  CHECK(errorOf("id: [a]\nversion: 1.0.0\nlayer: base\n") ==
        "line 1, column 5: 'id' must be a single value");
  CHECK(
      errorOf("id: base\nversion: 1.0.0\nlayer: base\nid: other\n").contains("duplicate key 'id'"));
  CHECK(errorOf("id: a\nversion: 1.0.0\nlayer: mods\ndependencies: base\n") ==
        "line 4, column 15: 'dependencies' must be a list");
  CHECK(errorOf("id: a\nversion: 1.0.0\nlayer: mods\ndependencies:\n- id: base\n") ==
        "line 5, column 3: a dependency needs 'id' and 'min_version'");
  CHECK(errorOf(
            "id: a\nversion: 1.0.0\nlayer: mods\ndependencies:\n- {id: a, min_version: 1.0.0}\n") ==
        "line 5, column 3: a pack cannot depend on itself");
  CHECK(errorOf("id: a\nversion: 1.0.0\nlayer: mods\ndependencies:\n- {id: b, min_version: 1.0.0}\n"
                "- {id: b, min_version: 2.0.0}\n") == "line 6, column 3: pack 'b' is listed twice");
  CHECK(
      errorOf(
          "id: a\nversion: 1.0.0\nlayer: mods\ndependencies:\n- {id: b, min_version: 1.0.0, max: 2}\n") ==
      "line 5, column 36: unknown dependency key 'max'");
  CHECK(errorOf("id: [unclosed\n").starts_with("line 2, column 1:"));
}

TEST_CASE("a manifest maps schemas to the ids of the documents that follow them") {
  const auto manifest = parsePackManifest("id: base\nversion: 0.1.0\nlayer: base\n"
                                          "schemas:\n"
                                          "  digimon: [data/digimon]\n"
                                          "  image: [\"card/*_meta\", \"texture/**_meta\"]\n");
  REQUIRE(manifest.has_value());
  REQUIRE(manifest->schemas.size() == 2);
  CHECK(manifest->schemas[0].schema == "digimon");
  CHECK(manifest->schemas[0].ids == std::vector<std::string>{"data/digimon"});
  CHECK(manifest->schemas[1].ids == std::vector<std::string>{"card/*_meta", "texture/**_meta"});
  CHECK(errorOf("id: a\nversion: 1.0.0\nlayer: mods\nschemas: [a]\n") ==
        "line 4, column 10: 'schemas' must be a mapping from schema names to lists of id patterns");
  CHECK(errorOf("id: a\nversion: 1.0.0\nlayer: mods\nschemas:\n  Map: [a]\n") ==
        "line 5, column 8: schema name 'Map' must have 1 to 64 characters of a-z, 0-9 and '_'");
  CHECK(errorOf("id: a\nversion: 1.0.0\nlayer: mods\nschemas:\n  map: []\n") ==
        "line 5, column 8: schema 'map' needs a list of id patterns");
  CHECK(errorOf("id: a\nversion: 1.0.0\nlayer: mods\nschemas:\n  map: [a/b.yaml]\n") ==
        "line 5, column 9: id pattern 'a/b.yaml' may only use a-z, 0-9, '_', '/' and '*'");
}
