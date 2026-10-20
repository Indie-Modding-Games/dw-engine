#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_store.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "temp_folder.hpp"
#include "tuning_model.hpp"

namespace dwe::debug {

namespace {

template <typename T>
std::string errorOf(const core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// The text of a result, or its error, so doctest can compare it.
std::string textOf(const core::Result<std::string>& result) {
    return result ? *result : result.error().message;
}

constexpr std::string_view Clock = R"(# El reloj (clock.c).
frames_per_second: 20
# clock.c: tickGameClock: un minuto cada 20 fotogramas.
frames_per_minute: 20
days_per_year: 30
year_limit: 100
start: {year: 0, day: 0, hour: 8, minute: 0}
)";

constexpr std::string_view Care = R"(hunger:
  # partner_impl.c: tickHungerMechanics: hora y media.
  window_minutes: 90
  label: "Hambre"
selection: original
quirks:
  # partner_impl.c: handleSleeping.
  sleeping_hungry_keeps_weight: true
)";

// The care section's check, as its system would give it: the window must be a number above 0.
core::Result<void> checkCare(const ruleset::Section& section) {
    const core::YamlNode* window = ruleset::findValue(section.node, "hunger.window_minutes");
    if (window == nullptr || !window->isScalar()) {
        return core::fail("window_minutes is missing");
    }
    if (window->text().starts_with('-') || window->text() == "0") {
        return std::unexpected{section.error(*window, "window_minutes must be more than zero")};
    }
    return {};
}

// The text of a file of the game's own pack.
std::string gamePackText(std::string_view relative) {
    std::ifstream stream{std::filesystem::path{DWE_GAME_PACK} / relative, std::ios::binary};
    REQUIRE_MESSAGE(stream.good(), relative);
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

// The game's save section, so the panel is tried on a real section and not only on the samples of these tests.
constexpr std::string_view GameSaveSection = "rules/original/save.yaml";

// A rules pack on disk with the rulesets `small` and `hard` (which extends small), so saving can write its files.
// `small` also has the game's own save section.
class DiskPacks {
public:
    DiskPacks() {
        folder_.write("rules/pack.yaml", "id: rules\nversion: 1.0.0\nlayer: mods\n");
        folder_.write("rules/rules/small/ruleset.yaml",
                      "id: small\nname: Small\nversion: 1.0.0\nsections: [clock, care, save]\n");
        folder_.write("rules/rules/small/save.yaml", gamePackText(GameSaveSection));
        folder_.write("rules/rules/small/clock.yaml", Clock);
        folder_.write("rules/rules/small/care.yaml", Care);
        folder_.write("rules/rules/hard/ruleset.yaml",
                      "id: hard\nname: Hard\nversion: 1.0.0\nextends: small\nsections: [care]\n");
        folder_.write("rules/rules/hard/care.yaml",
                      "hunger:\n  window_minutes: 30\n  label: \"Hambre\"\n"
                      "selection: newest\nquirks:\n  sleeping_hungry_keeps_weight: false\n");
        const std::array<std::filesystem::path, 1> roots{folder_.path() / "rules"};
        auto mounted = assets::VirtualFileSystem::mount(disk_, roots);
        REQUIRE_MESSAGE(mounted.has_value(), errorOf(mounted));
        const assets::VirtualFileSystem& files = vfs_.emplace(std::move(*mounted));
        auto loaded = ruleset::RulesetStore::load(files, "small", {{"care", checkCare}});
        REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
        ruleset::RulesetStore& rules = store_.emplace(std::move(*loaded));
        auto clockRules = ruleset::readClockRules(rules.current());
        REQUIRE_MESSAGE(clockRules.has_value(), errorOf(clockRules));
        target_ = {.files = &files, .store = &rules, .clock = &clock_.emplace(*clockRules)};
    }

    [[nodiscard]] TuningTarget target() const { return target_; }

    [[nodiscard]] std::string fileText(std::string_view relative) const {
        std::ifstream stream{folder_.path() / "rules" / relative, std::ios::binary};
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

private:
    assets::testing::TempFolder folder_;
    assets::DiskFileSystem disk_;
    std::optional<assets::VirtualFileSystem> vfs_;
    std::optional<ruleset::RulesetStore> store_;
    std::optional<ruleset::GameClock> clock_;
    TuningTarget target_;
};

// A copy of the node at a path, or an empty group when there is none (and the test fails).
TuningNode nodeAt(const TuningNode& root, std::string_view path) {
    for (const TuningNode& child : root.children) {
        if (child.path == path) {
            return child;
        }
        if (path.starts_with(child.path + '.')) {
            return nodeAt(child, path);
        }
    }
    FAIL("no node at " << path);
    return {};
}

const TuningSection& sectionNamed(TuningModel& model, std::string_view name) {
    for (const TuningSection& section : model.sections()) {
        if (section.name == name) {
            return section;
        }
    }
    FAIL("no section " << name);
    return model.sections().front();
}

} // namespace

TEST_CASE("a section becomes a tree of groups and values, each edited by its kind") {
    auto document = core::parseYaml(Care);
    REQUIRE(document.has_value());
    const TuningNode root = buildTuningTree(*document);
    REQUIRE(root.children.size() == 3);
    const TuningNode& hunger = root.children[0];
    CHECK(hunger.kind == TuningValueKind::Group);
    CHECK(hunger.key == "hunger");
    CHECK(nodeAt(root, "hunger.window_minutes").kind == TuningValueKind::Number);
    CHECK(nodeAt(root, "hunger.window_minutes").text == "90");
    CHECK(nodeAt(root, "hunger.label").kind == TuningValueKind::Text);
    CHECK(nodeAt(root, "hunger.label").quoted);
    CHECK(nodeAt(root, "selection").kind == TuningValueKind::Text);
    CHECK(nodeAt(root, "quirks.sleeping_hungry_keeps_weight").kind == TuningValueKind::Flag);

    CHECK(textOf(yamlOfEdit(nodeAt(root, "hunger.window_minutes"), " 45 ")) == "45");
    CHECK_FALSE(yamlOfEdit(nodeAt(root, "hunger.window_minutes"), "soon").has_value());
    CHECK(textOf(yamlOfEdit(nodeAt(root, "quirks.sleeping_hungry_keeps_weight"), "false")) == "false");
    CHECK_FALSE(yamlOfEdit(nodeAt(root, "quirks.sleeping_hungry_keeps_weight"), "no").has_value());
    // A quoted text stays quoted; a strategy name stays plain.
    CHECK(textOf(yamlOfEdit(nodeAt(root, "hunger.label"), "Hambre fuerte")) == "\"Hambre fuerte\"");
    CHECK(textOf(yamlOfEdit(nodeAt(root, "selection"), "newest")) == "newest");
    CHECK_FALSE(yamlOfEdit(hunger, "1").has_value());
}

TEST_CASE("the model lists the rulesets, switches between them and shows the revision") {
    DiskPacks packs;
    TuningModel model{packs.target()};
    CHECK(model.rulesets() == std::vector<std::string>{"hard", "small"});
    CHECK(model.active().id == "small");
    CHECK(model.revision() == 1);
    REQUIRE(model.select("hard"));
    CHECK(model.active().id == "hard");
    CHECK(model.revision() == 2);
    CHECK(nodeAt(sectionNamed(model, "care").root, "hunger.window_minutes").text == "30");
    CHECK(sectionNamed(model, "clock").owner == "small");
    CHECK_FALSE(model.select("missing"));
    CHECK(model.active().id == "hard");
    CHECK_FALSE(model.error().empty());
}

TEST_CASE("an edit goes through the store; a refused one keeps the old value and says why") {
    DiskPacks packs;
    TuningModel model{packs.target()};
    const TuningNode window = nodeAt(sectionNamed(model, "care").root, "hunger.window_minutes");
    REQUIRE(model.edit("care", window, "45"));
    CHECK(model.revision() == 2);
    CHECK(model.error().empty());
    CHECK(model.status() == "care.hunger.window_minutes = 45");
    CHECK(nodeAt(sectionNamed(model, "care").root, "hunger.window_minutes").text == "45");

    // The care check refuses 0: the value stays 45 and the revision does not move.
    CHECK_FALSE(model.edit("care", window, "0"));
    CHECK(model.error().contains("window_minutes must be more than zero"));
    CHECK(model.revision() == 2);
    CHECK(nodeAt(sectionNamed(model, "care").root, "hunger.window_minutes").text == "45");

    const TuningNode quirk = nodeAt(sectionNamed(model, "care").root, "quirks.sleeping_hungry_keeps_weight");
    REQUIRE(model.edit("care", quirk, std::string{FlagOffText}));
    CHECK(nodeAt(sectionNamed(model, "care").root, "quirks.sleeping_hungry_keeps_weight").text == "false");
    CHECK(model.error().empty());
}

TEST_CASE("before reloading, the model shows the tuned values the files would put back") {
    DiskPacks packs;
    TuningModel model{packs.target()};
    REQUIRE(model.edit("care", nodeAt(sectionNamed(model, "care").root, "hunger.window_minutes"), "45"));
    REQUIRE(model.previewReload());
    REQUIRE(model.reloadChanges().size() == 1);
    CHECK(model.reloadChanges()[0].path == "hunger.window_minutes");
    CHECK(model.reloadChanges()[0].before == "45");
    CHECK(model.reloadChanges()[0].after == "90");
    REQUIRE(model.reload());
    CHECK(model.reloadChanges().empty());
    CHECK(nodeAt(sectionNamed(model, "care").root, "hunger.window_minutes").text == "90");
}

TEST_CASE("saving writes the tuned values into the section's file and keeps its citations") {
    DiskPacks packs;
    TuningModel model{packs.target()};
    REQUIRE(model.edit("care", nodeAt(sectionNamed(model, "care").root, "hunger.window_minutes"), "60"));
    REQUIRE(model.edit("care", nodeAt(sectionNamed(model, "care").root, "hunger.label"), "Hambre fuerte"));
    REQUIRE_MESSAGE(model.save("care"), model.error());
    CHECK(model.status().starts_with("section care saved to "));
    CHECK(packs.fileText("rules/small/care.yaml") == R"(hunger:
  # partner_impl.c: tickHungerMechanics: hora y media.
  window_minutes: 60
  label: "Hambre fuerte"
selection: original
quirks:
  # partner_impl.c: handleSleeping.
  sleeping_hungry_keeps_weight: true
)");
    // Reloading now reads the saved value.
    REQUIRE(model.previewReload());
    CHECK(model.reloadChanges().empty());
    CHECK_FALSE(model.save("missing"));
}

TEST_CASE("a section of a ruleset that extends another is saved to the file that provides it") {
    DiskPacks packs;
    TuningModel model{packs.target()};
    REQUIRE(model.select("hard"));
    const TuningNode frames = nodeAt(sectionNamed(model, "clock").root, "frames_per_minute");
    REQUIRE(model.edit("clock", frames, "10"));
    REQUIRE(model.save("clock"));
    CHECK(packs.fileText("rules/small/clock.yaml")
              .contains("# clock.c: tickGameClock: un minuto cada 20 fotogramas.\n"
                        "frames_per_minute: 10\n"));
}

TEST_CASE("the game's save section shows its autosave triggers as flags, edited and saved like any other") {
    DiskPacks packs;
    TuningModel model{packs.target()};
    const TuningNode& root = sectionNamed(model, "save").root;
    for (const std::string_view trigger :
         {"autosave.on_map_change", "autosave.on_battle_end", "autosave.on_partner_sleep", "autosave.on_new_day",
          "autosave.on_evolution", "autosave.on_tournament_result", "autosave.on_recruit_join", "autosave.on_key_item",
          "autosave.on_dangerous_area", "autosave.on_story_progress", "autosave.on_partner_reborn"}) {
        CAPTURE(trigger);
        CHECK(nodeAt(root, trigger).kind == TuningValueKind::Flag);
    }
    CHECK(nodeAt(root, "autosave.coalesce_seconds").kind == TuningValueKind::Number);
    const std::uint64_t revision = model.revision();
    REQUIRE(model.edit("save", nodeAt(root, "autosave.on_new_day"), std::string{FlagOffText}));
    CHECK(model.revision() == revision + 1);
    CHECK(nodeAt(sectionNamed(model, "save").root, "autosave.on_new_day").text == FlagOffText);
    REQUIRE_MESSAGE(model.save("save"), model.error());

    std::string expected = gamePackText(GameSaveSection);
    const std::string_view before = "on_new_day: true";
    const std::size_t at = expected.find(before);
    REQUIRE(at != std::string::npos);
    expected.replace(at, before.size(), "on_new_day: false");
    CHECK(packs.fileText("rules/small/save.yaml") == expected);
}

TEST_CASE("the game time reads as year, day and time of day") {
    CHECK(describeTime({.year = 1, .day = 3, .hour = 8, .minute = 5}) == "year 1, day 3, 08:05");
}

} // namespace dwe::debug
