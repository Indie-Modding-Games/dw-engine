#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/guarded_walk.hpp>
#include <dwe/formats/script/map_changes.hpp>
#include <dwe/formats/script/scn_file.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/script_warps.hpp>
#include <dwe/formats/script/structuring.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// The proof that summarizing routines (WalkOptions::maximumRoutineWalks) finds what walking them in
// full finds, on the user's disc. Opt-in, and not part of the gate: the full walk of some scripts
// does not finish. Set DWE_WALK_PROOF to the scripts to compare ("all", or numbers and ranges:
// "165,166-221"), so that the proof runs in chunks; DWE_WALK_PROOF_LONG to give each script the
// long budget. A script whose full walk does not finish within the budget is proven by the
// fallback: the walks that summarize later (FallbackRoutineWalks) give what the summarized walk
// gives. docs/architecture/warps-de-script.md has the results. The last test, that the walk the
// extraction makes ends within a budget on every script, is part of the gate.

namespace disc = dwe::formats::disc;
namespace script = dwe::formats::script;

namespace {

constexpr std::string_view ProofVariable = "DWE_WALK_PROOF";
constexpr std::string_view LongVariable = "DWE_WALK_PROOF_LONG";
constexpr std::string_view AllScripts = "all";
// How long the walk compared with may take for one script, all its sections.
constexpr std::chrono::minutes Budget{10};
constexpr std::chrono::minutes LongBudget{60};
// The bound on a statement's paths that the merge check walks with: far above DefaultMaximumPaths,
// so that what merging at the default loses shows against it.
constexpr std::size_t LargeMaximumPaths = 4096;
// A script the merge check cannot compare: its walk with LargeMaximumPaths still merges or does not
// end within the budget. Each one says why; none is known yet (the proof measures it).
struct MergeException {
  std::uint16_t script = 0;
  std::string_view reason;
};
constexpr std::array<MergeException, 0> MergeExceptions{};
// A script whose walk with LargeMaximumPaths still merges: a precision note, not a failure. The
// merge check still compares it (the same map changes, no structured guard made up) and only does
// not ask the larger walk to end without merging; a script listed here that no longer merges fails,
// so the list stays current. Measured 2026-10-22 (merges with DefaultMaximumPaths, then with
// LargeMaximumPaths); none of their map changes is after a merge.
struct StillMerging {
  std::uint16_t script = 0;
  std::string_view reason;
};
constexpr std::array<StillMerging, 9> MergeStillMerges = {{
    {.script = 165,
     .reason = "150 merges, 130 with 4096 paths; the summarized walk is proven by fallback"},
    {.script = 168,
     .reason = "69 merges, 11 with 4096 paths; the summarized walk is proven by the full walk"},
    {.script = 176,
     .reason = "4 merges, 2 with 4096 paths; the summarized walk is proven by the full walk"},
    {.script = 199,
     .reason = "90 merges, 16 with 4096 paths; the summarized walk is proven by the full walk"},
    {.script = 200,
     .reason = "90 merges, 16 with 4096 paths; the summarized walk is proven by the full walk"},
    {.script = 201,
     .reason = "90 merges, 16 with 4096 paths; the summarized walk is proven by the full walk"},
    {.script = 202,
     .reason = "90 merges, 16 with 4096 paths; the summarized walk is proven by the full walk"},
    {.script = 211,
     .reason = "150 merges, 130 with 4096 paths; the summarized walk is proven by fallback"},
    {.script = 214,
     .reason = "69 merges, 11 with 4096 paths; the summarized walk is proven by the full walk"},
}};
// A map change the static check does not count: no path of the walk gets to it, and the flow from a
// section only gets to it by a side of a test that never runs. Each one says why.
struct StaticUnreachable {
  std::uint16_t script = 0;
  std::uint32_t offset = 0;
  std::string_view reason;
};
constexpr std::uint16_t DeadWarpsScript = 124;
constexpr std::uint16_t DeadWarpsTrigger = 221;
constexpr std::string_view DeadWarpsReason =
    "on the false side of a test of trigger 221, which the script sets at offset 0x56c on every path to it";
// The map changes on the true side of the tests of trigger 221 (at 0x59c and 0x5fc) the dead ones
// are under. The walk hands an IF to no filter (it walks it as a statement), so the check looks at
// what runs after it: every path that gets to these knows 221 set, so it took the true side, and
// none gets to the dead ones (the static check).
constexpr std::array<std::uint32_t, 6> LiveWarpsBesideDead = {0x5e4, 0x5f4, 0x5f8,
                                                              0x644, 0x654, 0x658};
constexpr std::array<StaticUnreachable, 6> StaticUnreachables = {{
    {.script = DeadWarpsScript, .offset = 0x5c0, .reason = DeadWarpsReason},
    {.script = DeadWarpsScript, .offset = 0x5d0, .reason = DeadWarpsReason},
    {.script = DeadWarpsScript, .offset = 0x5d4, .reason = DeadWarpsReason},
    {.script = DeadWarpsScript, .offset = 0x620, .reason = DeadWarpsReason},
    {.script = DeadWarpsScript, .offset = 0x630, .reason = DeadWarpsReason},
    {.script = DeadWarpsScript, .offset = 0x634, .reason = DeadWarpsReason},
}};
// The walks of a routine before it is summarized that the fallback of a script whose full walk does
// not finish compares: each must give what DefaultMaximumRoutineWalks gives.
constexpr std::array<std::size_t, 2> FallbackRoutineWalks = {256, 1024};
// How long the walk that the extraction makes may take for one script, all its sections, in the
// slowest build (the sanitized one). It takes a few seconds; the budget is what stops a walk that
// blows up, as one did when a recursion left a routine's summary unused
// (docs/architecture/warps-de-script.md).
constexpr std::chrono::minutes ExtractionBudget{2};
// The most guard terms one map change of the disc may have. The walk gives 16 (script 174,
// 2026-10-22); twice that leaves room for a walk that keeps a few more decisions, and still stops
// one whose guards grow with every call.
constexpr std::size_t MaximumGuardTerms = 32;
// The recursions the extraction's walk of the disc cuts, all scripts together (2026-10-22). The
// walk is deterministic, so a change means it walks the routines differently: report it before
// changing the number.
constexpr std::size_t ExpectedRecursionCuts = 2134;

std::optional<std::string> variable(std::string_view name) {
  const char* value = std::getenv(std::string{name}.c_str());
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string{value};
}

// "all", or numbers and ranges separated by commas.
std::set<std::uint16_t> scriptsIn(std::string_view text, std::size_t count) {
  std::set<std::uint16_t> scripts;
  if (text == AllScripts) {
    for (std::size_t id = 0; id < count; ++id) {
      scripts.insert(static_cast<std::uint16_t>(id));
    }
    return scripts;
  }
  while (!text.empty()) {
    const std::size_t comma = text.find(',');
    const std::string item{text.substr(0, comma)};
    const std::size_t dash = item.find('-');
    const auto first = static_cast<std::uint16_t>(std::stoul(item.substr(0, dash)));
    const auto last = dash == std::string::npos
                          ? first
                          : static_cast<std::uint16_t>(std::stoul(item.substr(dash + 1)));
    for (std::uint32_t id = first; id <= last; ++id) {
      scripts.insert(static_cast<std::uint16_t>(id));
    }
    text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
  }
  return scripts;
}

struct Walked {
  script::ScriptListing listing;
  script::StructuredScript structured;
};

// What identifies a map change, its guard aside.
using ChangeKey =
    std::tuple<std::uint16_t, std::size_t, std::optional<std::int32_t>, std::optional<std::int32_t>,
               std::optional<std::int32_t>, std::optional<std::int32_t>>;

ChangeKey keyOf(const script::MapChange& change) {
  return {change.path.place.script, change.path.place.instruction, change.screen, change.exit,
          change.arrivalSection,    change.screenVariable};
}

// The map changes of every section of a script the game can run.
std::vector<script::MapChange> changesOf(std::uint16_t id, const Walked& walked,
                                         const script::ScriptLookup& lookup,
                                         const script::WalkOptions& options) {
  std::vector<script::MapChange> changes;
  for (const script::ListingSection& section : walked.listing.runnableSections()) {
    auto found = script::findMapChanges(id, section.id, lookup, options);
    std::ranges::move(found, std::back_inserter(changes));
  }
  return changes;
}

// The instructions the flow of the script gets to from the given sections, whatever the tests give.
std::set<std::size_t> flowReached(const script::ScriptListing& listing,
                                  const std::vector<script::ListingSection>& sections) {
  std::set<std::size_t> seen;
  std::vector<std::size_t> pending;
  const auto push = [&](std::optional<std::size_t> index) {
    if (index && *index < listing.instructions.size() && seen.insert(*index).second) {
      pending.push_back(*index);
    }
  };
  for (const script::ListingSection& section : sections) {
    push(listing.indexAt(section.entry));
  }
  while (!pending.empty()) {
    const std::size_t index = pending.back();
    pending.pop_back();
    const script::ListingInstruction& instruction = listing.instructions[index];
    const std::optional<std::size_t> next = index + 1;
    switch (instruction.role) {
    case script::FlowRole::Jump:
    case script::FlowRole::Switch:
      for (const std::uint32_t target : instruction.targets) {
        push(listing.indexAt(target));
      }
      break;
    case script::FlowRole::Call:
    case script::FlowRole::Selection:
      for (const std::uint32_t target : instruction.targets) {
        push(listing.indexAt(target));
      }
      push(next);
      break;
    case script::FlowRole::If:
      for (const script::ConditionalJump& jump : instruction.conditionalJumps) {
        push(listing.indexAt(jump.target));
      }
      push(next);
      break;
    case script::FlowRole::Return:
    case script::FlowRole::FileJump:
    case script::FlowRole::MapChange:
    case script::FlowRole::EndSection:
    case script::FlowRole::EndScript:
    case script::FlowRole::Data:
      break;
    case script::FlowRole::Plain:
    case script::FlowRole::FileCall:
    case script::FlowRole::LoadScript:
      push(next);
      break;
    }
  }
  return seen;
}

// Every script of the disc decoded, listed and structured: MAPHEAD's and DG.SCN's. A REQUIRE stops
// the test case, so this fills `walked` instead of returning it.
void loadScripts(const std::filesystem::path& discPath, std::map<std::uint16_t, Walked>& walked) {
  auto image = disc::DiscImage::open(discPath);
  REQUIRE(image.has_value());
  auto volume = disc::readIsoVolume(*image);
  REQUIRE(volume.has_value());
  const auto read = [&](std::string_view path) {
    const auto found = std::ranges::find(volume->files, path, &disc::IsoFile::path);
    REQUIRE(found != volume->files.end());
    auto bytes = disc::readIsoFile(*image, *found);
    REQUIRE(bytes.has_value());
    return std::move(*bytes);
  };
  const auto mapHead = read("SCN/MAPHEAD.SCN");
  const auto pack = read("SCN/DG.SCN");
  const auto slots = script::readScriptTable(pack);
  REQUIRE(slots.has_value());
  const auto add = [&walked](std::uint16_t id, std::span<const std::byte> bytes) {
    const auto disassembly = script::disassembleScript(bytes);
    REQUIRE(disassembly.has_value());
    auto listing = script::listingOf(*disassembly);
    auto structured = script::structureScript(listing);
    REQUIRE(structured.has_value());
    walked.emplace(id, Walked{.listing = std::move(listing), .structured = std::move(*structured)});
  };
  add(script::MapHeadScript, mapHead);
  for (std::size_t id = 1; id < slots->size(); ++id) {
    add(static_cast<std::uint16_t>(id),
        std::span{pack}.subspan((*slots)[id].offset, (*slots)[id].size));
  }
}

script::ScriptLookup lookupOf(const std::map<std::uint16_t, Walked>& walked) {
  return [&walked](std::uint16_t id) -> std::optional<script::WalkableScript> {
    const auto found = walked.find(id);
    if (found == walked.end()) {
      return std::nullopt;
    }
    return script::WalkableScript{.listing = &found->second.listing,
                                  .structured = &found->second.structured};
  };
}

long long millisecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                               start)
      .count();
}

std::set<ChangeKey> keysOf(const std::vector<script::MapChange>& changes) {
  std::set<ChangeKey> keys;
  for (const script::MapChange& change : changes) {
    keys.insert(keyOf(change));
  }
  return keys;
}

std::string listOf(const std::vector<std::uint16_t>& scripts) {
  std::string list;
  for (const std::uint16_t id : scripts) {
    list += (list.empty() ? "" : " ") + std::to_string(id);
  }
  return list;
}

script::WalkOptions walkOptions(std::size_t routineWalks, script::WalkMonitor& monitor) {
  return {.knownTriggers = {},
          .othersClear = false,
          .maximumPaths = script::DefaultMaximumPaths,
          .maximumRoutineWalks = routineWalks,
          .monitor = &monitor};
}

const StaticUnreachable* staticUnreachable(std::uint16_t script, std::uint32_t offset) {
  const auto found = std::ranges::find_if(StaticUnreachables, [&](const StaticUnreachable& entry) {
    return entry.script == script && entry.offset == offset;
  });
  return found == StaticUnreachables.end() ? nullptr : &*found;
}

// Every path of script 124 to the map changes on the true side of the tests that guard its dead
// ones knows trigger 221 set: it went through those tests knowing it, so their false side never
// runs.
void checkDeadWarpsTests(const std::map<std::uint16_t, Walked>& walked,
                         const script::ScriptLookup& lookup) {
  const Walked& code = walked.at(DeadWarpsScript);
  for (const std::uint32_t offset : LiveWarpsBesideDead) {
    INFO("script ", DeadWarpsScript, ", the map change at offset ", offset);
    const auto index = code.listing.indexAt(offset);
    if (!index) {
      FAIL("no instruction at the map change's offset");
      return;
    }
    std::size_t paths = 0;
    for (const script::ListingSection& section : code.listing.runnableSections()) {
      const auto found = script::walkSection(DeadWarpsScript, section.id, lookup,
                                             [&](const script::ListingInstruction& instruction) {
                                               return instruction.offset == offset;
                                             });
      for (const script::GuardedInstruction& path : found) {
        if (path.place.script != DeadWarpsScript || path.place.instruction != *index) {
          continue;
        }
        ++paths;
        CHECK(std::ranges::contains(
            path.state, script::TriggerEffect{.trigger = DeadWarpsTrigger, .set = true}));
      }
    }
    CHECK(paths > 0);
  }
}

} // namespace

TEST_CASE(
    "the user's disc: a walk that summarizes routines finds the map changes of the full walk") {
  const auto scriptsText = variable(ProofVariable);
  const auto discPath = dwe::test_support::userDisc();
  if (!scriptsText || !discPath) {
    MESSAGE("DWE_WALK_PROOF or DWE_DISC is not set: skipping the proof of the summarized walk");
    return;
  }
  std::map<std::uint16_t, Walked> walked;
  loadScripts(*discPath, walked);
  const script::ScriptLookup lookup = lookupOf(walked);

  const std::chrono::minutes budget = variable(LongVariable) ? LongBudget : Budget;
  const std::set<std::uint16_t> compared = scriptsIn(*scriptsText, walked.size());
  MESSAGE("comparing ", compared.size(), " scripts with the full walk, budget ", budget.count(),
          " min per script");

  // The summarized walk of every script: its map changes, wherever they are, for the static check.
  std::set<ChangeKey> foundAnywhere;
  std::map<std::uint16_t, std::vector<script::MapChange>> summarized;
  std::map<std::uint16_t, std::size_t> mergesOf;
  script::WalkMonitor total;
  for (const auto& [id, code] : walked) {
    script::WalkMonitor monitor;
    auto changes =
        changesOf(id, code, lookup, walkOptions(script::DefaultMaximumRoutineWalks, monitor));
    for (const script::MapChange& change : changes) {
      foundAnywhere.insert(keyOf(change));
    }
    total.merges += monitor.merges;
    total.summarizedCalls += monitor.summarizedCalls;
    total.summaryWalks += monitor.summaryWalks;
    total.recursionCuts += monitor.recursionCuts;
    summarized.emplace(id, std::move(changes));
    mergesOf.emplace(id, monitor.merges);
  }
  MESSAGE("every script: ", total.merges, " merges of more than ", script::DefaultMaximumPaths,
          " paths, ", total.summarizedCalls, " summarized calls, ", total.summaryWalks,
          " summary walks, ", total.recursionCuts, " recursion cuts");

  // The map changes of script 124 that no flow takes: the ones after the tests of trigger 221 that
  // a path into them knows set.
  if (compared.contains(DeadWarpsScript)) {
    checkDeadWarpsTests(walked, lookup);
  }

  std::size_t notFound = 0;
  std::size_t shadowed = 0;
  std::size_t mergeUnfinished = 0;
  std::size_t stillMerging = 0;
  std::size_t mergeDifferentChanges = 0;
  std::size_t madeUpGuards = 0;
  std::size_t differentChanges = 0;
  std::size_t differentGuards = 0;
  std::vector<std::uint16_t> byFallback;
  std::vector<std::uint16_t> unproven;
  for (const std::uint16_t id : compared) {
    const Walked& code = walked.at(id);
    const std::vector<script::MapChange>& mine = summarized.at(id);
    INFO("script ", id);

    // Every warp_to and teleport of the script is found, or the flow from its sections never gets
    // to it, or it is one of StaticUnreachable.
    const std::set<std::size_t> reached =
        flowReached(code.listing, code.listing.runnableSections());
    const std::set<std::size_t> reachedByAny = flowReached(code.listing, code.listing.sections);
    std::size_t missing = 0;
    for (std::size_t index = 0; index < code.listing.instructions.size(); ++index) {
      if (code.listing.instructions[index].role != script::FlowRole::MapChange) {
        continue;
      }
      const std::uint32_t offset = code.listing.instructions[index].offset;
      const bool found = std::ranges::any_of(foundAnywhere, [&](const ChangeKey& key) {
        return std::get<0>(key) == id && std::get<1>(key) == index;
      });
      const auto* unreachable = staticUnreachable(id, offset);
      CHECK_MESSAGE((!found || unreachable == nullptr), "excepted but found at offset ", offset);
      if (!found && unreachable != nullptr) {
        MESSAGE("script ", id, ": the map change at offset ", offset,
                " is unreachable: ", unreachable->reason);
      } else if (!found && reached.contains(index)) {
        ++missing;
        MESSAGE("script ", id, ": the map change at offset ", offset,
                " is in the flow of a section and the walk does not find it");
      } else if (!found && reachedByAny.contains(index)) {
        ++shadowed;
        MESSAGE(
            "script ", id, ": the map change at offset ", offset,
            " is unreachable: only a section shadowed by an earlier one with the same id gets to it");
      } else if (!found) {
        MESSAGE("script ", id, ": the map change at offset ", offset,
                " is unreachable: no flow from a section gets to it");
      }
    }
    notFound += missing;

    // The merge check: if the script merged paths, a walk that keeps many more of them must find
    // the same map changes, and every change the merged walk gives as structured must be one it
    // gives too (a merge only makes a path unstructured, it never makes up a decision).
    const auto listedStill = std::ranges::find(MergeStillMerges, id, &StillMerging::script);
    const bool stillExpected = listedStill != MergeStillMerges.end();
    CHECK_MESSAGE((!stillExpected || mergesOf.at(id) != 0), "script ", id,
                  " is in MergeStillMerges but no longer merges");
    if (mergesOf.at(id) != 0) {
      if (const auto excepted = std::ranges::find(MergeExceptions, id, &MergeException::script);
          excepted != MergeExceptions.end()) {
        MESSAGE("script ", id, ": not compared by the merge check: ", excepted->reason);
      } else {
        script::WalkMonitor monitor;
        monitor.deadline = std::chrono::steady_clock::now() + budget;
        const auto many = changesOf(id, code, lookup,
                                    {.knownTriggers = {},
                                     .othersClear = false,
                                     .maximumPaths = LargeMaximumPaths,
                                     .maximumRoutineWalks = script::DefaultMaximumRoutineWalks,
                                     .monitor = &monitor});
        if (monitor.stopped) {
          ++mergeUnfinished;
          MESSAGE("script ", id, ": the walk with ", LargeMaximumPaths, " paths did not finish in ",
                  budget.count(), " min");
        } else {
          stillMerging += monitor.merges != 0 && !stillExpected ? 1 : 0;
          if (stillExpected) {
            CHECK_MESSAGE(monitor.merges != 0, "script ", id,
                          " is in MergeStillMerges but no longer merges"
                          " with ",
                          LargeMaximumPaths, " paths");
            MESSAGE("script ", id, ": still merges, as listed: ", listedStill->reason);
          }
          const auto madeUp = static_cast<std::size_t>(
              std::ranges::count_if(mine, [&](const script::MapChange& change) {
                return !change.path.guard.unstructured && !std::ranges::contains(many, change);
              }));
          const bool same = keysOf(mine) == keysOf(many);
          mergeDifferentChanges += same ? 0 : 1;
          madeUpGuards += madeUp;
          MESSAGE("script ", id, ": ", mergesOf.at(id), " merges; with ", LargeMaximumPaths,
                  " paths ", monitor.merges, " merges, same map changes ", same,
                  ", structured guards not found with them ", madeUp);
        }
      }
    }

    // The full walk, under the budget; for a script it does not finish, walks that summarize later
    // (FallbackRoutineWalks) must give what the summarized walk gives, guards and all.
    script::WalkMonitor monitor;
    const auto start = std::chrono::steady_clock::now();
    monitor.deadline = start + budget;
    const auto full =
        changesOf(id, code, lookup, walkOptions(std::numeric_limits<std::size_t>::max(), monitor));
    if (!monitor.stopped) {
      const bool sameChanges = keysOf(mine) == keysOf(full);
      const bool sameGuards = mine == full;
      differentChanges += sameChanges ? 0 : 1;
      differentGuards += sameGuards ? 0 : 1;
      MESSAGE("script ", id, ": the full walk took ", millisecondsSince(start), " ms, ",
              full.size(), " map changes, ", monitor.merges, " merges; same map changes ",
              sameChanges, ", same guards ", sameGuards);
      continue;
    }
    MESSAGE("script ", id, ": the full walk did not finish in ", budget.count(), " min");
    bool proven = true;
    for (const std::size_t walks : FallbackRoutineWalks) {
      script::WalkMonitor fallback;
      const auto fallbackStart = std::chrono::steady_clock::now();
      fallback.deadline = fallbackStart + budget;
      const auto changes = changesOf(id, code, lookup, walkOptions(walks, fallback));
      const bool same = !fallback.stopped && changes == mine;
      MESSAGE("script ", id, ": the walk that summarizes after ", walks, " walks took ",
              millisecondsSince(fallbackStart), " ms, finished ", !fallback.stopped,
              ", same guards ", same);
      proven = proven && same;
    }
    (proven ? byFallback : unproven).push_back(id);
  }
  MESSAGE("in a section's flow and not found: ", notFound,
          "; reached only from shadowed sections: ", shadowed);
  MESSAGE("merge check: ", mergeUnfinished, " did not finish with ", LargeMaximumPaths, " paths, ",
          stillMerging, " not in MergeStillMerges still merge with them");
  MESSAGE("proven by fallback: ", byFallback.size(), " (", listOf(byFallback), ")");
  MESSAGE("not proven: ", unproven.size(), " (", listOf(unproven), ")");
  CHECK(notFound == 0);
  // A script the larger walk cannot settle is not compared at all: it goes in MergeExceptions with
  // its reason.
  CHECK(mergeUnfinished == 0);
  CHECK(stillMerging == 0);
  CHECK(mergeDifferentChanges == 0);
  CHECK(madeUpGuards == 0);
  CHECK(differentChanges == 0);
  CHECK(differentGuards == 0);
  CHECK(unproven.empty());
}

TEST_CASE("the user's disc: the extraction's walk of every script ends within its budget") {
  const auto discPath = dwe::test_support::userDisc();
  if (!discPath) {
    MESSAGE("DWE_DISC is not set: skipping the walk of the disc's scripts");
    return;
  }
  std::map<std::uint16_t, Walked> walked;
  loadScripts(*discPath, walked);
  const script::ScriptLookup lookup = lookupOf(walked);
  std::size_t changes = 0;
  std::size_t terms = 0;
  std::size_t mostTerms = 0;
  std::uint16_t mostTermsScript = 0;
  std::size_t recursionCuts = 0;
  for (const auto& [id, code] : walked) {
    script::WalkMonitor monitor{.deadline = std::chrono::steady_clock::now() + ExtractionBudget,
                                .stopped = false,
                                .merges = 0,
                                .summarizedCalls = 0,
                                .summaryWalks = 0,
                                .recursionCuts = 0};
    const auto found = changesOf(id, code, lookup,
                                 {.knownTriggers = {},
                                  .othersClear = false,
                                  .maximumPaths = script::DefaultMaximumPaths,
                                  .maximumRoutineWalks = script::DefaultMaximumRoutineWalks,
                                  .monitor = &monitor});
    INFO("script ", id);
    CHECK_FALSE(monitor.stopped);
    changes += found.size();
    recursionCuts += monitor.recursionCuts;
    // How long the paths' guards are: a chain of undecided routines could make them grow with every
    // call.
    for (const script::MapChange& change : found) {
      const std::size_t count = change.path.guard.terms.size();
      terms += count;
      if (count > mostTerms) {
        mostTerms = count;
        mostTermsScript = id;
      }
    }
  }
  MESSAGE(changes, " map changes, ", terms, " guard terms; the most on one change: ", mostTerms,
          " (script ", mostTermsScript, "); ", recursionCuts, " recursion cuts");
  CHECK(changes > 0);
  CHECK(mostTerms <= MaximumGuardTerms);
  CHECK_MESSAGE(recursionCuts == ExpectedRecursionCuts, "recursion cuts: ", recursionCuts);

  // The walk of a new game knows the starting partners' raised triggers clear, though the game code
  // sets one of them first (data/new_game_story lists them as unknown); that is right only while no
  // script tests them.
  const auto startingRaised = [](std::int32_t trigger) {
    return std::ranges::contains(script::StartingPartnerRaisedTriggers, trigger);
  };
  for (const auto& [id, code] : walked) {
    for (const script::ListingInstruction& instruction : code.listing.instructions) {
      for (const script::Condition& condition : instruction.conditions) {
        const auto tested = std::ranges::find(condition.arguments, script::ArgumentKind::Trigger,
                                              &script::Argument::kind);
        const bool testsRaised = condition.test == script::ConditionTest::Trigger &&
                                 tested != condition.arguments.end() &&
                                 startingRaised(tested->value);
        CHECK_MESSAGE(!testsRaised, "script ", id, ", offset ", instruction.offset,
                      " tests a starting partner's raised trigger");
      }
      for (const script::Argument& argument : instruction.arguments) {
        if (argument.kind == script::ArgumentKind::Trigger && startingRaised(argument.value)) {
          MESSAGE("script ", id, ", offset ", instruction.offset, " (", instruction.op,
                  ") names trigger ", argument.value);
        }
      }
    }
  }
}
