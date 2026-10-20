#include <dwe/assets/yaml_reader.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_store.hpp>
#include <dwe/ruleset/strategy.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "ruleset_fixture.hpp"

namespace dwe::ruleset {

namespace {

using testing::errorOf;
using testing::FixturePacks;

// A strategy of the tests: how the care system of the fixture would pick something.
struct Selection {
    Selection() = default;
    Selection(const Selection&) = delete;
    Selection& operator=(const Selection&) = delete;
    Selection(Selection&&) = delete;
    Selection& operator=(Selection&&) = delete;
    virtual ~Selection() = default;
    [[nodiscard]] virtual int pick() const = 0;
};

struct OriginalSelection final : Selection {
    [[nodiscard]] int pick() const override { return 1; }
};

struct RandomSelection final : Selection {
    [[nodiscard]] int pick() const override { return 2; }
};

StrategyRegistry<Selection> selections() {
    StrategyRegistry<Selection> registry{"care selection"};
    registry.add("original", [] { return std::make_unique<OriginalSelection>(); });
    registry.add("random", [] { return std::make_unique<RandomSelection>(); });
    return registry;
}

// The care section of the fixture as its system would read it.
struct CareRules {
    int windowMinutes = 0;
};

core::Result<CareRules> readCare(const Section& section) {
    assets::YamlReader reader;
    if (!reader.expectKeys(section.node, "the care section", {"hunger", "selection"})) {
        return std::unexpected{reader.error(section.file)};
    }
    const core::YamlNode& hunger = reader.field(section.node, "hunger");
    CareRules care;
    if (reader.expectKeys(hunger, "hunger", {"window_minutes"})) {
        const core::YamlNode& window = reader.field(hunger, "window_minutes");
        care.windowMinutes = reader.integer<int>(window);
        if (reader.ok() && care.windowMinutes <= 0) {
            reader.fail(window, "window_minutes must be more than zero");
        }
    }
    if (!reader.ok()) {
        return std::unexpected{reader.error(section.file)};
    }
    if (auto known = selections().check(section, "selection"); !known) {
        return std::unexpected{known.error()};
    }
    return care;
}

SectionChecks careChecks() {
    return {{"care", [](const Section& section) -> core::Result<void> {
                 auto care = readCare(section);
                 if (!care) {
                     return std::unexpected{care.error()};
                 }
                 return {};
             }}};
}

int windowOf(const Ruleset& ruleset) {
    const Section* care = ruleset.section("care");
    REQUIRE(care != nullptr);
    const auto read = readCare(*care);
    REQUIRE_MESSAGE(read.has_value(), errorOf(read));
    return read.value_or(CareRules{}).windowMinutes;
}

} // namespace

TEST_CASE("a ruleset hands out its sections, tables and description") {
    FixturePacks packs;
    const auto ruleset = loadRuleset(packs.mount(), "small", careChecks());
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    CHECK(ruleset->id() == "small");
    CHECK(ruleset->info().name == "Small");
    CHECK(ruleset->info().version == "1.2.3");
    CHECK(ruleset->info().description == "A ruleset for the tests.");
    CHECK(ruleset->info().chain == std::vector<std::string>{"small"});
    REQUIRE(ruleset->table("raise") != nullptr);
    CHECK(ruleset->table("raise")->str() == "data/raise");
    CHECK(ruleset->table("moves") == nullptr);
    CHECK(ruleset->sections().size() == 2);
    CHECK(windowOf(*ruleset) == 90);
    CHECK(ruleset->section("care")->owner == "small");
    CHECK(ruleset->section("care")->file.ends_with("rules/small/care.yaml"));
    CHECK(ruleset->section("battle") == nullptr);
    CHECK(errorOf(ruleset->requireSection("battle")).contains("has no section 'battle'"));
    const auto clock = readClockRules(*ruleset);
    REQUIRE_MESSAGE(clock.has_value(), errorOf(clock));
    CHECK(clock->framesPerMinute == 20);
    CHECK(clock->start == GameTime{.year = 0, .day = 0, .hour = 8, .minute = 0});
}

TEST_CASE("a ruleset that extends another replaces only the sections and tables it has") {
    FixturePacks packs;
    packs.files.addFile("rules/rules/hard/ruleset.yaml", R"(id: hard
name: Hard
version: 0.1.0
extends: small
tables:
  raise: data/hard_raise
sections: [care]
)");
    packs.files.addFile("rules/rules/hard/care.yaml", "hunger:\n  window_minutes: 30\nselection: random\n");
    packs.files.addFile("rules/data/hard_raise.yaml", "raise: []\n");
    const auto ruleset = loadRuleset(packs.mount(), "hard", careChecks());
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    CHECK(ruleset->info().chain == std::vector<std::string>{"hard", "small"});
    CHECK(ruleset->info().name == "Hard");
    CHECK(windowOf(*ruleset) == 30);
    CHECK(ruleset->section("care")->owner == "hard");
    // The clock and the digimon table come from the ruleset it extends.
    REQUIRE(ruleset->section("clock") != nullptr);
    CHECK(ruleset->section("clock")->owner == "small");
    CHECK(ruleset->table("digimon")->str() == "data/digimon");
    CHECK(ruleset->table("raise")->str() == "data/hard_raise");
    const auto selection = selections().create(*ruleset->section("care"), "selection");
    REQUIRE_MESSAGE(selection.has_value(), errorOf(selection));
    CHECK((*selection)->pick() == 2);
}

TEST_CASE("an unknown key or a bad value of a section is refused with its file, line and column") {
    FixturePacks packs;
    SUBCASE("a key the clock does not have") {
        packs.files.addFile("rules/rules/small/clock.yaml", std::string{testing::Clock} + "speed: 2\n");
        const auto ruleset = loadRuleset(packs.mount(), "small");
        CHECK(errorOf(ruleset).contains("rules/small/clock.yaml: line 10, column 8: unknown key 'speed'"));
    }
    SUBCASE("a value out of range") {
        packs.files.addFile("rules/rules/small/clock.yaml",
                            "frames_per_second: 20\nframes_per_minute: 0\ndays_per_year: 30\nyear_limit: 100\n"
                            "start: {year: 0, day: 0, hour: 8, minute: 0}\n");
        const auto ruleset = loadRuleset(packs.mount(), "small");
        CHECK(errorOf(ruleset).contains(
            "rules/small/clock.yaml: line 2, column 20: frames_per_minute must be more than zero"));
    }
    SUBCASE("a key of a section that its system's check refuses") {
        packs.files.addFile("rules/rules/small/care.yaml", "hunger:\n  window_minutes: -5\nselection: original\n");
        const auto ruleset = loadRuleset(packs.mount(), "small", careChecks());
        CHECK(errorOf(ruleset).contains(
            "rules/small/care.yaml: line 2, column 19: window_minutes must be more than zero"));
        // Without the system's check the section is handed out as it is.
        CHECK(loadRuleset(packs.mount(), "small").has_value());
    }
}

TEST_CASE("a broken manifest is refused with its file and line") {
    FixturePacks packs;
    const auto manifest = [&](std::string_view text) {
        packs.files.addFile("rules/rules/small/ruleset.yaml", text);
        return errorOf(loadRuleset(packs.mount(), "small"));
    };
    CHECK(manifest("id: big\nname: S\nversion: 1.0.0\nsections: []\n")
              .contains("line 1, column 5: the id must be 'small'"));
    CHECK(manifest("id: small\nname: S\nversion: one\nsections: []\n").contains("line 3, column 10"));
    CHECK(manifest("id: small\nname: S\nversion: 1.0.0\nsections: []\nrules: 3\n").contains("unknown key 'rules'"));
    CHECK(manifest("id: small\nname: S\nversion: 1.0.0\nsections: []\ntables:\n  moves: data/moves\n")
              .contains("line 6, column 10: table moves: data/moves is in no pack"));
    CHECK(manifest("id: small\nname: S\nversion: 1.0.0\nsections: [clock, battle]\n")
              .contains("section rules/small/battle is in no pack"));
    CHECK(manifest("id: small\nname: S\nversion: 1.0.0\nsections: [clock, clock]\n").contains("listed twice"));
    CHECK(manifest("id: small\nname: S\nversion: 1.0.0\nsections: []\nscripts: [ai.lua]\n")
              .contains("script 'ai.lua' is in no pack"));
    CHECK(manifest("id: small\nname: S\nversion: 1.0.0\nsections: []\nextends: small\n").contains("extends itself"));
    CHECK(errorOf(loadRuleset(packs.mount(), "missing")).contains("ruleset rules/missing/ruleset is in no pack"));
    CHECK(errorOf(loadRuleset(packs.mount(), "Bad/Id")).contains("ruleset id 'Bad/Id'"));
}

TEST_CASE("a ruleset names its Lua scripts") {
    FixturePacks packs;
    packs.files.addFile("rules/rules/small/ruleset.yaml",
                        "id: small\nname: S\nversion: 1.0.0\nsections: []\nscripts: [ai.lua]\n");
    packs.files.addFile("rules/rules/small/ai.lua", "return {}\n");
    const auto ruleset = loadRuleset(packs.mount(), "small");
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    REQUIRE(ruleset->scripts().size() == 1);
    CHECK(ruleset->scripts().front().str() == "rules/small/ai");
}

TEST_CASE("a strategy is built by the name its section gives, and an unknown name is an error at its line") {
    FixturePacks packs;
    const auto ruleset = loadRuleset(packs.mount(), "small", careChecks());
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    const auto selection = selections().create(*ruleset->section("care"), "selection");
    REQUIRE_MESSAGE(selection.has_value(), errorOf(selection));
    CHECK((*selection)->pick() == 1);
    CHECK(selections().names() == "original, random");
    CHECK(errorOf(selections().create(*ruleset->section("care"), "hunger"))
              .contains("line 2, column 3: hunger must name a care selection"));
    CHECK(errorOf(selections().create(*ruleset->section("care"), "missing")).contains("missing is missing"));

    packs.files.addFile("rules/rules/small/care.yaml", "hunger:\n  window_minutes: 90\nselection: newest\n");
    CHECK(errorOf(loadRuleset(packs.mount(), "small", careChecks()))
              .contains("rules/small/care.yaml: line 3, column 12: unknown care selection 'newest'; known: "
                        "original, random"));
}

TEST_CASE("a value is found and set by its dotted path") {
    FixturePacks packs;
    auto ruleset = loadRuleset(packs.mount(), "small");
    REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
    const Section& clock = *ruleset->section("clock");
    REQUIRE(findValue(clock.node, "start.hour") != nullptr);
    CHECK(findValue(clock.node, "start.hour")->text() == "8");
    CHECK(findValue(clock.node, "start.second") == nullptr);
    REQUIRE(ruleset->setValue("clock", "start.hour", "6").has_value());
    CHECK(findValue(ruleset->section("clock")->node, "start.hour")->text() == "6");
    CHECK(errorOf(ruleset->setValue("clock", "start.second", "6"))
              .contains("clock.start.second: there is no "
                        "'second'"));
    CHECK(errorOf(ruleset->setValue("battle", "speed", "6")).contains("has no section 'battle'"));

    core::YamlNode list = core::YamlNode::mapping();
    core::YamlNode items = core::YamlNode::sequence();
    items.append(core::YamlNode::scalar("a"));
    items.append(core::YamlNode::scalar("b"));
    list.set("items", std::move(items));
    CHECK(findValue(list, "items.1")->text() == "b");
    CHECK(findValue(list, "items.2") == nullptr);
}

TEST_CASE("the store tunes a value, checks it and bumps the revision") {
    FixturePacks packs;
    auto store = RulesetStore::load(packs.mount(), "small", careChecks());
    REQUIRE_MESSAGE(store.has_value(), errorOf(store));
    CHECK(store->revision() == 1);
    REQUIRE(store->setValue("care", "hunger.window_minutes", "45").has_value());
    CHECK(store->revision() == 2);
    CHECK(windowOf(store->current()) == 45);

    // A value the care check refuses changes nothing.
    CHECK(errorOf(store->setValue("care", "hunger.window_minutes", "0")).contains("care.hunger.window_minutes = 0: "));
    CHECK(errorOf(store->setValue("care", "selection", "newest")).contains("unknown care selection 'newest'"));
    CHECK(errorOf(store->setValue("clock", "frames_per_minute", "[1]")).contains("frames_per_minute"));
    CHECK(store->revision() == 2);
    CHECK(windowOf(store->current()) == 45);

    REQUIRE(store->setValue("care", "selection", "random").has_value());
    CHECK((*selections().create(*store->current().section("care"), "selection"))->pick() == 2);
    CHECK(store->revision() == 3);
}

TEST_CASE("reloading reads the edited files, and a broken edit keeps the ruleset that was there") {
    FixturePacks packs;
    auto store = RulesetStore::load(packs.mount(), "small", careChecks());
    REQUIRE_MESSAGE(store.has_value(), errorOf(store));
    REQUIRE(store->setValue("care", "hunger.window_minutes", "45").has_value());

    packs.files.addFile("rules/rules/small/care.yaml", "hunger:\n  window_minutes: 60\nselection: original\n");
    REQUIRE(store->reload().has_value());
    CHECK(store->revision() == 3);
    // The files are the truth: the tuned value is gone.
    CHECK(windowOf(store->current()) == 60);

    packs.files.addFile("rules/rules/small/care.yaml", "hunger:\n  window_minutes: 60\nselection: newest\n");
    CHECK(errorOf(store->reload()).contains("unknown care selection 'newest'"));
    packs.files.addFile("rules/rules/small/care.yaml", "hunger: [\n");
    CHECK(errorOf(store->reload()).contains("rules/small/care.yaml"));
    CHECK(store->revision() == 3);
    CHECK(windowOf(store->current()) == 60);

    CHECK(errorOf(store->select("missing")).contains("is in no pack"));
    CHECK(store->current().id() == "small");
}

TEST_CASE("the store switches to another ruleset") {
    FixturePacks packs;
    packs.files.addFile("rules/rules/fast/ruleset.yaml", "id: fast\nname: Fast\nversion: 0.1.0\nextends: small\n"
                                                         "sections: [clock]\n");
    packs.files.addFile("rules/rules/fast/clock.yaml",
                        "frames_per_second: 20\nframes_per_minute: 10\ndays_per_year: 30\nyear_limit: 100\n"
                        "start: {year: 0, day: 0, hour: 8, minute: 0}\n");
    auto store = RulesetStore::load(packs.mount(), "small", careChecks());
    REQUIRE_MESSAGE(store.has_value(), errorOf(store));
    REQUIRE(store->select("fast").has_value());
    CHECK(store->current().id() == "fast");
    CHECK(store->revision() == 2);
    CHECK(readClockRules(store->current())->framesPerMinute == 10);
    CHECK(windowOf(store->current()) == 90);
}

TEST_CASE("the store adopts a loaded ruleset that passes its checks") {
    FixturePacks packs;
    const assets::VirtualFileSystem& files = packs.mount();
    auto loaded = loadRuleset(files, "small", careChecks());
    REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
    REQUIRE(loaded->setValue("care", "hunger.window_minutes", "45").has_value());
    auto store = RulesetStore::adopt(files, *loaded, careChecks());
    REQUIRE_MESSAGE(store.has_value(), errorOf(store));
    CHECK(store->revision() == 1);
    CHECK(windowOf(store->current()) == 45);

    REQUIRE(loaded->setValue("care", "hunger.window_minutes", "0").has_value());
    CHECK(errorOf(RulesetStore::adopt(files, *loaded, careChecks())).contains("window_minutes"));
}

TEST_CASE("quirks are read by name, each true or false") {
    const auto read = [](std::string_view text) {
        const auto node = core::parseYaml(text);
        REQUIRE(node.has_value());
        assets::YamlReader reader;
        const Quirks quirks = readQuirks(reader, *node, {"slow_toilet", "fixed_sleep"});
        return std::pair{reader.ok() ? std::string{} : reader.error("quirks").message, quirks};
    };
    const auto [error, quirks] = read("slow_toilet: true\nfixed_sleep: false\n");
    CHECK(error.empty());
    CHECK(quirks.on("slow_toilet"));
    CHECK_FALSE(quirks.on("fixed_sleep"));
    CHECK_FALSE(quirks.on("other"));
    CHECK(read("slow_toilet: true\n").first.contains("fixed_sleep"));
    CHECK(read("slow_toilet: true\nfixed_sleep: false\nother: true\n").first.contains("unknown key 'other'"));
    CHECK(read("slow_toilet: yes\nfixed_sleep: false\n").first.contains("line 1, column 14"));
}

} // namespace dwe::ruleset
