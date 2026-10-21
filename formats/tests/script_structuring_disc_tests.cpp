#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/control_flow.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/structure_statistics.hpp>
#include <dwe/formats/script/structuring.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "structure_parity.hpp"

namespace script = dwe::formats::script;

namespace {

constexpr std::string_view MapheadListing = "maphead.yaml";
constexpr std::string_view ScriptFolder = "script";
constexpr std::string_view DgFolder = "dg";
// What the US disc gives (docs/reference/scripts.md, "Forma del control de flujo").
constexpr std::size_t ExpectedSections = 1553;
constexpr std::size_t ExpectedMultiJumpIfs = 0; // IFs with more than one jump entry
constexpr std::size_t ExpectedStateMachines = 4;
constexpr std::size_t ExpectedIrreducible = 3;
constexpr std::size_t ExpectedNestedExits = 1;
constexpr std::size_t ExpectedStructuredSections = ExpectedSections - ExpectedStateMachines;

// The script folder of an extracted content folder: the one that holds maphead.yaml.
std::optional<std::filesystem::path> findScriptFolder(const std::filesystem::path& content) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator{content}) {
        if (entry.path().filename() == MapheadListing && entry.path().parent_path().filename() == ScriptFolder) {
            return entry.path().parent_path();
        }
    }
    return std::nullopt;
}

// The listings by script id: 0 is MAPHEAD, the others DG.SCN's.
std::map<std::uint16_t, std::filesystem::path> findListings(const std::filesystem::path& folder) {
    std::map<std::uint16_t, std::filesystem::path> listings{{0, folder / MapheadListing}};
    for (const auto& entry : std::filesystem::directory_iterator{folder / DgFolder}) {
        const std::string stem = entry.path().stem().string();
        listings.emplace(static_cast<std::uint16_t>(std::stoi(stem)), entry.path());
    }
    return listings;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

std::set<std::size_t> reachableInstructions(const script::ControlFlowGraph& graph, script::BlockId entry) {
    std::set<script::BlockId> seen;
    std::vector<script::BlockId> pending{entry};
    std::set<std::size_t> instructions;
    while (!pending.empty()) {
        const script::BlockId block = pending.back();
        pending.pop_back();
        if (!seen.insert(block).second) {
            continue;
        }
        const script::BasicBlock& current = graph.blocks[block];
        for (std::size_t index = current.first; index < current.first + current.count; ++index) {
            instructions.insert(index);
        }
        std::ranges::copy(current.successors, std::back_inserter(pending));
    }
    return instructions;
}

// The sections that reach an instruction another section reaches too.
std::size_t sectionsSharingCode(const script::ScriptFlow& flow) {
    std::vector<std::set<std::size_t>> reached;
    std::map<std::size_t, std::size_t> owners;
    for (const script::Routine& routine : flow.routines) {
        if (routine.kind == script::RoutineKind::Section) {
            reached.push_back(reachableInstructions(flow.graph, routine.entry));
            for (const std::size_t instruction : reached.back()) {
                ++owners[instruction];
            }
        }
    }
    return static_cast<std::size_t>(std::ranges::count_if(reached, [&](const std::set<std::size_t>& instructions) {
        return std::ranges::any_of(instructions, [&](std::size_t instruction) { return owners[instruction] > 1; });
    }));
}

struct DiscReport {
    std::size_t scripts = 0;
    std::size_t multiJumpIfs = 0;
    std::size_t fileCalls = 0;
    std::size_t fileJumps = 0;
    std::size_t loadScripts = 0;
    std::size_t danglingReferences = 0;
    std::size_t sectionsSharingCode = 0;
    std::size_t structuredSections = 0;
    std::size_t parityFailures = 0;
    std::size_t gotoFree = 0; // scripts whose every transfer is structured
    std::vector<std::string> fallbacks;
    script::StructureStatistics statistics;
};

void countReferences(const script::ScriptListing& listing,
                     const std::map<std::uint16_t, std::set<std::uint16_t>>& sectionsByScript, DiscReport& report) {
    for (const script::ListingInstruction& instruction : listing.instructions) {
        report.multiJumpIfs += instruction.conditionalJumps.size() > 1 ? 1 : 0;
        report.fileCalls += instruction.role == script::FlowRole::FileCall ? 1 : 0;
        report.fileJumps += instruction.role == script::FlowRole::FileJump ? 1 : 0;
        report.loadScripts += instruction.role == script::FlowRole::LoadScript ? 1 : 0;
        if (instruction.reference) {
            const auto found = sectionsByScript.find(instruction.reference->script);
            const bool known =
                found != sectionsByScript.end() && found->second.contains(instruction.reference->section);
            report.danglingReferences += known ? 0 : 1;
        }
    }
}

std::string_view reasonName(script::FallbackReason reason) {
    switch (reason) {
    case script::FallbackReason::Irreducible:
        return "irreducible";
    case script::FallbackReason::NestedExit:
        return "nested exit";
    case script::FallbackReason::Duplication:
        return "duplication";
    }
    return "";
}

} // namespace

// Runs only when DWE_CONTENT names a content folder that dwx extracted (scripts/dev.sh mounts it). Structures every
// routine of every event script, checks that it runs like its graph, and reports the shape of the converted code.
TEST_CASE("the user's disc, extracted: every event script routine structures and runs like its graph") {
    const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
    if (!content) {
        MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
        return;
    }
    const auto folder = findScriptFolder(*content);
    REQUIRE_MESSAGE(folder.has_value(), "no script/maphead.yaml under DWE_CONTENT");
    std::map<std::uint16_t, script::ScriptListing> listings;
    std::map<std::uint16_t, std::set<std::uint16_t>> sectionsByScript;
    for (const auto& [id, path] : findListings(folder.value_or(std::filesystem::path{}))) {
        CAPTURE(path.string());
        const auto yaml = dwe::core::parseYaml(readText(path));
        REQUIRE(yaml.has_value());
        auto listing = script::readScriptListing(*yaml);
        REQUIRE_MESSAGE(listing.has_value(), listing.error().message);
        for (const script::ListingSection& section : listing->sections) {
            sectionsByScript[id].insert(section.id);
        }
        listings.emplace(id, std::move(*listing));
    }

    DiscReport report;
    std::size_t sectionsWithBody = 0;
    for (const auto& [id, listing] : listings) {
        CAPTURE(id);
        const auto flow = script::buildScriptFlow(listing);
        REQUIRE_MESSAGE(flow.has_value(), flow.error().message);
        const auto structured = script::structureScript(listing);
        REQUIRE(structured.has_value());
        ++report.scripts;
        countReferences(listing, sectionsByScript, report);
        report.sectionsSharingCode += sectionsSharingCode(*flow);
        report.statistics += script::measureStructure(*structured);
        report.gotoFree += structure_parity::TransferCheck::goesNowhereElse(*structured) ? 1 : 0;
        for (const script::StructuredRoutine& routine : structured->routines) {
            CHECK_FALSE(routine.body.empty());
            sectionsWithBody += routine.kind == script::RoutineKind::Section && !routine.body.empty() ? 1 : 0;
            const bool runsAlike = structure_parity::runsLikeTheGraph(listing, flow->graph, *structured, routine,
                                                                      structure_parity::ParitySeeds);
            report.parityFailures += runsAlike ? 0 : 1;
            if (routine.kind == script::RoutineKind::Section && !routine.fallback) {
                ++report.structuredSections;
            }
            if (routine.fallback) {
                report.fallbacks.push_back(std::format("script {} routine at {:#06x}: {}", id,
                                                       listing.instructions[routine.instruction].offset,
                                                       reasonName(*routine.fallback)));
            }
        }
    }

    const script::StructureStatistics& total = report.statistics;
    MESSAGE(std::format("scripts {}, sections {}, subroutines {}, shared tails {}", report.scripts, total.sections,
                        total.subroutines, total.sharedTails));
    MESSAGE(std::format("sections structured without a state machine: {} of {} ({:.1f} %)", report.structuredSections,
                        total.sections,
                        100.0 * static_cast<double>(report.structuredSections) /
                            static_cast<double>(std::max<std::size_t>(total.sections, 1))));
    MESSAGE(std::format("state machines {} (irreducible {}, nested exits {}, duplication {})", total.stateMachines,
                        total.irreducible, total.nestedExits, total.duplications));
    MESSAGE(std::format("ifs {}, loops {}, breaks {}, choices {}, tail calls {}, max depth {}", total.ifs, total.loops,
                        total.breaks, total.choices, total.tailCalls, total.maxDepth));
    MESSAGE(std::format("routines holding copied code {}; instructions emitted {} for {} distinct ({:.3f}x)",
                        total.copiedRoutines, total.emittedInstructions, total.distinctInstructions,
                        static_cast<double>(total.emittedInstructions) /
                            static_cast<double>(std::max<std::size_t>(total.distinctInstructions, 1))));
    MESSAGE(
        std::format("IFs with several jump entries {}, file calls {}, file jumps {}, set_script {}, references to a "
                    "missing section {}, sections sharing code with another {}",
                    report.multiJumpIfs, report.fileCalls, report.fileJumps, report.loadScripts,
                    report.danglingReferences, report.sectionsSharingCode));
    for (const std::string& fallback : report.fallbacks) {
        MESSAGE(fallback);
    }
    CHECK(report.parityFailures == 0);
    CHECK(report.gotoFree == report.scripts);
    CHECK(sectionsWithBody == total.sections);
    CHECK(total.sections == ExpectedSections);
    CHECK(report.multiJumpIfs == ExpectedMultiJumpIfs);
    CHECK(report.structuredSections == ExpectedStructuredSections);
    CHECK(total.stateMachines == ExpectedStateMachines);
    CHECK(total.irreducible == ExpectedIrreducible);
    CHECK(total.nestedExits == ExpectedNestedExits);
}
