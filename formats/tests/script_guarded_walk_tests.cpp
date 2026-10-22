#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/guarded_walk.hpp>
#include <dwe/formats/script/map_changes.hpp>
#include <dwe/formats/script/routine_writes.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/script_warps.hpp>
#include <dwe/formats/script/script_yaml.hpp>
#include <dwe/formats/script/structuring.hpp>
#include <dwe/test_support/script_warp_fixtures.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "schema_check.hpp"
#include "script_builder.hpp"

using dwe::formats::script::BranchTerm;
using dwe::formats::script::ChoiceTerm;
using dwe::formats::script::findMapChanges;
using dwe::formats::script::ForgetTerm;
using dwe::formats::script::GuardTerm;
using dwe::formats::script::listingOf;
using dwe::formats::script::MapChange;
using dwe::formats::script::readScriptListing;
using dwe::formats::script::RecursionTerm;
using dwe::formats::script::ScriptListing;
using dwe::formats::script::ScriptLookup;
using dwe::formats::script::scriptToYaml;
using dwe::formats::script::StructuredScript;
using dwe::formats::script::TriggerEffect;
using dwe::formats::script::WalkableScript;
using dwe::formats::script::WalkMonitor;
using dwe::formats::script::WalkOptions;
using script_fixtures::ScriptBuilder;

namespace {

constexpr std::uint16_t Section = 0x33;
constexpr std::uint16_t CodeStart = 8; // after one section and the 0xFFFF
constexpr unsigned NoSection = 0xFF;

// IF, its pad, then entries of {code, pad, u16}: a trigger test (code 0 set, 1 clear) and a goto_if_false.
void ifTriggerElseGoto(ScriptBuilder& script, unsigned trigger, bool clear, unsigned target) {
    script.u8({0x19, 0x00, clear ? 0x01U : 0x00U, 0x00}).u16(trigger);
    script.u8({0x18, 0x00}).u16(target);
    script.u8({0x19, 0x00});
}

void warpTo(ScriptBuilder& script, unsigned screen, unsigned exit) {
    script.u8({0x4B, screen, exit, NoSection});
}

void setTrigger(ScriptBuilder& script, unsigned trigger) {
    script.u8({0x1C, 0x00}).u16(trigger);
}

void setScript(ScriptBuilder& script, unsigned id, unsigned map) {
    script.u8({0xFB, 0x00}).u16(id).u16(map);
}

void callRoutine(ScriptBuilder& script, unsigned routine) {
    script.u8({0x64, routine});
}

void giveItem(ScriptBuilder& script, unsigned item) {
    script.u8({0x28, 0x00, item, 1});
}

void endSection(ScriptBuilder& script) {
    script.u8({0xFE, 0x00});
}

// A script decoded, listed and structured.
struct Prepared {
    ScriptListing listing;
    StructuredScript structured;
};

// Ends the code with end_script, as every script does, and decodes it.
Prepared prepare(ScriptBuilder script) {
    script.u8({0xFF, 0x00});
    const auto disassembly = dwe::formats::script::disassembleScript(script.bytes());
    REQUIRE(disassembly.has_value());
    const std::string firstUnknown = disassembly->unknowns.empty() ? std::string{} : disassembly->unknowns.front();
    INFO(firstUnknown);
    REQUIRE(disassembly->unknowns.empty());
    Prepared prepared{.listing = listingOf(*disassembly), .structured = {}};
    auto structured = dwe::formats::script::structureScript(prepared.listing);
    REQUIRE(structured.has_value());
    prepared.structured = std::move(*structured);
    return prepared;
}

ScriptLookup lookupOf(const std::map<std::uint16_t, Prepared>& scripts) {
    return [&scripts](std::uint16_t id) -> std::optional<WalkableScript> {
        const auto found = scripts.find(id);
        if (found == scripts.end()) {
            return std::nullopt;
        }
        return WalkableScript{.listing = &found->second.listing, .structured = &found->second.structured};
    };
}

const BranchTerm& branchOf(const GuardTerm& term) {
    REQUIRE(std::holds_alternative<BranchTerm>(term.decision));
    return std::get<BranchTerm>(term.decision);
}

const ForgetTerm& forgetOf(const GuardTerm& term) {
    REQUIRE(std::holds_alternative<ForgetTerm>(term.decision));
    return std::get<ForgetTerm>(term.decision);
}

const MapChange& changeTo(const std::vector<MapChange>& changes, std::int32_t screen) {
    const auto found = std::ranges::find(changes, std::optional{screen}, &MapChange::screen);
    REQUIRE(found != changes.end());
    return *found;
}

} // namespace

TEST_CASE("a script listing read back from its YAML is the listing of its disassembly") {
    ScriptBuilder script{{{Section, CodeStart}}};
    ifTriggerElseGoto(script, 5, true, CodeStart + 16);
    warpTo(script, 3, 1);
    warpTo(script, 4, 2);
    endSection(script);
    const auto disassembly = dwe::formats::script::disassembleScript(script.bytes());
    REQUIRE(disassembly.has_value());
    const auto read = readScriptListing(scriptToYaml(*disassembly));
    REQUIRE(read.has_value());
    CHECK(*read == listingOf(*disassembly));
    CHECK(read->instructions[1].role == dwe::formats::script::FlowRole::MapChange);
    CHECK(read->instructions[1].argument("screen") == 3);
    CHECK(read->instructions[0].conditions.size() == 2);
}

TEST_CASE("each map change of a section comes with the decision that leads to it") {
    ScriptBuilder script{{{Section, CodeStart}}};
    ifTriggerElseGoto(script, 5, true, CodeStart + 16); // 8..19: if trigger 5 is clear, else go to 24
    warpTo(script, 3, 1);                               // 20
    warpTo(script, 4, 2);                               // 24
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts));
    REQUIRE(changes.size() == 2);
    const MapChange& whenClear = changeTo(changes, 3);
    CHECK(whenClear.exit == 1);
    CHECK(whenClear.arrivalSection == dwe::formats::script::NoArrivalSection);
    REQUIRE(whenClear.path.guard.terms.size() == 1);
    CHECK(branchOf(whenClear.path.guard.terms[0]).holds);
    REQUIRE(branchOf(whenClear.path.guard.terms[0]).tests.size() == 1);
    CHECK(branchOf(whenClear.path.guard.terms[0]).tests[0].code == 0x01);
    const MapChange& whenSet = changeTo(changes, 4);
    REQUIRE(whenSet.path.guard.terms.size() == 1);
    CHECK_FALSE(branchOf(whenSet.path.guard.terms[0]).holds);
    CHECK_FALSE(whenSet.path.guard.unstructured);
}

TEST_CASE("a trigger set on the path decides a later test of it, and the path keeps the effect") {
    ScriptBuilder script{{{Section, CodeStart}}};
    setTrigger(script, 5);                              // 8
    ifTriggerElseGoto(script, 5, true, CodeStart + 20); // 12..23
    warpTo(script, 3, 1);                               // 24
    warpTo(script, 4, 2);                               // 28
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts));
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].screen == 4);
    CHECK(changes[0].path.guard.terms.empty());
    CHECK(changes[0].path.effects == std::vector<TriggerEffect>{{.trigger = 5, .set = true}});
}

TEST_CASE("a new game's clear triggers decide the tests of them") {
    ScriptBuilder script{{{Section, CodeStart}}};
    ifTriggerElseGoto(script, 9, false, CodeStart + 16); // if trigger 9 is set
    warpTo(script, 3, 1);
    warpTo(script, 4, 2);
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes =
        findMapChanges(1, Section, lookupOf(scripts), WalkOptions{.knownTriggers = {}, .othersClear = true});
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].screen == 4);
}

TEST_CASE("the routines of the game that write triggers are each listed once, by a routine the game has") {
    using dwe::formats::script::WriteSet;
    std::set<std::pair<std::string_view, std::uint8_t>> seen;
    for (const auto& entry : dwe::formats::script::routineWrites()) {
        INFO(entry.op, " ", static_cast<unsigned>(entry.routine));
        CHECK(seen.emplace(entry.op, entry.routine).second);
        const bool callRoutine = entry.op == dwe::formats::script::CallRoutineOp;
        CHECK((callRoutine || entry.op == dwe::formats::script::CallDigimonRoutineOp));
        CHECK(entry.routine <=
              (callRoutine ? dwe::formats::script::LastCallRoutine : dwe::formats::script::LastDigimonRoutine));
        CHECK_FALSE(entry.source.empty());
        CHECK(entry.triggers.empty() == (entry.set == WriteSet::Any));
        CHECK(std::ranges::is_sorted(entry.triggers));
        CHECK(std::ranges::adjacent_find(entry.triggers) == entry.triggers.end());
    }
    // The discard menu sets trigger 3 (script_common.c: openDiscardItem); removing an item writes none.
    const auto* discard = dwe::formats::script::routineWrites(dwe::formats::script::CallRoutineOp, 0);
    REQUIRE(discard != nullptr);
    CHECK(discard->triggers == std::vector<std::uint16_t>{3});
    CHECK(dwe::formats::script::routineWrites(dwe::formats::script::CallRoutineOp, 0x0c) == nullptr);
}

TEST_CASE("a routine of the game that writes a trigger the path knew leaves a forget term and both arms of its test") {
    constexpr unsigned Trigger = 3;
    ScriptBuilder script{{{Section, CodeStart}}};
    setTrigger(script, Trigger);                               // 8
    callRoutine(script, 0);                                    // 12: the discard menu, which writes trigger 3
    ifTriggerElseGoto(script, Trigger, false, CodeStart + 22); // 14..25: if trigger 3, else go to 30
    warpTo(script, 3, 1);                                      // 26
    warpTo(script, 4, 2);                                      // 30
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts));
    REQUIRE(changes.size() == 2);
    for (const std::int32_t screen : {3, 4}) {
        const MapChange& change = changeTo(changes, screen);
        REQUIRE(change.path.guard.terms.size() == 2);
        const ForgetTerm& forget = forgetOf(change.path.guard.terms[0]);
        CHECK(forget.place.script == 1);
        CHECK(forget.place.instruction == 1);
        CHECK(forget.triggers == std::vector<std::uint16_t>{Trigger});
        CHECK(branchOf(change.path.guard.terms[1]).holds == (screen == 3));
    }
}

TEST_CASE("give_item makes the walk forget trigger 0, where it leaves its result") {
    ScriptBuilder script{{{Section, CodeStart}}};
    giveItem(script, 1);                                // 8
    ifTriggerElseGoto(script, 0, true, CodeStart + 20); // 12..23: if trigger 0 is clear, else go to 28
    warpTo(script, 3, 1);                               // 24
    warpTo(script, 4, 2);                               // 28
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    // A new game knows trigger 0 clear; the item given leaves it unknown.
    const auto changes =
        findMapChanges(1, Section, lookupOf(scripts), WalkOptions{.knownTriggers = {}, .othersClear = true});
    REQUIRE(changes.size() == 2);
    REQUIRE_FALSE(changeTo(changes, 4).path.guard.terms.empty());
    CHECK(forgetOf(changeTo(changes, 4).path.guard.terms[0]).triggers == std::vector<std::uint16_t>{0});
}

TEST_CASE("a routine that writes known triggers leaves the tests of the others decided") {
    constexpr unsigned Trigger = 5;
    // The discard menu writes trigger 3 only; removing an item and routine 0x21, whose triggers no path knows, leave
    // no term.
    for (const unsigned routine : {0x00U, 0x0cU, 0x21U}) {
        INFO(routine);
        ScriptBuilder script{{{Section, CodeStart}}};
        setTrigger(script, Trigger);                               // 8
        callRoutine(script, routine);                              // 12
        ifTriggerElseGoto(script, Trigger, false, CodeStart + 22); // 14..25
        warpTo(script, 3, 1);                                      // 26
        warpTo(script, 4, 2);                                      // 30
        const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
        const auto changes = findMapChanges(1, Section, lookupOf(scripts));
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].screen == 3);
        CHECK(changes[0].path.guard.terms.size() == (routine == 0 ? 1U : 0U));
    }
}

TEST_CASE("a routine whose writes the game does not bound makes the walk forget every trigger") {
    constexpr unsigned SaveMachine = 0x30; // it may load a saved game
    ScriptBuilder script{{{Section, CodeStart}}};
    callRoutine(script, SaveMachine);                    // 8
    ifTriggerElseGoto(script, 9, false, CodeStart + 18); // 10..21
    warpTo(script, 3, 1);                                // 22
    warpTo(script, 4, 2);                                // 26
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes =
        findMapChanges(1, Section, lookupOf(scripts), WalkOptions{.knownTriggers = {}, .othersClear = true});
    REQUIRE(changes.size() == 2);
    CHECK(forgetOf(changeTo(changes, 3).path.guard.terms[0]).triggers.empty());
}

TEST_CASE("a map change after a call of a routine that returns and a jump is found") {
    constexpr unsigned Routine = CodeStart + 10;
    constexpr unsigned After = Routine + 2;
    ScriptBuilder script{{{Section, CodeStart}}};
    script.u8({0x13, 0x00}).u16(Routine); // 8 jump_and_link
    script.u8({0x16, 0x00}).u16(After);   // 12 jump_to
    endSection(script);                   // 16
    script.u8({0x15, 0x00});              // 18: the routine: jump_return
    warpTo(script, 3, 1);                 // 20
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts));
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].screen == 3);
    CHECK(changes[0].path.guard.terms.empty());
}

TEST_CASE("a decision whose arms meet again with the same state leaves no term after it") {
    ScriptBuilder script{{{Section, CodeStart}}};
    ifTriggerElseGoto(script, 7, false, CodeStart + 16); // 8..19: if trigger 7, else go to 24
    script.u8({0x1E, 0x00, 5, 1});                       // 20 set_pstat 5 = 1
    warpTo(script, 3, 1);                                // 24
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts));
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].path.guard.terms.empty());
}

TEST_CASE("a SWITCH gives each arm's map change a choice term") {
    ScriptBuilder script{{{Section, CodeStart}}};
    script.u8({0x18, 7}).u16(2).u16(CodeStart + 8).u16(CodeStart + 12); // 8 switch on pstat 7, 2 cases
    warpTo(script, 3, 1);                                               // 16
    warpTo(script, 4, 2);                                               // 20
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts));
    REQUIRE(changes.size() == 2);
    for (const std::int32_t screen : {3, 4}) {
        const MapChange& change = changeTo(changes, screen);
        REQUIRE(change.path.guard.terms.size() == 1);
        REQUIRE(std::holds_alternative<ChoiceTerm>(change.path.guard.terms[0].decision));
        CHECK(std::get<ChoiceTerm>(change.path.guard.terms[0].decision).arm == (screen == 3 ? 0U : 1U));
        CHECK_FALSE(std::get<ChoiceTerm>(change.path.guard.terms[0].decision).selection);
    }
}

TEST_CASE("a map change in a subroutine or in another script is found through the call or the jump") {
    ScriptBuilder caller{{{Section, CodeStart}}};
    caller.u8({0x13, 0x00}).u16(CodeStart + 12); // 8 jump_and_link 20
    caller.u8({0x17, 0x00}).u16(2).u16(0x40);    // 12 jump_to_file script 2, section 0x40
    endSection(caller);                          // 18
    warpTo(caller, 3, 1);                        // 20: the subroutine
    ScriptBuilder callee{{{0x40, CodeStart}}};
    warpTo(callee, 5, 0);
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(caller)}, {2, prepare(callee)}};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts));
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].screen == 3);
    CHECK(changes[0].path.place.script == 1);

    ScriptBuilder returning{{{Section, CodeStart}}};
    returning.u8({0x13, 0x00}).u16(CodeStart + 12); // 8 jump_and_link 20
    returning.u8({0x17, 0x00}).u16(2).u16(0x40);    // 12 jump_to_file script 2, section 0x40
    endSection(returning);                          // 18
    returning.u8({0x15, 0x00});                     // 20 jump_return
    const std::map<std::uint16_t, Prepared> jumping{{1, prepare(returning)}, {2, prepare(callee)}};
    const auto jumped = findMapChanges(1, Section, lookupOf(jumping));
    REQUIRE(jumped.size() == 1);
    CHECK(jumped[0].screen == 5);
    CHECK(jumped[0].path.place.script == 2);
}

TEST_CASE("paths merged past the limit still reach every map change, and no change after the merge is certain") {
    // Seven IFs, each of whose holding arm sets a trigger of its own: 128 paths that differ in what they know.
    constexpr unsigned Decisions = 7;
    constexpr unsigned FirstTested = 10;
    constexpr unsigned FirstSet = 20;
    constexpr unsigned Block = 16; // an IF (12 bytes) and a set_trigger (4)
    ScriptBuilder script{{{Section, CodeStart}}};
    for (unsigned decision = 0; decision < Decisions; ++decision) {
        const unsigned at = CodeStart + (decision * Block);
        ifTriggerElseGoto(script, FirstTested + decision, false, at + Block);
        setTrigger(script, FirstSet + decision);
    }
    constexpr unsigned Last = CodeStart + (Decisions * Block);
    ifTriggerElseGoto(script, FirstSet, false, Last + 16); // the first decision's trigger: known on some paths only
    warpTo(script, 4, 0);
    warpTo(script, 5, 0);
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    dwe::formats::script::WalkMonitor monitor;
    const auto merged = findMapChanges(1, Section, lookupOf(scripts),
                                       {.knownTriggers = {},
                                        .othersClear = false,
                                        .maximumPaths = dwe::formats::script::DefaultMaximumPaths,
                                        .maximumRoutineWalks = dwe::formats::script::DefaultMaximumRoutineWalks,
                                        .monitor = &monitor});
    const auto enumerated = findMapChanges(1, Section, lookupOf(scripts),
                                           {.knownTriggers = {},
                                            .othersClear = false,
                                            .maximumPaths = std::numeric_limits<std::size_t>::max(),
                                            .maximumRoutineWalks = dwe::formats::script::DefaultMaximumRoutineWalks,
                                            .monitor = nullptr});
    CHECK(monitor.merges == 1);
    const auto screens = [](const std::vector<MapChange>& changes) {
        std::set<std::optional<std::int32_t>> found;
        for (const MapChange& change : changes) {
            found.insert(change.screen);
        }
        return found;
    };
    CHECK(screens(enumerated) == std::set<std::optional<std::int32_t>>{4, 5});
    CHECK(screens(merged) == screens(enumerated));
    CHECK(enumerated.size() > dwe::formats::script::DefaultMaximumPaths);
    for (const MapChange& change : merged) {
        CHECK(change.path.guard.unstructured);
        // No decision is shared by every path, so none is left, and the first trigger is not known after the merge.
        CHECK(change.path.guard.terms.size() <= 1);
        CHECK(std::ranges::none_of(change.path.state,
                                   [](const TriggerEffect& known) { return known.trigger == FirstSet; }));
    }
}

TEST_CASE("a routine summarized after it was walked too often gives what walking it in full gives") {
    constexpr unsigned Subroutine = CodeStart + 18;
    constexpr unsigned WarpAt = Subroutine + 14;
    constexpr unsigned Trigger = 7;
    ScriptBuilder script{{{Section, CodeStart}}};
    script.u8({0x13, 0x00}).u16(Subroutine);          // 8 jump_and_link
    script.u8({0x13, 0x00}).u16(Subroutine);          // 12 jump_and_link: a second walk, the same knowledge
    setTrigger(script, Trigger);                      // 16
    script.u8({0x13, 0x00}).u16(Subroutine);          // 20 jump_and_link: the trigger is known now
    endSection(script);                               // 24
    ifTriggerElseGoto(script, Trigger, true, WarpAt); // 26: the subroutine
    script.u8({0x15, 0x00});                          // 38 jump_return
    warpTo(script, 3, 0);                             // 40
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const WalkOptions full{.knownTriggers = {},
                           .othersClear = false,
                           .maximumPaths = WalkOptions{}.maximumPaths,
                           .maximumRoutineWalks = std::numeric_limits<std::size_t>::max()};
    const WalkOptions summarized{.knownTriggers = {},
                                 .othersClear = false,
                                 .maximumPaths = WalkOptions{}.maximumPaths,
                                 .maximumRoutineWalks = 1};
    const auto walked = findMapChanges(1, Section, lookupOf(scripts), full);
    CHECK(walked.size() == 3);
    CHECK(findMapChanges(1, Section, lookupOf(scripts), summarized) == walked);
    CHECK(findMapChanges(1, Section, lookupOf(scripts)) == walked);
    // The third call knows the trigger, so its map change has no decision of its own and keeps the effect.
    const auto known =
        std::ranges::find_if(walked, [](const MapChange& change) { return !change.path.effects.empty(); });
    REQUIRE(known != walked.end());
    CHECK(known->path.guard.terms.size() == 2);
}

TEST_CASE("routines that call themselves are summarized once each, not walked again on every call") {
    // A chain of routines; each may call itself, which the recursion check cuts, and calls the next one twice. A
    // summary that a cut made unusable again would walk the last routine about 2^Depth times; kept, there is one for
    // each routine and each thing the paths into it know, at most Depth of them per routine. The map change's path
    // crosses every routine's decision on every call, about 2^Depth terms, as a full walk gives too, which keeps the
    // chain short.
    constexpr unsigned Depth = 12;
    constexpr unsigned FirstRoutine = CodeStart + 14;
    constexpr unsigned RoutineSize = 26;
    constexpr unsigned FirstTrigger = 100;
    constexpr auto Budget = std::chrono::seconds{60};
    ScriptBuilder script{{{Section, CodeStart}}};
    script.u8({0x13, 0x00}).u16(FirstRoutine); // 8 jump_and_link
    script.u8({0x13, 0x00}).u16(FirstRoutine); // 12 jump_and_link: a second walk
    warpTo(script, 3, 0);                      // 16: reached only through the routines' returns
    endSection(script);                        // 20
    for (unsigned depth = 0; depth + 1 < Depth; ++depth) {
        const unsigned at = FirstRoutine + (depth * RoutineSize);
        const unsigned next = at + RoutineSize;
        ifTriggerElseGoto(script, FirstTrigger + depth, true, at + 16); // +0
        script.u8({0x13, 0x00}).u16(at);                                // +12 jump_and_link: itself
        script.u8({0x13, 0x00}).u16(next);                              // +16 jump_and_link
        script.u8({0x13, 0x00}).u16(next);                              // +20 jump_and_link
        script.u8({0x15, 0x00});                                        // +24 jump_return
    }
    script.u8({0x15, 0x00}); // the last routine: jump_return
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    WalkMonitor monitor{.deadline = std::chrono::steady_clock::now() + Budget,
                        .stopped = false,
                        .merges = 0,
                        .summarizedCalls = 0,
                        .summaryWalks = 0,
                        .recursionCuts = 0};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts),
                                        {.knownTriggers = {},
                                         .othersClear = false,
                                         .maximumPaths = WalkOptions{}.maximumPaths,
                                         .maximumRoutineWalks = 1,
                                         .monitor = &monitor});
    CHECK_FALSE(monitor.stopped);
    CHECK(monitor.summaryWalks <= Depth * Depth);
    REQUIRE_FALSE(changes.empty());
    CHECK(std::ranges::all_of(changes, [](const MapChange& change) { return change.screen == 3; }));
}

TEST_CASE("a decision whose arm calls the routine again leaves a recursion term, not one arm alone") {
    constexpr unsigned Routine = CodeStart + 6;
    constexpr unsigned After = Routine + 16;
    constexpr unsigned Trigger = 9;
    ScriptBuilder script{{{Section, CodeStart}}};
    script.u8({0x13, 0x00}).u16(Routine);            // 8 jump_and_link
    endSection(script);                              // 12
    ifTriggerElseGoto(script, Trigger, true, After); // 14: the routine
    script.u8({0x13, 0x00}).u16(Routine);            // 26 jump_and_link: itself
    warpTo(script, 3, 0);                            // 30
    script.u8({0x15, 0x00});                         // 34 jump_return
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts));
    REQUIRE(changes.size() == 2);
    // The arm that skips the call, as before; and the arm that calls the routine again, which goes on after the call.
    const auto skipped =
        std::ranges::find_if(changes, [](const MapChange& change) { return change.path.guard.terms.size() == 1; });
    const auto recursed =
        std::ranges::find_if(changes, [](const MapChange& change) { return change.path.guard.terms.size() == 2; });
    REQUIRE(skipped != changes.end());
    REQUIRE(recursed != changes.end());
    CHECK_FALSE(branchOf(skipped->path.guard.terms[0]).holds);
    CHECK(branchOf(recursed->path.guard.terms[0]).holds);
    const auto* recursion = std::get_if<RecursionTerm>(&recursed->path.guard.terms[1].decision);
    REQUIRE(recursion != nullptr);
    CHECK(recursion->routine.script == 1);
    // After the recursion the path knows no trigger, not even the one it tested.
    CHECK(recursed->path.state.empty());
}

TEST_CASE("a map change after a routine calls itself, not as its last instruction, is found") {
    constexpr unsigned Routine = CodeStart + 6;
    ScriptBuilder script{{{Section, CodeStart}}};
    script.u8({0x13, 0x00}).u16(Routine); // 8 jump_and_link
    endSection(script);                   // 12
    script.u8({0x13, 0x00}).u16(Routine); // 14: the routine: jump_and_link to itself
    warpTo(script, 4, 1);                 // 18
    script.u8({0x15, 0x00});              // 22 jump_return
    const std::map<std::uint16_t, Prepared> scripts{{1, prepare(script)}};
    const auto changes = findMapChanges(1, Section, lookupOf(scripts));
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].screen == 4);
    REQUIRE(changes[0].path.guard.terms.size() == 1);
    CHECK(std::holds_alternative<RecursionTerm>(changes[0].path.guard.terms[0].decision));
}

TEST_CASE("a section whose id an earlier entry of the table has is never walked, as the game takes the first") {
    constexpr std::uint16_t Map = 5;
    constexpr std::uint16_t MapScript = 1;
    constexpr std::uint16_t StepSection = 51;
    ScriptBuilder mapHead{{{Map, CodeStart}}};
    setScript(mapHead, MapScript, Map); // 8
    endSection(mapHead);                // 14
    // Two entries for section 51, as scripts 4 and 52 of the disc have: the first runs, the second never does.
    ScriptBuilder mapScript{{{StepSection, 12}, {StepSection, 16}}};
    warpTo(mapScript, 6, 1); // 12
    warpTo(mapScript, 7, 2); // 16
    const Prepared prepared = prepare(mapScript);
    CHECK(prepared.listing.runnableSections() ==
          std::vector<dwe::formats::script::ListingSection>{{.id = StepSection, .entry = 12}});
    const std::map<std::uint16_t, Prepared> scripts{{dwe::formats::script::MapHeadScript, prepare(mapHead)},
                                                    {MapScript, prepared}};
    const auto changes = findMapChanges(MapScript, StepSection, lookupOf(scripts));
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].screen == 6);
    const auto found = dwe::formats::script::findScriptWarps(lookupOf(scripts));
    REQUIRE(found.warps.size() == 1);
    CHECK(found.warps[0].change.screen == 6);
}

TEST_CASE("the map changes of a map's script are found on the maps MAPHEAD loads it on, by what runs them") {
    using dwe::formats::script::WarpVia;
    constexpr std::uint16_t Map = 5;
    constexpr std::uint16_t Global = 0x40;
    constexpr std::uint16_t StepSection = 51;
    constexpr std::uint16_t MapScript = 1;
    ScriptBuilder mapHead{{{Map, 16}, {dwe::formats::script::NewGameSection, 24}, {Global, 32}}};
    setScript(mapHead, MapScript, Map);                                  // 16
    endSection(mapHead);                                                 // 22
    setScript(mapHead, MapScript, dwe::formats::script::NewGameSection); // 24
    endSection(mapHead);                                                 // 30
    warpTo(mapHead, 7, 0);                                               // 32
    ScriptBuilder mapScript{{{StepSection, 12}, {dwe::formats::script::EnterSection, 16}}};
    warpTo(mapScript, 6, 2);  // 12
    setTrigger(mapScript, 9); // 16
    endSection(mapScript);    // 20
    const std::map<std::uint16_t, Prepared> scripts{{dwe::formats::script::MapHeadScript, prepare(mapHead)},
                                                    {MapScript, prepare(mapScript)}};
    const auto found = dwe::formats::script::findScriptWarps(lookupOf(scripts));
    CHECK(found.otherMapHeadSections == std::vector<std::uint16_t>{Global});
    REQUIRE(found.warps.size() == 2);
    CHECK(found.warps[0].map == Map);
    CHECK(found.warps[0].via == WarpVia::Step);
    CHECK(found.warps[0].section == StepSection);
    CHECK(found.warps[0].script == MapScript);
    CHECK(found.warps[0].change.screen == 6);
    CHECK(found.warps[0].change.exit == 2);
    CHECK(found.warps[1].map == dwe::formats::script::NewGameSection);

    const auto triggers = dwe::formats::script::findNewGameTriggers(lookupOf(scripts));
    REQUIRE(triggers.has_value());
    const auto newGame = triggers.value_or(dwe::formats::script::NewGameTriggers{});
    CHECK(newGame.map == dwe::formats::script::NewGameSection);
    CHECK(newGame.script == MapScript);
    CHECK(newGame.set == std::vector<std::uint16_t>{9});
    CHECK(newGame.agreed);
    CHECK(newGame.unknown == std::vector<std::uint16_t>{0x203, 0x211});
    const dwe::formats::script::MapName mapName = [](std::uint16_t map) -> std::optional<std::string> {
        return map == Map ? std::optional<std::string>{"room01"} : std::nullopt;
    };
    const auto document = dwe::formats::script::scriptWarpsToYaml(found, lookupOf(scripts), mapName);
    const auto problems = dwe::formats::test::checkAgainstSchema(document, "script_warps");
    CHECK_MESSAGE(problems.empty(), (problems.empty() ? std::string{} : problems.front()));
    const auto yaml = dwe::core::emitYaml(document);
    REQUIRE(yaml.has_value());
    CHECK(yaml->starts_with("warps:\n- map: room01\n  via: step\n  section: 51\n  to:\n    screen: 6\n    exit: 2\n"));
    CHECK(yaml->contains("source:\n    script: 1\n    section: 51\n    offset: 12\n"));
    const auto storyDocument = dwe::formats::script::newGameTriggersToYaml(newGame, mapName);
    CHECK(dwe::formats::test::checkAgainstSchema(storyDocument, "new_game_story").empty());
    const auto story = dwe::core::emitYaml(storyDocument);
    REQUIRE(story.has_value());
    CHECK(*story == "map: 204\nscript: 1\ntriggers_set:\n- 9\ntriggers_unknown:\n- 515\n- 529\n");
    CHECK(dwe::formats::script::warpViaOf(80) == WarpVia::Confirm);
    CHECK(dwe::formats::script::warpViaOf(dwe::formats::script::EnterSection) == WarpVia::Enter);
    CHECK(dwe::formats::script::warpViaOf(3) == WarpVia::Other);
}

TEST_CASE("a map change behind a recursion the walk cut reaches field/script_warps with the routine's site") {
    constexpr std::uint16_t House = 5;
    constexpr std::uint16_t Town = 7;
    constexpr std::uint16_t MapScript = 1;
    constexpr std::uint16_t StepSection = 51;
    constexpr unsigned Routine = CodeStart + 6;
    ScriptBuilder mapHead{{{House, CodeStart}}};
    setScript(mapHead, MapScript, House); // 8
    endSection(mapHead);                  // 14
    ScriptBuilder mapScript{{{StepSection, CodeStart}}};
    mapScript.u8({0x13, 0x00}).u16(Routine); // 8 jump_and_link
    endSection(mapScript);                   // 12
    mapScript.u8({0x13, 0x00}).u16(Routine); // 14: the routine: jump_and_link to itself
    warpTo(mapScript, Town, 2);              // 18
    mapScript.u8({0x15, 0x00});              // 22 jump_return
    const std::map<std::uint16_t, Prepared> scripts{{dwe::formats::script::MapHeadScript, prepare(mapHead)},
                                                    {MapScript, prepare(mapScript)}};
    const auto found = dwe::formats::script::findScriptWarps(lookupOf(scripts));
    const dwe::formats::script::MapName mapName = [](std::uint16_t map) -> std::optional<std::string> {
        if (map == House) {
            return std::string{dwe::test_support::RecursionWarpHouse};
        }
        return map == Town ? std::optional{std::string{dwe::test_support::RecursionWarpTown}} : std::nullopt;
    };
    const auto document = dwe::formats::script::scriptWarpsToYaml(found, lookupOf(scripts), mapName);
    CHECK(dwe::formats::test::checkAgainstSchema(document, "script_warps").empty());
    const auto yaml = dwe::core::emitYaml(document);
    REQUIRE(yaml.has_value());
    CHECK(*yaml == dwe::test_support::RecursionWarpYaml);
}
