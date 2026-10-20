#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_store.hpp>
#include <dwe/ruleset/ruleset_values.hpp>
#include <dwe/ruleset/section_file.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "ruleset_fixture.hpp"
#include "temp_folder.hpp"

namespace dwe::ruleset {

namespace {

using testing::errorOf;
using testing::FixturePacks;

// The text of a result, or its error, so doctest can compare it.
std::string textOf(const core::Result<std::string>& result) {
    return result ? *result : result.error().message;
}

core::YamlNode parsed(std::string_view text) {
    auto node = core::parseYaml(text);
    REQUIRE_MESSAGE(node.has_value(), errorOf(node));
    return *node;
}

// A care section with the decomp citations a real one has, a quoted string, a flow sequence and a non-ASCII comment.
constexpr std::string_view CitedCare = R"(# Cuidado: cabecera con tildes, ñ.
hunger:
  # partner_impl.c: tickHungerMechanics: hora y media.
  window_minutes: 90   # comentario al final
  label: "Hambre"
  hours: [2, 3]
selection: original
)";

std::string readFile(const std::filesystem::path& file) {
    std::ifstream stream{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("the values of a document are listed by dotted path, in the document's order") {
    const auto values = listValues("care", parsed(CitedCare));
    const std::vector<RulesetValue> expected{
        {.section = "care", .path = "hunger.window_minutes", .yaml = "90"},
        {.section = "care", .path = "hunger.label", .yaml = "\"Hambre\""},
        {.section = "care", .path = "hunger.hours.0", .yaml = "2"},
        {.section = "care", .path = "hunger.hours.1", .yaml = "3"},
        {.section = "care", .path = "selection", .yaml = "original"},
    };
    CHECK(values == expected);
    CHECK(listValues("x", parsed("list: []\n")).front().yaml == "[]");
}

TEST_CASE("two rulesets differ in the values one changes, adds or lacks") {
    FixturePacks packs;
    packs.files.addFile("rules/rules/hard/ruleset.yaml",
                        "id: hard\nname: Hard\nversion: 0.1.0\nextends: small\nsections: [care]\n");
    packs.files.addFile("rules/rules/hard/care.yaml", "hunger:\n  window_minutes: 30\n  extra: 1\n");
    const assets::VirtualFileSystem& vfs = packs.mount();
    CHECK(listRulesets(vfs) == std::vector<std::string>{"hard", "small"});
    const auto small = loadRuleset(vfs, "small");
    const auto hard = loadRuleset(vfs, "hard");
    REQUIRE_MESSAGE(small.has_value(), errorOf(small));
    REQUIRE_MESSAGE(hard.has_value(), errorOf(hard));
    const std::vector<ValueDifference> expected{
        {.section = "care", .path = "hunger.extra", .before = {}, .after = "1"},
        {.section = "care", .path = "hunger.window_minutes", .before = "90", .after = "30"},
        {.section = "care", .path = "selection", .before = "original", .after = {}},
    };
    CHECK(diffValues(*small, *hard) == expected);
    CHECK(diffValues(*small, *small).empty());
}

TEST_CASE("a value is written as YAML on one line, quoted when it must be") {
    CHECK(textOf(inlineYaml(core::YamlNode::scalar("45"))) == "45");
    CHECK(textOf(inlineYaml(core::YamlNode::scalar("Hard", core::YamlNode::ScalarStyle::Quoted))) == "\"Hard\"");
    // A plain text that would read as a mapping is quoted, so it stays text.
    CHECK(textOf(inlineYaml(core::YamlNode::scalar("a: b"))) != "a: b");
}

TEST_CASE("changed values are written back in place, keeping the citations and the layout") {
    const core::YamlNode values = parsed(CitedCare);
    auto edited = core::parseYaml(R"(hunger:
  window_minutes: 45
  label: "Hambre fuerte"
  hours: [2, 4]
selection: newest
)");
    REQUIRE(edited.has_value());
    const auto patched = patchSectionText(CitedCare, *edited);
    REQUIRE_MESSAGE(patched.has_value(), errorOf(patched));
    CHECK(*patched == R"(# Cuidado: cabecera con tildes, ñ.
hunger:
  # partner_impl.c: tickHungerMechanics: hora y media.
  window_minutes: 45   # comentario al final
  label: "Hambre fuerte"
  hours: [2, 4]
selection: newest
)");
    // Nothing changed: the text stays byte for byte.
    CHECK(textOf(patchSectionText(CitedCare, values)) == CitedCare);
}

TEST_CASE("a change of shape is not written back") {
    const auto patched = patchSectionText(CitedCare, parsed("hunger: 1\nselection: original\n"));
    REQUIRE_FALSE(patched.has_value());
    CHECK(errorOf(patched).contains("hunger"));
    CHECK_FALSE(patchSectionText(CitedCare, parsed("hunger: {}\n")).has_value());
}

TEST_CASE("a tuned section is saved to its file on disk, and the file loads with the tuned value") {
    const assets::testing::TempFolder folder;
    folder.write("rules/pack.yaml", "id: rules\nversion: 1.0.0\nlayer: mods\n");
    folder.write("rules/data/digimon.yaml", "digimon: []\n");
    folder.write("rules/data/raise.yaml", "raise: []\n");
    folder.write("rules/rules/small/ruleset.yaml",
                 "id: small\nname: Small\nversion: 1.0.0\ntables: {digimon: data/digimon, raise: data/raise}\n"
                 "sections: [clock, care]\n");
    folder.write("rules/rules/small/clock.yaml", testing::Clock);
    folder.write("rules/rules/small/care.yaml", CitedCare);
    const assets::DiskFileSystem disk;
    const std::array<std::filesystem::path, 1> roots{folder.path() / "rules"};
    const auto vfs = assets::VirtualFileSystem::mount(disk, roots);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    auto store = RulesetStore::load(*vfs, "small");
    REQUIRE_MESSAGE(store.has_value(), errorOf(store));
    RulesetStore& rules = *store;
    REQUIRE(rules.setValue("care", "hunger.window_minutes", "60").has_value());

    const auto saved = saveSection(*vfs, *rules.current().section("care"));
    REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
    CHECK(*saved == folder.path() / "rules/rules/small/care.yaml");
    const std::string text = readFile(*saved);
    CHECK(text.contains("# partner_impl.c: tickHungerMechanics: hora y media."));
    CHECK(text.contains("window_minutes: 60   # comentario al final"));
    REQUIRE(rules.reload().has_value());
    CHECK(findValue(rules.current().section("care")->node, "hunger.window_minutes")->text() == "60");
}

TEST_CASE("a section of a pack in memory cannot be saved") {
    FixturePacks packs;
    const assets::VirtualFileSystem& vfs = packs.mount();
    const auto ruleset = loadRuleset(vfs, "small");
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    const Section* care = ruleset->section("care");
    REQUIRE(care != nullptr);
    const auto saved = saveSection(vfs, *care);
    REQUIRE_FALSE(saved.has_value());
    CHECK(errorOf(saved).contains("not a file on disk"));
}

} // namespace dwe::ruleset
