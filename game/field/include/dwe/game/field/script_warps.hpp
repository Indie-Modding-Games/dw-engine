#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
struct NewGameStory;
} // namespace dwe::assets

// The map changes the event scripts of each map make (`field/script_warps`, which dwx extracts;
// content/schemas/script_warps.schema.json, docs/architecture/warps-de-script.md), each with the decisions its path
// takes. The field takes the ones it can decide: those whose decisions are all about triggers.
namespace dwe::game::field {

// What runs the section (tamer.c: checkMapInteraction): stepping on a cell of value 51-79, pressing confirm on one
// of 80-109, entering the map, talking to an NPC of the map whose script id is the section (Talk; the field has no
// NPCs yet), or anything else (another section).
enum class WarpVia : std::uint8_t { Step, Confirm, Enter, Other, Talk };

// What runs the section a cell of this collision value runs: Step, Confirm, or Other for a value that runs none.
WarpVia warpViaOf(std::int32_t value);

// How an IF entry joins the result of the ones before it (script_interp.c: scriptIfInstruction).
enum class TestCombine : std::uint8_t { Replace, And, Or };

// How a comparison test compares what it reads with its value (script_interp.c: scriptCompareValue).
enum class Comparison : std::uint8_t { Equal, NotEqual, AtLeast, AtMost, Greater, Less };

// The IF entries the field reads besides triggers (script_value.c: scriptCompareItemCount, scriptCompareMoney,
// scriptCompareStat, scriptCompareCondition). Every other test is kept by its name and never decided.
inline constexpr std::string_view TriggerTestName = "trigger";
inline constexpr std::string_view ItemCountTestName = "item_count";
inline constexpr std::string_view MoneyTestName = "money";
inline constexpr std::string_view StatTestName = "stat";
inline constexpr std::string_view ConditionTestName = "condition";

// The script's stat whose value the original compares as signed (script_value.c: scriptCompareStat, the happiness).
inline constexpr std::int32_t SignedScriptStat = 9;

// An IF entry. The field reads trigger tests, the bag's item counts, the money and the partner's stats and
// conditions; any other test is kept by its name and is never decided.
struct GuardTest {
    std::string test; // the listing's name: "trigger", "pstat", "item_count"...
    TestCombine combine = TestCombine::Replace;
    std::int32_t trigger = 0; // a trigger test: the trigger
    bool clear = false;       // a trigger test: whether it asks for the trigger to be clear
    // An item count, money or stat test: what it reads (the item, the stat; nothing for money), how it compares and
    // with what. A condition test: the mask of conditions in `operand` and `negate`.
    std::int32_t operand = 0;
    Comparison compare = Comparison::Equal;
    std::int64_t value = 0;
    bool negate = false; // a condition test: whether it asks for none of the mask's conditions

    bool operator==(const GuardTest&) const = default;
};

// An IF the path goes through: its entries, folded left to right, must give `holds`.
struct GuardBranch {
    std::vector<GuardTest> tests;
    bool holds = true;

    bool operator==(const GuardBranch&) const = default;
};

// An arm of a SWITCH or of a SET_SELECTION (the player picks it) the path goes through.
struct GuardChoice {
    std::int32_t arm = 0;
    bool selection = false;

    bool operator==(const GuardChoice&) const = default;
};

// A call of a routine the extraction's walk was already in: it did not follow the recursion, so what it decides is not
// known (docs/architecture/warps-de-script.md).
struct GuardRecursion {
    std::string site; // "script 63, offset 0x0120": the routine called again

    bool operator==(const GuardRecursion&) const = default;
};

// An instruction on the path that runs game code which may set or clear triggers (a routine of the game, give_item,
// compare_date): from it on, the path's trigger tests do not read what the state knew of them.
struct GuardForget {
    std::string site;                   // "script 206, offset 0x069e"
    std::vector<std::int32_t> triggers; // the triggers it may write; empty: any

    bool operator==(const GuardForget&) const = default;
};

struct WarpGuard {
    std::vector<std::variant<GuardBranch, GuardChoice, GuardRecursion, GuardForget>> terms;
    // The path goes round a loop or a state machine: the terms are those of one way through.
    bool unstructured = false;

    bool operator==(const WarpGuard&) const = default;
};

// A SET_TRIGGER (set) or UNSET_TRIGGER on the way to the map change.
struct FlagEffect {
    std::int32_t trigger = 0;
    bool set = true;

    bool operator==(const FlagEffect&) const = default;
};

// A pick of the transport menu (shop/script_menu.c: tickBirdraTransport): the entry of the executable's table and
// what it costs. The warp's guard ends with the entry's trigger, the money it costs and the pick; the menu hands the
// script over to MAPHEAD's section `handOff`, which teleports to the entry's map.
struct TransportPick {
    std::int32_t entry = 0;
    std::int64_t cost = 0;
    std::int32_t handOff = 0;

    bool operator==(const TransportPick&) const = default;
};

struct ScriptWarp {
    std::string map; // the map whose script runs it
    WarpVia via = WarpVia::Other;
    std::int32_t section = 0;
    // Where it goes; nothing for a teleport (the map is in a variable) or a screen no map has.
    std::optional<std::string> targetMap;
    std::int32_t targetExit = 0;
    WarpGuard scriptGuard; // what loads the map's script
    WarpGuard guard;       // what the section takes to get to the map change
    std::vector<FlagEffect> setsFlags;
    std::optional<TransportPick> transport{};
    std::string source; // "script 178, section 51, offset 0x0120"

    bool operator==(const ScriptWarp&) const = default;
};

// A decision the field can make (True, False) or cannot (Unknown: a test of something it does not track, a choice,
// an unstructured path).
enum class Truth : std::uint8_t { False, True, Unknown };

// Whether a trigger is set; nothing when it is not known.
using TriggerState = std::function<std::optional<bool>(std::int32_t trigger)>;

// What the guards read of the rest of the game besides the triggers. A reader that is empty, or gives nothing, leaves
// its tests Unknown.
using ItemCountReader = std::function<std::optional<std::int64_t>(std::int32_t item)>; // the bag's count of an item
using MoneyReader = std::function<std::optional<std::int64_t>()>;
// A stat by the script's number (script.h: SCRIPT_STAT_*): the partner's stats, needs and lifetime, the merit...
using StatReader = std::function<std::optional<std::int64_t>(std::int32_t stat)>;
// The partner's conditions as the original's bits (params.h: CONDITION_*).
using ConditionReader = std::function<std::optional<std::uint32_t>()>;

// Sets or clears a story trigger: what a script warp the field takes sets on its way (sets_flags).
using TriggerWriter = std::function<void(std::int32_t trigger, bool set)>;

// Everything a guard reads, live: the field asks when it decides.
struct GuardFacts {
    GuardFacts() = default;
    // Facts that know only the triggers.
    GuardFacts(TriggerState triggerState) : triggers(std::move(triggerState)) {}

    TriggerState triggers;
    ItemCountReader itemCount;
    MoneyReader money;
    StatReader stat;
    ConditionReader conditions;
};

// The decision of a guard, in three values: every term must hold. A recursion, a test of a trigger the state does
// not know, or that game code before it on the path may have written, and a test whose reader gives nothing are
// Unknown.
Truth evaluate(const WarpGuard& guard, const GuardFacts& facts);

// Whether a warp is taken now: its script is loaded and its section gets to it. A warp without a target is never
// True.
Truth evaluate(const ScriptWarp& warp, const GuardFacts& facts);

// What the field does with the things it is told it cannot know: a recursion the extraction's walk cut, a trigger
// the game code on the path may have written (GuardForget), and a trigger the new game's paths do not agree on. Every
// other Unknown (a test of something else than a trigger, a choice, an unstructured path) is never taken, whatever the
// policy.
enum class UnknownPolicy : std::uint8_t {
    Allow, // the term holds: an IF whose tests read an unknown trigger holds whichever way it goes
    Deny,  // the term is Unknown, and the warp is not taken
};

// The policy of the original's game: a warp is not lost for what the extraction cannot know; the field logs it.
inline constexpr UnknownPolicy VanillaUnknownPolicy = UnknownPolicy::Allow;

// Whether a warp is taken under a policy, and what the policy let through to take it (for the log).
struct WarpDecision {
    Truth truth = Truth::Unknown;
    std::vector<std::string> allowed;
};

WarpDecision decide(const ScriptWarp& warp, const GuardFacts& facts, UnknownPolicy policy);

// The warp the field takes on a map's cell: of that map, way and section, the warps whose decision is True. Under
// Allow two paths that part at an unknown trigger can both be True, while the original runs the section, which takes
// only one of them: when exactly one of them is True without the policy (its decision allowed nothing), the field
// takes it; otherwise it takes the first, in the warps' order, and if they go to more than one place the cell is
// ambiguous.
struct CellWarp {
    const ScriptWarp* warp = nullptr; // nothing when none is taken
    WarpDecision decision;
    bool undecided = false; // a warp of the cell before the first True one (or of the whole cell) was Unknown
    // The True warps, in order, when they go to more than one place and the field took the first.
    std::vector<const ScriptWarp*> ambiguous;
};

CellWarp warpOnCell(std::span<const ScriptWarp> warps, std::string_view map, WarpVia via, std::int32_t section,
                    const GuardFacts& facts, UnknownPolicy policy);

// Whether a map has data the field can enter; an empty one lets every map through.
using MapFilter = std::function<bool(std::string_view map)>;

// Free exploration (an option, off by default; docs/architecture/warps-de-script.md): the places the scripts' map
// changes on a map's cell go to, whatever their guards decide: of that map, way and section, the warps with a target
// that `hasMap` lets through, the first of each place (map and exit) in the warps' order.
std::vector<const ScriptWarp*> explorationWarps(std::span<const ScriptWarp> warps, std::string_view map, WarpVia via,
                                                std::int32_t section, const MapFilter& hasMap);

// The triggers as a new game leaves them: set those the story lists and clear the rest; when its paths do not agree,
// the rest is unknown. A field that starts on `startMap` at `startExit` anywhere but the opening's spawn (the story's
// map at the opening's exit) skips the opening, and sets the triggers the opening would have set too.
TriggerState newGameTriggers(const assets::NewGameStory& story, std::string_view startMap, std::int32_t startExit);

// Parses the text of `field/script_warps`. Unknown and missing keys are errors; the keys of a test other than a
// trigger test are not read.
core::Result<std::vector<ScriptWarp>> parseScriptWarps(std::string_view yaml);

// Reads `field/script_warps` from the virtual file system; nothing when no pack has it.
core::Result<std::vector<ScriptWarp>> loadScriptWarps(const assets::VirtualFileSystem& vfs);

} // namespace dwe::game::field
