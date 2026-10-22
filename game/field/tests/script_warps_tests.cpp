#include <dwe/assets/new_game_story.hpp>
#include <dwe/game/field/script_warps.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using dwe::game::field::Comparison;
using dwe::game::field::decide;
using dwe::game::field::evaluate;
using dwe::game::field::GuardBranch;
using dwe::game::field::GuardChoice;
using dwe::game::field::GuardFacts;
using dwe::game::field::GuardForget;
using dwe::game::field::GuardRecursion;
using dwe::game::field::GuardTest;
using dwe::game::field::newGameTriggers;
using dwe::game::field::parseScriptWarps;
using dwe::game::field::ScriptWarp;
using dwe::game::field::SignedScriptStat;
using dwe::game::field::TestCombine;
using dwe::game::field::TriggerState;
using dwe::game::field::Truth;
using dwe::game::field::UnknownPolicy;
using dwe::game::field::WarpGuard;
using dwe::game::field::warpOnCell;
using dwe::game::field::WarpVia;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// Trigger 5 is set, 6 is clear, every other one unknown.
const TriggerState knownTriggers = [](std::int32_t trigger) -> std::optional<bool> {
    if (trigger == 5) {
        return true;
    }
    if (trigger == 6) {
        return false;
    }
    return std::nullopt;
};

GuardTest trigger(std::int32_t number, bool clear, TestCombine combine = TestCombine::Replace) {
    return {.test = "trigger", .combine = combine, .trigger = number, .clear = clear};
}

WarpGuard ifTests(std::vector<GuardTest> tests, bool holds) {
    return {.terms = {GuardBranch{.tests = std::move(tests), .holds = holds}}, .unstructured = false};
}

ScriptWarp guardedWarp(WarpGuard guard) {
    return {.map = "a",
            .via = WarpVia::Step,
            .section = 51,
            .targetMap = "b",
            .targetExit = 0,
            .scriptGuard = {},
            .guard = std::move(guard),
            .setsFlags = {},
            .source = "test"};
}

bool mentions(const std::vector<std::string>& allowed, std::string_view text) {
    return std::ranges::any_of(allowed, [text](const std::string& reason) { return reason.contains(text); });
}

} // namespace

TEST_CASE("field script warps read every warp with its guards") {
    const auto warps = parseScriptWarps(R"(warps:
- map: room08
  via: step
  section: 51
  to:
    map: twna01
    exit: 2
  guard:
    terms:
    - if:
      - test: trigger
        clear: true
        trigger: 203
      - test: pstat
        compare: le
        pstat: 110
        value: 24
        combine: and
      holds: true
    - selection: 1
  sets_flags:
  - trigger: 12
    set: true
  source:
    script: 178
    section: 51
    offset: 288
- map: yaka22
  via: other
  section: 7
  teleport:
    pstat: 31
  script_guard:
    unstructured: true
    terms: []
  source:
    script: 90
    section: 7
)");
    REQUIRE_MESSAGE(warps.has_value(), errorOf(warps));
    REQUIRE(warps->size() == 2);
    const auto& door = (*warps)[0];
    CHECK(door.map == "room08");
    CHECK(door.via == WarpVia::Step);
    CHECK(door.section == 51);
    CHECK(door.targetMap == "twna01");
    CHECK(door.targetExit == 2);
    REQUIRE(door.guard.terms.size() == 2);
    const auto& branch = std::get<GuardBranch>(door.guard.terms[0]);
    CHECK(branch.tests[0] == trigger(203, true));
    CHECK(branch.tests[1].test == "pstat");
    CHECK(branch.tests[1].combine == TestCombine::And);
    CHECK(std::get<GuardChoice>(door.guard.terms[1]) == GuardChoice{.arm = 1, .selection = true});
    CHECK(door.setsFlags.size() == 1);
    CHECK(door.source == "script 178, section 51, offset 0x0120");
    CHECK_FALSE((*warps)[1].targetMap.has_value());
    CHECK((*warps)[1].scriptGuard.unstructured);

    CHECK_FALSE(parseScriptWarps("warps:\n- map: a\n  via: walk\n  section: 51\n  source: {script: 1, section: 51}\n")
                    .has_value());
    CHECK_FALSE(parseScriptWarps("warps: []\nscripted_warps: []\n").has_value());
}

TEST_CASE("a guard is decided only by the triggers it tests, folded as the interpreter folds an IF") {
    CHECK(evaluate(WarpGuard{}, knownTriggers) == Truth::True);
    CHECK(evaluate(ifTests({trigger(5, false)}, true), knownTriggers) == Truth::True);
    CHECK(evaluate(ifTests({trigger(5, false)}, false), knownTriggers) == Truth::False);
    CHECK(evaluate(ifTests({trigger(6, true)}, true), knownTriggers) == Truth::True);
    CHECK(evaluate(ifTests({trigger(5, false), trigger(6, false, TestCombine::And)}, true), knownTriggers) ==
          Truth::False);
    CHECK(evaluate(ifTests({trigger(6, false), trigger(5, false, TestCombine::Or)}, true), knownTriggers) ==
          Truth::True);
    // An unknown trigger or another test leaves it undecided, unless the fold does not need it.
    CHECK(evaluate(ifTests({trigger(7, false)}, true), knownTriggers) == Truth::Unknown);
    CHECK(evaluate(ifTests({trigger(6, false), trigger(7, false, TestCombine::And)}, true), knownTriggers) ==
          Truth::False);
    CHECK(evaluate(
              ifTests({GuardTest{.test = "item", .combine = TestCombine::Replace, .trigger = 0, .clear = false}}, true),
              knownTriggers) == Truth::Unknown);
    CHECK(evaluate(ifTests({trigger(5, false)}, true), TriggerState{}) == Truth::Unknown);
    // A term that fails decides the guard; a choice or a loop never does on its own.
    WarpGuard mixed = ifTests({trigger(5, false)}, false);
    mixed.terms.emplace_back(GuardChoice{.arm = 0, .selection = false});
    CHECK(evaluate(mixed, knownTriggers) == Truth::False);
    WarpGuard looped = ifTests({trigger(5, false)}, true);
    looped.unstructured = true;
    CHECK(evaluate(looped, knownTriggers) == Truth::Unknown);

    dwe::game::field::ScriptWarp teleport{.map = "a",
                                          .via = WarpVia::Step,
                                          .section = 51,
                                          .targetMap = std::nullopt,
                                          .targetExit = 0,
                                          .scriptGuard = {},
                                          .guard = {},
                                          .setsFlags = {},
                                          .source = "test"};
    CHECK(evaluate(teleport, knownTriggers) == Truth::Unknown);
    teleport.targetMap = "b";
    CHECK(evaluate(teleport, knownTriggers) == Truth::True);
    teleport.scriptGuard = ifTests({trigger(5, false)}, false);
    CHECK(evaluate(teleport, knownTriggers) == Truth::False);
    CHECK(dwe::game::field::warpViaOf(79) == WarpVia::Step);
    CHECK(dwe::game::field::warpViaOf(109) == WarpVia::Confirm);
    CHECK(dwe::game::field::warpViaOf(110) == WarpVia::Other);
}

TEST_CASE("field script warps read a recursion term as the routine called again") {
    const auto warps = parseScriptWarps(R"(warps:
- map: room08
  via: step
  section: 51
  to:
    map: twna01
    exit: 2
  guard:
    terms:
    - recursion:
        script: 63
        offset: 288
    - recursion:
        script: 64
  source:
    script: 178
    section: 51
)");
    REQUIRE_MESSAGE(warps.has_value(), errorOf(warps));
    REQUIRE(warps->size() == 1);
    const auto& terms = (*warps)[0].guard.terms;
    REQUIRE(terms.size() == 2);
    CHECK(std::get<GuardRecursion>(terms[0]).site == "script 63, offset 0x0120");
    CHECK(std::get<GuardRecursion>(terms[1]).site == "script 64");
    CHECK_FALSE(parseScriptWarps("warps:\n- map: a\n  via: step\n  section: 51\n  guard:\n    terms:\n    - recursion: "
                                 "{offset: 1}\n  source: {script: 1, section: 51}\n")
                    .has_value());
}

TEST_CASE("under the vanilla policy a recursion the walk cut is taken and named, under Deny it is not") {
    const ScriptWarp warp =
        guardedWarp({.terms = {GuardRecursion{.site = "script 63, offset 0x0120"}}, .unstructured = false});
    const auto allowed = decide(warp, knownTriggers, UnknownPolicy::Allow);
    CHECK(allowed.truth == Truth::True);
    CHECK(mentions(allowed.allowed, "script 63, offset 0x0120"));
    const auto denied = decide(warp, knownTriggers, UnknownPolicy::Deny);
    CHECK(denied.truth == Truth::Unknown);
    CHECK(denied.allowed.empty());
    CHECK(evaluate(warp, knownTriggers) == Truth::Unknown);
    static_assert(dwe::game::field::VanillaUnknownPolicy == UnknownPolicy::Allow);

    // A term that fails still decides the guard, and nothing is reported for a warp that is not taken.
    WarpGuard failing = ifTests({trigger(5, false)}, false);
    failing.terms.emplace_back(GuardRecursion{.site = "script 63"});
    const auto notTaken = decide(guardedWarp(failing), knownTriggers, UnknownPolicy::Allow);
    CHECK(notTaken.truth == Truth::False);
    CHECK(notTaken.allowed.empty());
}

TEST_CASE("under the vanilla policy a test of a trigger the state does not know holds whichever way it goes") {
    for (const bool holds : {true, false}) {
        CAPTURE(holds);
        const auto decision =
            decide(guardedWarp(ifTests({trigger(7, false)}, holds)), knownTriggers, UnknownPolicy::Allow);
        CHECK(decision.truth == Truth::True);
        CHECK(mentions(decision.allowed, "trigger 7"));
        CHECK(decide(guardedWarp(ifTests({trigger(7, false)}, holds)), knownTriggers, UnknownPolicy::Deny).truth ==
              Truth::Unknown);
    }
    // Only the unknown trigger is named; a known one that decides the fold still decides it.
    const auto mixed = decide(guardedWarp(ifTests({trigger(5, false), trigger(7, false, TestCombine::And)}, true)),
                              knownTriggers, UnknownPolicy::Allow);
    CHECK(mixed.truth == Truth::True);
    CHECK(mentions(mixed.allowed, "trigger 7"));
    CHECK_FALSE(mentions(mixed.allowed, "trigger 5"));
    CHECK(decide(guardedWarp(ifTests({trigger(6, false), trigger(7, false, TestCombine::And)}, true)), knownTriggers,
                 UnknownPolicy::Allow)
              .truth == Truth::False);
}

TEST_CASE("the vanilla policy never takes a warp behind another test, a choice or a loop") {
    const GuardTest item{.test = "item", .combine = TestCombine::Replace, .trigger = 0, .clear = false};
    CHECK(decide(guardedWarp(ifTests({item}, true)), knownTriggers, UnknownPolicy::Allow).truth == Truth::Unknown);
    CHECK(decide(guardedWarp(ifTests({trigger(7, false), item}, true)), knownTriggers, UnknownPolicy::Allow).truth ==
          Truth::Unknown);
    CHECK(decide(guardedWarp({.terms = {GuardChoice{.arm = 1, .selection = true}}, .unstructured = false}),
                 knownTriggers, UnknownPolicy::Allow)
              .truth == Truth::Unknown);
    WarpGuard looped{.terms = {GuardRecursion{.site = "script 63"}}, .unstructured = true};
    CHECK(decide(guardedWarp(looped), knownTriggers, UnknownPolicy::Allow).truth == Truth::Unknown);
    // Without a trigger state nothing about triggers is known, and nothing is allowed for them.
    CHECK(decide(guardedWarp(ifTests({trigger(7, false)}, true)), TriggerState{}, UnknownPolicy::Allow).truth ==
          Truth::Unknown);
}

TEST_CASE("a new game's triggers are those its story sets; the rest are clear only when its paths agree") {
    const dwe::assets::NewGameStory agreed{
        .map = "a", .script = 1, .triggersSet = {3, 9}, .agreed = true, .triggersUnknown = {}, .opening = {}};
    const TriggerState known = newGameTriggers(agreed, "a", 1);
    CHECK(known(3) == std::optional{true});
    CHECK(known(9) == std::optional{true});
    CHECK(known(4) == std::optional{false});
    dwe::assets::NewGameStory parted = agreed;
    parted.agreed = false;
    const TriggerState shared = newGameTriggers(parted, "a", 1);
    CHECK(shared(3) == std::optional{true});
    CHECK_FALSE(shared(4).has_value());
    // A warp behind a trigger only some of the paths set is taken under the vanilla policy, and named.
    const auto decision = decide(guardedWarp(ifTests({trigger(4, true)}, true)), shared, UnknownPolicy::Allow);
    CHECK(decision.truth == Truth::True);
    CHECK(mentions(decision.allowed, "trigger 4"));
    CHECK(decide(guardedWarp(ifTests({trigger(4, true)}, true)), known, UnknownPolicy::Allow).truth == Truth::True);
    CHECK(decide(guardedWarp(ifTests({trigger(4, true)}, false)), known, UnknownPolicy::Allow).truth == Truth::False);
}

TEST_CASE("the triggers the game code may set before a new game are not known either way") {
    const auto story =
        dwe::assets::parseNewGameStory("map: a\nscript: 1\ntriggers_set: [9, 3]\ntriggers_unknown: [529, 515]\n");
    REQUIRE(story.has_value());
    CHECK(story->triggersSet == std::vector<std::int32_t>{3, 9});
    CHECK(story->triggersUnknown == std::vector<std::int32_t>{0x203, 0x211});
    const TriggerState known = newGameTriggers(*story, "a", 1);
    CHECK(known(3) == std::optional{true});
    CHECK(known(4) == std::optional{false});
    CHECK_FALSE(known(0x203).has_value());
    CHECK_FALSE(known(0x211).has_value());
    CHECK_FALSE(dwe::assets::parseNewGameStory("map: a\nscript: 1\ntriggers_set: []\ntriggers_unknown: [800]\n"));
}

TEST_CASE("a start that skips the new game's opening sets what the opening sets") {
    const auto story = dwe::assets::parseNewGameStory("map: town\nscript: 1\ntriggers_set: [3]\n"
                                                      "opening:\n  exit: 9\n  section: 54\n"
                                                      "  to: {map: opening, exit: 9}\n  sets: [75, 54, 55]\n");
    if (!story || !story->opening) {
        FAIL("the story or its opening does not read");
        return;
    }
    const dwe::assets::NewGameOpening& opening = *story->opening;
    CHECK(opening.exit == 9);
    CHECK(opening.section == 54);
    CHECK(opening.toMap == std::optional<std::string>{"opening"});
    CHECK_FALSE(opening.toScreen.has_value());
    CHECK(opening.toExit == 9);
    CHECK(opening.sets == std::vector<std::int32_t>{54, 55, 75});
    // Started elsewhere, the opening never ran: the state is the one it leaves.
    for (const auto& [map, exit] : {std::pair{"town", 1}, std::pair{"field", 9}}) {
        CAPTURE(map);
        CAPTURE(exit);
        const TriggerState skipped = newGameTriggers(*story, map, exit);
        CHECK(skipped(3) == std::optional{true});
        CHECK(skipped(54) == std::optional{true});
        CHECK(skipped(55) == std::optional{true});
        CHECK(skipped(75) == std::optional{true});
        CHECK(skipped(4) == std::optional{false});
    }
    // Started on the opening's spawn, the opening is still ahead.
    const TriggerState ahead = newGameTriggers(*story, "town", 9);
    CHECK(ahead(3) == std::optional{true});
    CHECK(ahead(54) == std::optional{false});
    CHECK(ahead(75) == std::optional{false});

    CHECK_FALSE(dwe::assets::parseNewGameStory("map: town\nscript: 1\ntriggers_set: []\nopening:\n  exit: 9\n"
                                               "  section: 54\n  to: {exit: 9}\n  sets: []\n"));
    CHECK_FALSE(dwe::assets::parseNewGameStory("map: town\nscript: 1\ntriggers_set: []\nopening:\n  exit: 9\n"
                                               "  section: 54\n  to: {map: a, screen: 3, exit: 9}\n  sets: []\n"));
    CHECK_FALSE(dwe::assets::parseNewGameStory("map: town\nscript: 1\ntriggers_set: []\nopening:\n  exit: 9\n"
                                               "  section: 54\n  to: {map: a, exit: 9}\n  sets: [800]\n"));
}

TEST_CASE("after a recursion the walk cut, a test of a trigger the state knows is unknown, for either polarity") {
    for (const bool holds : {true, false}) {
        CAPTURE(holds);
        WarpGuard guard{.terms = {GuardRecursion{.site = "script 63"}}, .unstructured = false};
        guard.terms.emplace_back(GuardBranch{.tests = {trigger(5, false)}, .holds = holds});
        const auto allowed = decide(guardedWarp(guard), knownTriggers, UnknownPolicy::Allow);
        CHECK(allowed.truth == Truth::True);
        CHECK(mentions(allowed.allowed, "script 63"));
        CHECK(mentions(allowed.allowed, "trigger 5"));
        CHECK(decide(guardedWarp(guard), knownTriggers, UnknownPolicy::Deny).truth == Truth::Unknown);

        // A recursion on the script guard's path comes before the section's: its tests are unknown too.
        ScriptWarp loaded = guardedWarp(ifTests({trigger(5, false)}, holds));
        loaded.scriptGuard = {.terms = {GuardRecursion{.site = "script 64"}}, .unstructured = false};
        const auto section = decide(loaded, knownTriggers, UnknownPolicy::Allow);
        CHECK(section.truth == Truth::True);
        CHECK(mentions(section.allowed, "trigger 5"));
    }
    // Before the recursion the state still decides.
    WarpGuard failsFirst = ifTests({trigger(5, false)}, false);
    failsFirst.terms.emplace_back(GuardRecursion{.site = "script 63"});
    failsFirst.terms.emplace_back(GuardBranch{.tests = {trigger(5, false)}, .holds = false});
    CHECK(decide(guardedWarp(failsFirst), knownTriggers, UnknownPolicy::Allow).truth == Truth::False);
}

TEST_CASE("on a cell the field takes the first warp that is taken, and calls the cell ambiguous when the policy lets "
          "two through to different places") {
    ScriptWarp set = guardedWarp(ifTests({trigger(7, false)}, true));
    set.targetMap = "set";
    ScriptWarp clear = guardedWarp(ifTests({trigger(7, false)}, false));
    clear.targetMap = "clear";
    ScriptWarp elsewhere = guardedWarp({});
    elsewhere.section = 52;
    const std::vector<ScriptWarp> warps{elsewhere, set, clear};

    const auto allowed = warpOnCell(warps, "a", WarpVia::Step, 51, knownTriggers, UnknownPolicy::Allow);
    REQUIRE(allowed.warp != nullptr);
    CHECK(allowed.warp->targetMap == "set");
    CHECK(mentions(allowed.decision.allowed, "trigger 7"));
    CHECK_FALSE(allowed.undecided);
    CHECK(allowed.ambiguous == std::vector<const ScriptWarp*>{&warps[1], &warps[2]});

    const auto denied = warpOnCell(warps, "a", WarpVia::Step, 51, knownTriggers, UnknownPolicy::Deny);
    CHECK(denied.warp == nullptr);
    CHECK(denied.undecided);
    CHECK(warpOnCell(warps, "a", WarpVia::Confirm, 51, knownTriggers, UnknownPolicy::Allow).warp == nullptr);
    CHECK(warpOnCell(warps, "b", WarpVia::Step, 52, knownTriggers, UnknownPolicy::Allow).warp == nullptr);
    CHECK(warpOnCell(warps, "a", WarpVia::Step, 52, knownTriggers, UnknownPolicy::Allow).warp == warps.data());
}

TEST_CASE("on a cell where the policy lets two warps through, the one taken without the policy is taken") {
    ScriptWarp unknown = guardedWarp(ifTests({trigger(7, false)}, true));
    unknown.targetMap = "unknown";
    ScriptWarp known = guardedWarp(ifTests({trigger(5, false)}, true));
    known.targetMap = "known";
    const std::vector<ScriptWarp> warps{unknown, known};
    const auto cell = warpOnCell(warps, "a", WarpVia::Step, 51, knownTriggers, UnknownPolicy::Allow);
    REQUIRE(cell.warp != nullptr);
    CHECK(cell.warp->targetMap == "known");
    CHECK(cell.decision.allowed.empty());
    CHECK(cell.ambiguous.empty());
    // Two warps to the same place are not ambiguous, whichever is taken.
    ScriptWarp alike = guardedWarp(ifTests({trigger(7, false)}, false));
    const std::vector<ScriptWarp> onePlace{alike, guardedWarp(ifTests({trigger(7, false)}, true))};
    CHECK(warpOnCell(onePlace, "a", WarpVia::Step, 51, knownTriggers, UnknownPolicy::Allow).ambiguous.empty());
}

TEST_CASE("field script warps read a forget term as the site of game code and the triggers it may write") {
    const auto warps = parseScriptWarps(R"(warps:
- map: a
  via: step
  section: 51
  to:
    map: b
    exit: 0
  guard:
    terms:
    - forget:
        script: 206
        offset: 1694
        triggers:
        - 3
    - forget:
        script: 7
  source:
    script: 206
    section: 51
)");
    REQUIRE_MESSAGE(warps.has_value(), errorOf(warps));
    REQUIRE(warps->size() == 1);
    const auto& terms = (*warps)[0].guard.terms;
    REQUIRE(terms.size() == 2);
    CHECK(std::get<GuardForget>(terms[0]) ==
          GuardForget{.site = "script 206, offset 0x069e", .triggers = std::vector<std::int32_t>{3}});
    CHECK(std::get<GuardForget>(terms[1]) == GuardForget{.site = "script 7", .triggers = {}});
    CHECK_FALSE(parseScriptWarps("warps:\n- map: a\n  via: step\n  section: 51\n  guard:\n    terms:\n    - forget: "
                                 "{script: 1, trigger: 3}\n  source: {script: 1, section: 51}\n")
                    .has_value());
}

TEST_CASE("after game code that may write a trigger, a test of it is unknown and a test of another is decided") {
    const GuardForget writesFive{.site = "script 206, offset 0x069e", .triggers = {5}};
    for (const bool holds : {true, false}) {
        CAPTURE(holds);
        WarpGuard guard{.terms = {writesFive}, .unstructured = false};
        guard.terms.emplace_back(GuardBranch{.tests = {trigger(5, false)}, .holds = holds});
        const auto allowed = decide(guardedWarp(guard), knownTriggers, UnknownPolicy::Allow);
        CHECK(allowed.truth == Truth::True);
        CHECK(mentions(allowed.allowed, "trigger 5"));
        CHECK(mentions(allowed.allowed, "script 206, offset 0x069e"));
        CHECK(decide(guardedWarp(guard), knownTriggers, UnknownPolicy::Deny).truth == Truth::Unknown);
        // On the script guard's path it comes before the section's tests too.
        ScriptWarp loaded = guardedWarp(ifTests({trigger(5, false)}, holds));
        loaded.scriptGuard = {.terms = {writesFive}, .unstructured = false};
        CHECK(decide(loaded, knownTriggers, UnknownPolicy::Deny).truth == Truth::Unknown);
    }
    // Trigger 6 is not written: the state decides its test, and nothing is allowed.
    WarpGuard other{.terms = {writesFive}, .unstructured = false};
    other.terms.emplace_back(GuardBranch{.tests = {trigger(6, true)}, .holds = true});
    const auto decided = decide(guardedWarp(other), knownTriggers, UnknownPolicy::Allow);
    CHECK(decided.truth == Truth::True);
    CHECK(decided.allowed.empty());
    CHECK(evaluate(guardedWarp(other), knownTriggers) == Truth::True);
    // Game code that may write any trigger leaves trigger 6 unknown too.
    WarpGuard any{.terms = {GuardForget{.site = "script 7", .triggers = {}}}, .unstructured = false};
    any.terms.emplace_back(GuardBranch{.tests = {trigger(6, true)}, .holds = true});
    CHECK(evaluate(guardedWarp(any), knownTriggers) == Truth::Unknown);
    // A test before the game code still reads the state.
    WarpGuard before = ifTests({trigger(5, false)}, false);
    before.terms.emplace_back(writesFive);
    CHECK(decide(guardedWarp(before), knownTriggers, UnknownPolicy::Allow).truth == Truth::False);
}

TEST_CASE("field script warps read the operands of item count, money, stat and condition tests") {
    const auto warps = parseScriptWarps(R"(warps:
- map: a
  via: step
  section: 51
  to: {map: b, exit: 0}
  guard:
    terms:
    - if:
      - {test: item_count, item: 115, compare: ge, value: 1}
      - {test: money, compare: lt, value: 400, combine: and}
      - {test: stat, stat: 8, compare: eq, value: 3, combine: or}
      - {test: condition, mask: 64, negate: 1, combine: and}
      - {test: pstat, pstat: 3, compare: eq, value: 1, combine: and}
      holds: true
  source: {script: 1, section: 51}
)");
    REQUIRE_MESSAGE(warps.has_value(), errorOf(warps));
    const auto& tests = std::get<GuardBranch>(warps->front().guard.terms.front()).tests;
    REQUIRE(tests.size() == 5);
    CHECK(tests[0].operand == 115);
    CHECK(tests[0].compare == Comparison::AtLeast);
    CHECK(tests[0].value == 1);
    CHECK(tests[1].compare == Comparison::Less);
    CHECK(tests[1].value == 400);
    CHECK(tests[1].combine == TestCombine::And);
    CHECK(tests[2].operand == 8);
    CHECK(tests[2].combine == TestCombine::Or);
    CHECK(tests[3].operand == 64);
    CHECK(tests[3].negate);
    CHECK(tests[4].test == "pstat");

    const auto badCompare = parseScriptWarps(R"(warps:
- map: a
  via: step
  section: 51
  guard: {terms: [{if: [{test: money, compare: about, value: 1}], holds: true}]}
  source: {script: 1, section: 51}
)");
    CHECK_FALSE(badCompare.has_value());
}

TEST_CASE("a guard reads the bag, the money and the partner as the original compares them") {
    GuardFacts facts{knownTriggers};
    facts.itemCount = [](std::int32_t item) -> std::optional<std::int64_t> {
        return item == 115 ? std::optional<std::int64_t>{2} : std::optional<std::int64_t>{0};
    };
    facts.money = []() -> std::optional<std::int64_t> { return 350; };
    facts.stat = [](std::int32_t stat) -> std::optional<std::int64_t> {
        if (stat == 8) {
            return 3;
        }
        return stat == SignedScriptStat ? std::optional<std::int64_t>{-5} : std::nullopt;
    };
    facts.conditions = []() -> std::optional<std::uint32_t> { return 0x20; };
    const auto test = [](std::string_view name, std::int32_t operand, Comparison compare, std::int64_t value) {
        return GuardTest{.test = std::string{name},
                         .combine = TestCombine::Replace,
                         .trigger = 0,
                         .clear = false,
                         .operand = operand,
                         .compare = compare,
                         .value = value,
                         .negate = false};
    };
    CHECK(evaluate(ifTests({test("item_count", 115, Comparison::AtLeast, 1)}, true), facts) == Truth::True);
    CHECK(evaluate(ifTests({test("item_count", 116, Comparison::AtLeast, 1)}, true), facts) == Truth::False);
    CHECK(evaluate(ifTests({test("money", 0, Comparison::Less, 400)}, true), facts) == Truth::True);
    // Money compares unsigned: a value the listing writes negative is a huge one.
    CHECK(evaluate(ifTests({test("money", 0, Comparison::Less, -1)}, true), facts) == Truth::True);
    CHECK(evaluate(ifTests({test("stat", 8, Comparison::Equal, 3)}, true), facts) == Truth::True);
    // Stat 9 compares signed, the others as unsigned 16-bit values.
    CHECK(evaluate(ifTests({test("stat", SignedScriptStat, Comparison::Less, 0)}, true), facts) == Truth::True);
    CHECK(evaluate(ifTests({test("stat", SignedScriptStat, Comparison::Less, 0xFFF0)}, true), facts) == Truth::False);
    // A stat the reader does not give is not known.
    CHECK(evaluate(ifTests({test("stat", 6, Comparison::Equal, 1)}, true), facts) == Truth::Unknown);

    GuardTest injured = test("condition", 0x20, Comparison::Equal, 0);
    CHECK(evaluate(ifTests({injured}, true), facts) == Truth::True);
    injured.negate = true;
    CHECK(evaluate(ifTests({injured}, true), facts) == Truth::False);
    GuardTest sick = test("condition", 0x40, Comparison::Equal, 0);
    CHECK(evaluate(ifTests({sick}, false), facts) == Truth::True);

    // Folded with a trigger test as the interpreter folds an IF.
    GuardTest moneyAnd = test("money", 0, Comparison::AtLeast, 1000);
    moneyAnd.combine = TestCombine::And;
    CHECK(evaluate(ifTests({trigger(5, false), moneyAnd}, true), facts) == Truth::False);

    // Without readers they are never decided, and no policy lets them through.
    const ScriptWarp warp = guardedWarp(ifTests({test("money", 0, Comparison::Less, 400)}, true));
    CHECK(decide(warp, knownTriggers, UnknownPolicy::Allow).truth == Truth::Unknown);
    CHECK(decide(warp, facts, UnknownPolicy::Allow).truth == Truth::True);
}
