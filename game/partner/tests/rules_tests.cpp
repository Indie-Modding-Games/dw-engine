#include <dwe/assets/file_system.hpp>
#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/rules.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <format>
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

#include "partner_fixture.hpp"

namespace dwe::game::partner {

namespace {

using testing::errorOf;

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// A care mistake rule that never counts one, as a redesign could pick.
class NoCareMistakes final : public CareMistakeRule {
public:
    [[nodiscard]] bool counts(CareMistakeCause /*cause*/, const PartnerState& /*state*/,
                              const SpeciesCare& /*species*/) const override {
        return false;
    }
};

} // namespace

TEST_CASE("the original ruleset gives the partner the original's numbers") {
    testing::OriginalRuleset original;
    const PartnerRules rules = original.rules();
    // partner_impl.c: tickHungerMechanics, setFoodTimer.
    CHECK(rules.care.hunger.windowMinutes == 90);
    CHECK(rules.care.hunger.babyIntervalHours == 2);
    CHECK(rules.care.hunger.inTrainingIntervalHours == 3);
    // SLEEP_PATTERN: eight patterns, the third from 22:00 to 7:00.
    REQUIRE(rules.care.sleep.patterns.size() == 8);
    CHECK(rules.care.sleep.patterns[2].sleepyHour == 22);
    CHECK(rules.care.sleep.patterns[2].wakeHour == 7);
    CHECK(rules.care.toilet.virusBarLimit == 16);
    CHECK(rules.care.mood.boostedScold.discipline == 8);
    // partner.c: initializePartner; evolution.c: the rebirth babies.
    CHECK(rules.life.lifetimeHours == 360);
    CHECK(rules.life.lives == 3);
    CHECK(rules.life.rebirth.babies.size() == 4);
    CHECK(rules.training.levelTenths == std::array<std::int32_t, LevelCount>{9, 9, 10, 11, 12});
    CHECK(rules.training.findPlace("trn") != nullptr);
    CHECK(rules.training.findPlace("trn2") != nullptr);
    // Every quirk reproduces the original.
    CHECK(rules.care.quirks.sicknessChanceWraps);
    CHECK(rules.care.quirks.toiletDoublesPoopInterval);
    CHECK(rules.training.quirks.brainTrainingUsesMpAptitude);
}

TEST_CASE("the partner's sections follow their schemas") {
    const assets::DiskFileSystem disk;
    constexpr std::array<std::string_view, 3> Sections{"care", "life", "training"};
    for (const std::string_view section : Sections) {
        CAPTURE(section);
        const auto schemaBytes =
            disk.readFile(std::filesystem::path{DWE_SCHEMA_DIR} / std::format("ruleset_{}.schema.json", section));
        REQUIRE_MESSAGE(schemaBytes.has_value(), errorOf(schemaBytes));
        const auto schemaText = core::parseYaml(textOf(*schemaBytes));
        REQUIRE_MESSAGE(schemaText.has_value(), errorOf(schemaText));
        const auto schema = core::JsonSchema::compile(*schemaText, {});
        REQUIRE_MESSAGE(schema.has_value(), errorOf(schema));
        const auto bytes =
            disk.readFile(std::filesystem::path{DWE_GAME_PACK} / std::format("rules/original/{}.yaml", section));
        REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
        const auto document = core::parseYaml(textOf(*bytes));
        REQUIRE_MESSAGE(document.has_value(), errorOf(document));
        for (const core::SchemaProblem& problem : schema->check(*document).problems) {
            FAIL_CHECK(std::format("{}:{}: {}: {}", problem.line, problem.column, problem.path, problem.message));
        }
    }
}

TEST_CASE("the partner's section checks refuse a value the partner cannot use") {
    // A tuning tool sets the value on a copy and runs the checks before it replaces the ruleset.
    const auto refused = [](std::string_view section, std::string_view path, std::string_view yaml) {
        testing::OriginalRuleset original;
        original.set(section, path, yaml);
        return !ruleset::checkRuleset(original.ruleset(), partnerSectionChecks()).has_value();
    };
    // A zero period would divide by zero.
    CHECK(refused("care", "hunger.window_step_frames", "0"));
    CHECK(refused("care", "strategies.sickness_roll", "nonsense"));
    CHECK(refused("life", "rebirth.baby_choice", "nonsense"));
    CHECK(refused("training", "places.0.courses.0.gains.0.stat", "luck"));
    testing::OriginalRuleset original;
    CHECK(ruleset::checkRuleset(original.ruleset(), partnerSectionChecks()).has_value());
}

TEST_CASE("switching the care mistake strategy changes what counts as one") {
    auto registries = std::make_shared<PartnerStrategyRegistries>();
    registries->careMistakes.add("forgiving", [] { return std::make_unique<NoCareMistakes>(); });
    testing::OriginalRuleset original(registries);
    original.set("care", "strategies.care_mistakes", "forgiving");
    testing::Raising raising(original.rules());
    raising.quiet();
    // Hungry with no energy: the original counts a care mistake when the window closes.
    raising.edit([](PartnerState& state) {
        state.hunger.foodLevel = 0;
        state.hunger.energy = 0;
    });
    const auto events = raising.playMinutes(120);
    CHECK(testing::has(events, EventKind::HungerWindowClosed));
    CHECK_FALSE(testing::has(events, EventKind::CareMistake));
    CHECK(raising.state().evolution.careMistakes == 0);
}

TEST_CASE("the original strategy counts the same missed meal as a care mistake") {
    testing::OriginalRuleset original;
    testing::Raising raising(original.rules());
    raising.quiet();
    raising.edit([](PartnerState& state) {
        state.hunger.foodLevel = 0;
        state.hunger.energy = 0;
    });
    const auto events = raising.playMinutes(120);
    const std::int32_t mistake = testing::valueOf(events, EventKind::CareMistake);
    CHECK(mistake == static_cast<std::int32_t>(CareMistakeCause::HungerWindow));
    CHECK(raising.state().evolution.careMistakes == 1);
}

TEST_CASE("a value changed in the ruleset changes the partner: a shorter hunger window") {
    testing::OriginalRuleset original;
    original.set("care", "hunger.window_minutes", "30");
    testing::Raising raising(original.rules());
    raising.quiet();
    raising.edit([](PartnerState& state) { state.hunger.foodLevel = 0; });
    // The window opens on the first frame and closes 30 minutes later instead of 90.
    CHECK_FALSE(testing::has(raising.playMinutes(29), EventKind::HungerWindowClosed));
    CHECK(testing::has(raising.playMinutes(2), EventKind::HungerWindowClosed));
}

} // namespace dwe::game::partner
