#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/simulation.hpp>
#include <dwe/game/evolution/state.hpp>
#include <dwe/ruleset/ruleset_store.hpp>

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include "fixture.hpp"

namespace dwe::game::evolution {

namespace {

using testing::errorOf;
using testing::inputsOf;
using testing::TableFixture;
using testing::valueOf;

// The tables the original ruleset names (rules/original/ruleset.yaml).
constexpr std::array<std::string_view, 8> OriginalTables{"digimon", "evolution", "raise",        "training",
                                                         "moves",   "items",     "item_effects", "shops"};

// A selection of the tests: always the last target of the chart, at any hour.
class LastTargetSelection final : public EvolutionSelection {
public:
    [[nodiscard]] std::optional<Species> select(const EvolutionQuery& query,
                                                const RequirementScoring& /*scoring*/) const override {
        const auto targets = query.tables->pathTargets(query.inputs->species);
        if (targets.empty()) {
            return std::nullopt;
        }
        return targets.back();
    }
};

// Random inputs of an in-training form, for the simulation.
EvolutionInputs randomInTraining(ruleset::Random& random) {
    constexpr std::int32_t MaxStat = 200;
    EvolutionInputs inputs = inputsOf("koro", 24);
    inputs.stats = {.hp = random.below(MaxStat * 10),
                    .mp = random.below(MaxStat * 10),
                    .offense = random.below(MaxStat),
                    .defense = random.below(MaxStat),
                    .speed = random.below(MaxStat),
                    .brains = random.below(MaxStat)};
    // The weight passes about half the time.
    constexpr std::int32_t MaxWeight = 10;
    inputs.weight = random.below(MaxWeight);
    return inputs;
}

} // namespace

// --- The evolution state the save keeps ---

TEST_CASE("each evolution is recorded as raised, with the item flag") {
    EvolutionState state;
    EvolutionResult virus{.target = TableFixture::index("scum")};
    virus.virusBackup = VirusBackup{.species = TableFixture::index("grey"), .stats = {.hp = 10}};
    recordEvolution(state, virus);
    CHECK(state.raised == std::set<Species>{TableFixture::index("scum")});
    CHECK_FALSE(state.evolutionItemUsed);
    recordEvolution(state, {.target = TableFixture::index("nume"), .evolutionItemUsed = true});
    CHECK(state.raised.size() == 2);
    CHECK(state.evolutionItemUsed);
}

TEST_CASE("the evolution state goes to YAML by form id and back") {
    const EvolutionTables tables = TableFixture{}.build();
    EvolutionState state;
    state.raised = {TableFixture::index("grey"), TableFixture::index("agum")};
    const auto node = toYaml(state, tables);
    REQUIRE_MESSAGE(node.has_value(), errorOf(node));
    const auto text = core::emitYaml(*node);
    REQUIRE_MESSAGE(text.has_value(), errorOf(text));
    CHECK(text->contains("grey"));
    const auto parsed = core::parseYaml(*text);
    REQUIRE_MESSAGE(parsed.has_value(), errorOf(parsed));
    const auto back = fromYaml(*parsed, tables);
    REQUIRE_MESSAGE(back.has_value(), errorOf(back));
    CHECK(*back == state);
}

TEST_CASE("a state with an unknown form or key is refused") {
    const EvolutionTables tables = TableFixture{}.build();
    const auto extra = core::parseYaml("raised: []\nversion: 1\n");
    REQUIRE(extra.has_value());
    CHECK_FALSE(fromYaml(*extra, tables).has_value());
    const auto unknown = core::parseYaml("raised: [nobody]\n");
    REQUIRE(unknown.has_value());
    CHECK(errorOf(fromYaml(*unknown, tables)).contains("unknown form 'nobody'"));
    // The partner keeps the virus backup now: the evolution state has no place for it.
    const auto backup = core::parseYaml("raised: []\nvirus_backup: {form: grey}\n");
    REQUIRE(backup.has_value());
    CHECK_FALSE(fromYaml(*backup, tables).has_value());
}

// --- Strategies and tuned values (R3.1) ---

TEST_CASE("another selection strategy changes the target without touching the rules") {
    TableFixture fixture;
    fixture.setPath("koro", {"agum", "gabu"});
    const auto lastTarget =
        Evolution::create(testing::originalRules(), fixture.build(), std::make_unique<LastTargetSelection>(),
                          std::make_unique<OriginalScoring>());
    REQUIRE_MESSAGE(lastTarget.has_value(), errorOf(lastTarget));
    ruleset::Random random{1};
    const NaturalCheck check = lastTarget->checkNatural(inputsOf("koro", 24), {}, 1, random);
    REQUIRE(check.evolution);
    CHECK(valueOf(check.evolution).target == TableFixture::index("gabu"));
    // The original finds no candidate with these inputs.
    CHECK_FALSE(fixture.makeEvolution().checkNatural(inputsOf("koro", 24), {}, 1, random).evolution);
}

TEST_CASE("a value tuned in the ruleset store changes the evolution, and a bad one is refused") {
    const assets::DiskFileSystem disk;
    assets::MemoryFileSystem memory;
    memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
    for (const std::string_view role : OriginalTables) {
        memory.addFile(std::format("base/data/{}.yaml", role), std::format("{}: []\n", role));
    }
    const std::array<assets::PackLocation, 2> packs{assets::PackLocation{.fileSystem = &memory, .root = "base"},
                                                    assets::PackLocation{.fileSystem = &disk, .root = DWE_GAME_PACK}};
    const auto vfs = assets::VirtualFileSystem::mount(packs);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    auto store =
        ruleset::RulesetStore::load(*vfs, "original", {{std::string{EvolutionSection}, checkEvolutionSection}});
    REQUIRE_MESSAGE(store.has_value(), errorOf(store));

    const auto rulesNow = [&] {
        const ruleset::Section* section = store->current().section(EvolutionSection);
        REQUIRE(section != nullptr);
        auto rules = readEvolutionRules(*section);
        REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
        return *rules;
    };
    const Species bota = TableFixture::index("bota");
    ruleset::Random random{1};
    CHECK(TableFixture{}.makeEvolution(rulesNow()).checkNatural(inputsOf("bota", 4), {}, 1, random).evolution ==
          std::nullopt);

    const auto tuned = store->setValue(EvolutionSection, "check_hours.baby", "4");
    REQUIRE_MESSAGE(tuned.has_value(), errorOf(tuned));
    CHECK(store->revision() == 2);
    const auto check = TableFixture{}.makeEvolution(rulesNow()).checkNatural(inputsOf("bota", 4), {}, 1, random);
    REQUIRE(check.evolution);
    CHECK(valueOf(check.evolution).target != bota);

    CHECK(errorOf(store->setValue(EvolutionSection, "stat_gains.gain_divisor", "0")).contains("must be 1 or more"));
    CHECK(errorOf(store->setValue(EvolutionSection, "scoring", "lenient")).contains("unknown evolution scoring"));
    CHECK(store->revision() == 2);
}

// --- The evolution items' table from the packs ---

TEST_CASE("the item effects are read by the ruleset's table id") {
    const assets::DiskFileSystem disk;
    const auto loadWith = [&](std::string_view itemEffects) {
        assets::MemoryFileSystem memory;
        memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
        for (const std::string_view role : OriginalTables) {
            if (role == EvolutionTableRole) {
                memory.addFile("base/data/evolution.yaml", "requirements: []\nstats_gains: []\npaths: []\n");
            } else if (role != ItemEffectsTableRole) {
                memory.addFile(std::format("base/data/{}.yaml", role), std::format("{}: []\n", role));
            }
        }
        memory.addFile("base/data/item_effects.yaml", std::string{itemEffects});
        const std::array<assets::PackLocation, 2> packs{
            assets::PackLocation{.fileSystem = &memory, .root = "base"},
            assets::PackLocation{.fileSystem = &disk, .root = DWE_GAME_PACK}};
        const auto vfs = assets::VirtualFileSystem::mount(packs);
        REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
        const auto store = ruleset::RulesetStore::load(*vfs, "original", {});
        REQUIRE_MESSAGE(store.has_value(), errorOf(store));
        return EvolutionTables::load(*vfs, store->current());
    };
    const auto empty = loadWith("item_effects: []\n");
    CHECK_MESSAGE(empty.has_value(), errorOf(empty));
    const auto unknown = loadWith("item_effects:\n  - {item: grey_claws, index: 71, handler: evolution, healing: null, "
                                  "evolves_to: nobody, values: null}\n");
    CHECK(errorOf(unknown).contains("item_effects: unknown digimon 'nobody'"));
    CHECK_FALSE(loadWith("item_effects: 3\n").has_value());
}

// --- The simulation for tuning ---

TEST_CASE("simulated raisings fill a histogram, the same for the same seed") {
    TableFixture fixture;
    fixture.setPath("koro", {"agum", "gabu"});
    fixture.requirement("agum").offense = FavouredStat;
    fixture.requirement("gabu").speed = FavouredStat;
    const Evolution evolution = fixture.makeEvolution();
    constexpr std::int32_t Raisings = 1000;
    constexpr std::uint64_t SimulationSeed = 99;
    const EvolutionHistogram first = simulateRaisings(evolution, randomInTraining, Raisings, SimulationSeed);
    const EvolutionHistogram again = simulateRaisings(evolution, randomInTraining, Raisings, SimulationSeed);
    CHECK(first.raisings == Raisings);
    std::int32_t evolved = 0;
    for (const auto& [target, count] : first.targets) {
        evolved += count;
    }
    CHECK(first.unchanged + evolved == Raisings);
    CHECK(first.targets.contains(TableFixture::index("agum")));
    CHECK(first.targets.contains(TableFixture::index("gabu")));
    CHECK(first.unchanged > 0);
    CHECK(again.targets == first.targets);
    CHECK(again.unchanged == first.unchanged);
}

} // namespace dwe::game::evolution
