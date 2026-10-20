#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/rules.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::partner::testing {

template <typename T>
std::string errorOf(const core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// The tables the original ruleset names, by role (rules/original/ruleset.yaml).
inline constexpr std::array<std::string_view, 8> OriginalTables{"digimon", "evolution", "raise",        "training",
                                                                "moves",   "items",     "item_effects", "shops"};

// The game pack's original ruleset, mounted over a stand-in base pack whose tables are empty, so the tests need no
// disc. Not movable: the file systems must stay where the mounted packs point.
class OriginalRuleset {
public:
    explicit OriginalRuleset(std::shared_ptr<const PartnerStrategyRegistries> registries =
                                 std::make_shared<const PartnerStrategyRegistries>())
        : registries_(std::move(registries)) {
        memory_.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
        for (const std::string_view role : OriginalTables) {
            memory_.addFile(std::format("base/data/{}.yaml", role), std::format("{}: []\n", role));
        }
        const std::array<assets::PackLocation, 2> packs{
            assets::PackLocation{.fileSystem = &memory_, .root = "base"},
            assets::PackLocation{.fileSystem = &disk_, .root = DWE_GAME_PACK}};
        auto vfs = assets::VirtualFileSystem::mount(packs);
        REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
        vfs_ = std::make_unique<assets::VirtualFileSystem>(std::move(*vfs));
        auto ruleset = ruleset::loadRuleset(*vfs_, ruleset::OriginalRuleset, partnerSectionChecks(registries_));
        REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
        ruleset_ = std::make_unique<ruleset::Ruleset>(std::move(*ruleset));
    }
    OriginalRuleset(const OriginalRuleset&) = delete;
    OriginalRuleset& operator=(const OriginalRuleset&) = delete;
    OriginalRuleset(OriginalRuleset&&) = delete;
    OriginalRuleset& operator=(OriginalRuleset&&) = delete;
    ~OriginalRuleset() = default;

    [[nodiscard]] ruleset::Ruleset& ruleset() { return *ruleset_; }

    // Sets a value as a tuning tool would; the test fails if the ruleset refuses it.
    void set(std::string_view section, std::string_view path, std::string_view yaml) {
        auto changed = ruleset_->setValue(section, path, yaml);
        REQUIRE_MESSAGE(changed.has_value(), errorOf(changed));
    }

    [[nodiscard]] PartnerRules rules() const {
        auto rules = loadPartnerRules(*ruleset_, *registries_);
        REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
        return std::move(*rules);
    }

    [[nodiscard]] ruleset::ClockRules clock() const {
        auto clock = ruleset::readClockRules(*ruleset_);
        REQUIRE_MESSAGE(clock.has_value(), errorOf(clock));
        return *clock;
    }

private:
    std::shared_ptr<const PartnerStrategyRegistries> registries_;
    assets::MemoryFileSystem memory_;
    assets::DiskFileSystem disk_;
    std::unique_ptr<assets::VirtualFileSystem> vfs_;
    std::unique_ptr<ruleset::Ruleset> ruleset_;
};

// Made-up species for the tests, in the shape of the raise table: not the original's data.
inline SpeciesCare babySpecies(std::string id) {
    return {.id = std::move(id),
            .level = Level::Baby,
            .hungerHours = {-1, -1, -1, -1, -1, -1, -1, -1},
            .energyCap = 20,
            .energyThreshold = 5,
            .energyUsage = 1,
            .poopInterval = 6,
            .poopSize = 4,
            .favoriteFood = "milk",
            .sleepPattern = 6,
            .favoredRegion = 1,
            .trainingAptitude = 0,
            .defaultWeight = 5};
}

inline SpeciesCare rookieSpecies(std::string id) {
    return {.id = std::move(id),
            .level = Level::Rookie,
            .hungerHours = {8, 13, 19, -1, -1, -1, -1, -1},
            .energyCap = 60,
            .energyThreshold = 15,
            .energyUsage = 3,
            .poopInterval = 18,
            .poopSize = 8,
            .favoriteFood = "meat",
            .sleepPattern = 2,
            .favoredRegion = 2,
            .trainingAptitude = 1,
            .defaultWeight = 20};
}

inline SpeciesTable testSpecies() {
    SpeciesTable table;
    for (const std::string_view baby : {"bota", "puni", "poyo", "yura"}) {
        table.add(babySpecies(std::string{baby}));
    }
    SpeciesCare inTraining = babySpecies("koro");
    inTraining.level = Level::InTraining;
    inTraining.sleepPattern = 7;
    table.add(inTraining);
    table.add(rookieSpecies("agu"));
    SpeciesCare champion = rookieSpecies("greymon");
    champion.level = Level::Champion;
    table.add(champion);
    SpeciesCare scum = rookieSpecies("scum");
    scum.level = Level::Champion;
    table.add(scum);
    SpeciesCare nume = rookieSpecies("nume");
    nume.level = Level::Champion;
    table.add(nume);
    return table;
}

inline PartnerStats testStats() {
    PartnerStats stats;
    stats.base.values = {1000, 1000, 100, 100, 100, 100};
    stats.currentHp = 1000;
    stats.currentMp = 1000;
    return stats;
}

// A partner raised on a clock, with the rules and species it points to. Not movable.
class Raising {
public:
    explicit Raising(PartnerRules rules, std::string_view species = "agu", std::uint64_t seed = 1)
        : rules_(std::move(rules)), species_(testSpecies()), clock_(ruleset::ClockRules{}),
          partner_(rules_, species_, seed) {
        auto started = partner_.start(species, testStats(), clock_);
        REQUIRE_MESSAGE(started.has_value(), errorOf(started));
    }
    Raising(const Raising&) = delete;
    Raising& operator=(const Raising&) = delete;
    Raising(Raising&&) = delete;
    Raising& operator=(Raising&&) = delete;
    ~Raising() = default;

    [[nodiscard]] Partner& partner() { return partner_; }
    [[nodiscard]] const PartnerState& state() const { return partner_.state(); }
    [[nodiscard]] ruleset::GameClock& clock() { return clock_; }
    [[nodiscard]] PartnerRules& rules() { return rules_; }

    // Plays this many frames, then the events they gave.
    std::vector<PartnerEvent> playFrames(std::int64_t frames, const CareContext& context = {}) {
        const ruleset::ClockAdvance advance = clock_.advanceFrames(frames);
        partner_.tick(clock_, advance, context);
        return partner_.takeEvents();
    }
    std::vector<PartnerEvent> playMinutes(std::int64_t minutes, const CareContext& context = {}) {
        return playFrames(minutes * clock_.rules().framesPerMinute, context);
    }
    std::vector<PartnerEvent> playHours(std::int64_t hours, const CareContext& context = {}) {
        return playFrames(hours * clock_.rules().framesPerMinute * ruleset::MinutesPerHour, context);
    }
    // Plays until the clock shows this time (the next day if it is past).
    std::vector<PartnerEvent> playUntil(std::int32_t hour, std::int32_t minute, const CareContext& context = {}) {
        const ruleset::GameTime now = clock_.time();
        std::int64_t target = (hour * ruleset::MinutesPerHour) + minute;
        const std::int64_t current = (now.hour * ruleset::MinutesPerHour) + now.minute;
        if (target <= current) {
            target += ruleset::HoursPerDay * ruleset::MinutesPerHour;
        }
        const std::int64_t frameOfMinute = clock_.frameOfDay() % clock_.rules().framesPerMinute;
        return playFrames(((target - current) * clock_.rules().framesPerMinute) - frameOfMinute, context);
    }

    // Moves the clock to this time (the next day if it is past) without playing the frames between.
    void skipTo(std::int32_t hour, std::int32_t minute) {
        const ruleset::GameTime now = clock_.time();
        std::int64_t target = (hour * ruleset::MinutesPerHour) + minute;
        const std::int64_t current = (now.hour * ruleset::MinutesPerHour) + now.minute;
        if (target <= current) {
            target += ruleset::HoursPerDay * ruleset::MinutesPerHour;
        }
        clock_.advanceFrames((target - current) * clock_.rules().framesPerMinute);
    }

    // Overwrites the state, for a test that starts from a given need.
    void edit(const std::function<void(PartnerState&)>& change) {
        PartnerState state = partner_.state();
        change(state);
        auto restored = partner_.restore(std::move(state));
        REQUIRE_MESSAGE(restored.has_value(), errorOf(restored));
    }

    // Puts off the meals and the toilet, so a test sees only the rule it looks at.
    void quiet() {
        edit([](PartnerState& state) {
            state.hunger.foodLevel = QuietMinutes;
            state.toilet.poopLevel = QuietMinutes;
        });
    }

private:
    static constexpr std::int32_t QuietMinutes = 1'000'000;

    PartnerRules rules_;
    SpeciesTable species_;
    ruleset::GameClock clock_;
    Partner partner_;
};

inline bool has(const std::vector<PartnerEvent>& events, EventKind kind) {
    return std::ranges::any_of(events, [&](const PartnerEvent& event) { return event.kind == kind; });
}

inline std::int64_t count(const std::vector<PartnerEvent>& events, EventKind kind) {
    return std::ranges::count_if(events, [&](const PartnerEvent& event) { return event.kind == kind; });
}

// The value of the first event of this kind; the test fails if there is none.
inline std::int32_t valueOf(const std::vector<PartnerEvent>& events, EventKind kind) {
    const auto found = std::ranges::find_if(events, [&](const PartnerEvent& event) { return event.kind == kind; });
    REQUIRE_MESSAGE(found != events.end(), "no event of kind ", static_cast<int>(kind));
    return found->value;
}

} // namespace dwe::game::partner::testing
