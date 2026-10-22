#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/guarded_walk.hpp>
#include <dwe/formats/script/map_changes.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/script_yaml.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include "script_builder.hpp"

using dwe::formats::script::BranchTerm;
using dwe::formats::script::ChoiceTerm;
using dwe::formats::script::findMapChanges;
using dwe::formats::script::GuardTerm;
using dwe::formats::script::listingOf;
using dwe::formats::script::MapChange;
using dwe::formats::script::readScriptListing;
using dwe::formats::script::ScriptListing;
using dwe::formats::script::ScriptLookup;
using dwe::formats::script::scriptToYaml;
using dwe::formats::script::StructuredScript;
using dwe::formats::script::TriggerEffect;
using dwe::formats::script::WalkableScript;
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
