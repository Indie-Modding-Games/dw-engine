// Runs only when DWE_CONTENT names a content folder that dwx extracted (scripts/dev.sh mounts it).
// Plays every section of the event scripts through the StoryRunner and StoryLuaHost and checks its
// effects against a reference interpreter of the listings. Nothing read from the disc is printed
// but counts.
//
// The ops the story API does not publish yet are recorded stubs of this test only: each records
// its call and gives back nil, except the reads of the IF tests and set_selection, which answer
// from the run's world, and warp_to and teleport, which end the script. call_routine (and every
// other op that runs code of the original elsewhere) is a recorded call with no nested execution,
// in Lua and in the reference alike.

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/story_api_names.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/session/story_lua.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "story_lua_fixture.hpp"
#include "story_lua_harness.hpp"

namespace script = dwe::formats::script;
namespace api = dwe::session::story_api;
using script::FlowRole;
using script::ListingInstruction;
using script::ScriptListing;
using story_lua::World;
using story_play::Played;
using story_play::SectionRef;
using story_play::StoryPlay;

namespace {

constexpr std::uint32_t Seeds = 2;
constexpr std::size_t ReferenceSteps = 100'000;
// The original's script stack holds 8 frames (script_interp.c: pushScriptStack).
constexpr std::size_t OriginalStackFrames = 8;
// The loop of the training sections: script 49 section 0x52 jumps to section 0x33 of one of
// scripts 199 to 202, which jumps back.
constexpr SectionRef LoopStart{.script = 49, .section = 0x52};
// Each round adds 1 to variable 123 and section 0x52 leaves the loop at 20, so a run from 0 makes
// 20 rounds, 40 tail jumps, when no other way out is taken.
constexpr std::int64_t LoopCounter = 123;
constexpr std::size_t LoopRounds = 20;
constexpr std::uint32_t LoopSeeds = 16;
constexpr std::size_t LoopEventLimit = 20'000;

std::string readText(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary};
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

// MAPHEAD's listing as script 0 and DG's as scripts 1 on.
std::map<std::uint16_t, ScriptListing> readListings(const std::filesystem::path& base) {
  std::map<std::uint16_t, std::filesystem::path> paths{{0, base / "script" / "maphead.yaml"}};
  for (const auto& entry : std::filesystem::directory_iterator{base / "script" / "dg"}) {
    const std::string stem = entry.path().stem().string();
    if (!stem.empty() && std::ranges::all_of(stem, [](char c) { return c >= '0' && c <= '9'; })) {
      paths.emplace(static_cast<std::uint16_t>(std::stoi(stem)), entry.path());
    }
  }
  std::map<std::uint16_t, ScriptListing> listings;
  for (const auto& [id, path] : paths) {
    CAPTURE(path.string());
    const auto yaml = dwe::core::parseYaml(readText(path));
    REQUIRE(yaml.has_value());
    auto listing = script::readScriptListing(*yaml);
    REQUIRE_MESSAGE(listing.has_value(), story_play::errorText(listing));
    listings.emplace(id, std::move(*listing));
  }
  return listings;
}

std::string textSourceOf(std::uint16_t script) {
  return std::format("story_{:03}", script);
}

std::vector<std::int64_t> operandsOf(const ListingInstruction& instruction) {
  std::vector<std::int64_t> operands;
  for (const auto& argument : instruction.arguments) {
    if (argument.kind != script::ArgumentKind::Pad &&
        argument.kind != script::ArgumentKind::Label) {
      operands.push_back(argument.value);
    }
  }
  return operands;
}

std::int64_t inVariableRange(std::int64_t value) {
  return std::clamp<std::int64_t>(value, 0, story_play::OriginalRules.variableMax);
}

struct ReferenceRun {
  story_lua::Outcome outcome;
  std::size_t deepestStack = 0;
  // The return frames held when a jump_to_file runs: the link depth the host must report.
  std::size_t deepestStackAtFileJump = 0;
  // jump_and_link calls made in MAPHEAD: their jump_return restores the map script as the base of
  // the relative jumps (script_interp.c: scriptInstruction10to27 saves ACTIVE_MAP_SCRIPT), while
  // Lua returns into MAPHEAD.
  std::size_t mapheadCalls = 0;
  // The branches MAPHEAD code took after such a return, which the original would take from the
  // map script's base.
  std::size_t mapheadBranchesAfterReturn = 0;
};

// The listings run as the original runs them, over a state the run changes: a stack of return
// frames that jump_to_file leaves alone.
class Reference {
public:
  Reference(const std::map<std::uint16_t, ScriptListing>& listings, World world, std::uint32_t seed,
            std::size_t eventLimit)
      : listings_{&listings}, world_{std::move(world)}, eventLimit_{eventLimit} {
    random_.reset(seed);
  }

  ReferenceRun run(SectionRef start) {
    struct Frame {
      std::uint16_t script = 0;
      std::size_t pc = 0;
    };
    std::vector<Frame> stack;
    std::uint16_t current = start.script;
    std::optional<std::size_t> pc = entryOf(start);
    bool baseIsMapScript = false;
    const auto finish = [&](std::string_view status) {
      run_.outcome.status = status;
      return run_;
    };
    if (!pc) {
      return finish("error");
    }
    for (std::size_t step = 0; step < ReferenceSteps; ++step) {
      const ScriptListing& listing = listings_->at(current);
      if (*pc >= listing.instructions.size()) {
        return finish("error");
      }
      const ListingInstruction& instruction = listing.instructions[*pc];
      const auto at = [&](std::uint32_t offset) {
        return listing.indexAt(offset).value_or(listing.instructions.size());
      };
      const bool branches =
          instruction.role == FlowRole::Jump || instruction.role == FlowRole::If ||
          instruction.role == FlowRole::Switch || instruction.role == FlowRole::Selection;
      if (branches && baseIsMapScript && current == 0) {
        ++run_.mapheadBranchesAfterReturn;
      }
      switch (instruction.role) {
      case FlowRole::Plain:
      case FlowRole::LoadScript:
      case FlowRole::FileCall:
        if (const auto status = apply(instruction, current)) {
          return finish(*status);
        }
        ++*pc;
        break;
      case FlowRole::FileJump: {
        const SectionRef target{.script = instruction.reference->script,
                                .section = instruction.reference->section};
        if (!record(std::format("jump_to_file({},{})", target.script, target.section))) {
          return finish("cut");
        }
        run_.deepestStackAtFileJump = std::max(run_.deepestStackAtFileJump, stack.size());
        pc = entryOf(target);
        if (!pc) {
          return finish("error");
        }
        current = target.script;
        baseIsMapScript = false;
        break;
      }
      case FlowRole::MapChange:
        return finish(record(story_lua::callOf(instruction, textSourceOf(current))) ? "ended"
                                                                                    : "cut");
      case FlowRole::Call:
        run_.mapheadCalls += current == 0 ? 1 : 0;
        stack.push_back({.script = current, .pc = *pc + 1});
        run_.deepestStack = std::max(run_.deepestStack, stack.size());
        pc = at(instruction.targets.front());
        break;
      case FlowRole::Return:
        if (stack.empty()) {
          return finish("returned");
        }
        baseIsMapScript = stack.back().script == 0;
        current = stack.back().script;
        pc = stack.back().pc;
        stack.pop_back();
        break;
      case FlowRole::Jump:
        pc = at(instruction.targets.front());
        break;
      case FlowRole::If:
        pc = branchOf(instruction, at).value_or(*pc + 1);
        break;
      case FlowRole::Switch: {
        if (instruction.targets.empty()) {
          return finish("error");
        }
        const std::size_t value =
            world_.vars.at(story_lua::slot(instruction.argument("pstat").value_or(0)));
        pc = at(instruction.targets[std::min(value, instruction.targets.size() - 1)]);
        break;
      }
      case FlowRole::Selection: {
        if (!record(story_lua::callOf(instruction, textSourceOf(current)))) {
          return finish("cut");
        }
        const std::size_t pick =
            world_.picks.at(picked_++ % world_.picks.size()) % (instruction.targets.size() + 1);
        pc = pick < instruction.targets.size() ? at(instruction.targets[pick]) : *pc + 1;
        break;
      }
      case FlowRole::EndSection:
      case FlowRole::EndScript:
        return finish("ended");
      case FlowRole::Data:
        return finish("error");
      }
    }
    return finish("cut");
  }

private:
  [[nodiscard]] std::optional<std::size_t> entryOf(SectionRef section) const {
    const auto listing = listings_->find(section.script);
    if (listing == listings_->end()) {
      return std::nullopt;
    }
    const auto found =
        std::ranges::find(listing->second.sections, section.section, &script::ListingSection::id);
    if (found == listing->second.sections.end()) {
      return std::nullopt;
    }
    return listing->second.indexAt(found->entry);
  }

  [[nodiscard]] std::optional<std::size_t> branchOf(const ListingInstruction& instruction,
                                                    const auto& at) const {
    bool result = false;
    for (std::size_t index = 0; index < instruction.conditions.size(); ++index) {
      const auto& condition = instruction.conditions[index];
      if (condition.test == script::ConditionTest::GotoIfTrue ||
          condition.test == script::ConditionTest::GotoIfFalse) {
        if (result == (condition.test == script::ConditionTest::GotoIfTrue)) {
          const auto jump = std::ranges::find(instruction.conditionalJumps, index,
                                              &script::ConditionalJump::entry);
          return at(jump->target);
        }
        continue;
      }
      const bool holds = story_lua::evaluate(condition, world_);
      switch (condition.combine) {
      case script::Combine::And:
        result = result && holds;
        break;
      case script::Combine::Or:
        result = result || holds;
        break;
      case script::Combine::Replace:
        result = holds;
        break;
      }
    }
    return std::nullopt;
  }

  bool record(std::string event) {
    if (run_.outcome.events.size() >= eventLimit_) {
      return false;
    }
    run_.outcome.events.push_back(std::move(event));
    return true;
  }

  bool writeVariable(std::int64_t variable, std::int64_t value) {
    const std::int64_t kept = inVariableRange(value);
    world_.vars.at(static_cast<std::size_t>(variable)) = static_cast<std::uint32_t>(kept);
    return record(std::format("var {} = {}", variable, kept));
  }

  // Runs a plain op; the status it ends the run with, if it does.
  std::optional<std::string_view> apply(const ListingInstruction& instruction,
                                        std::uint16_t current) {
    const std::string_view name = script::storyFunctionName(instruction.op);
    const std::vector<std::int64_t> operands = operandsOf(instruction);
    const auto variable = [&](std::size_t index) {
      return static_cast<std::int64_t>(world_.vars.at(static_cast<std::size_t>(operands[index])));
    };
    bool recorded = true;
    if (name == api::SetTrigger || name == api::UnsetTrigger) {
      if (std::cmp_greater_equal(operands[0], world_.triggers.size())) {
        return "error";
      }
      const bool set = name == api::SetTrigger;
      world_.triggers[static_cast<std::size_t>(operands[0])] = set;
      recorded = record(std::format("trigger {} = {}", operands[0], set ? 1 : 0));
    } else if (name == api::SetVariable) {
      recorded = writeVariable(operands[0], operands[1]);
    } else if (name == api::AddToVariable) {
      recorded = writeVariable(operands[0], variable(0) + operands[1]);
    } else if (name == api::ReduceVariable) {
      recorded = writeVariable(operands[0], variable(0) - operands[1]);
    } else if (name == api::CopyVariable) {
      recorded = writeVariable(operands[1], variable(0));
    } else if (name == api::StoreRandom) {
      const std::int64_t drawn = random_.next();
      recorded = writeVariable(operands[0], ((inVariableRange(operands[1]) + 1) * drawn) >> 15);
    } else if (name == api::Delay) {
      recorded = record(std::format("delay({})", std::max<std::int64_t>(operands[0], 1)));
    } else {
      recorded = record(story_lua::callOf(instruction, textSourceOf(current)));
    }
    return recorded ? std::nullopt : std::optional<std::string_view>{"cut"};
  }

  const std::map<std::uint16_t, ScriptListing>* listings_;
  World world_;
  std::size_t eventLimit_;
  story_play::SeededRandom random_;
  std::size_t picked_ = 0;
  ReferenceRun run_;
};

// The stubs of the ops the story API does not publish, and PLAYER and PARTNER.
class Stubs {
public:
  explicit Stubs(StoryPlay& play) : recorder_{&play.recorder()} {
    dwe::script::ApiRegistry registry = play.vm().api();
    REQUIRE(
        registry.publishValue(dwe::session::StoryNamespace, "PLAYER", story_play::PlayerEntity));
    REQUIRE(
        registry.publishValue(dwe::session::StoryNamespace, "PARTNER", story_play::PartnerEntity));
    const auto read = [this, &registry](std::string_view name, auto answer) {
      REQUIRE(registry.publish(
          dwe::session::StoryNamespace, name,
          [this, name,
           answer](const dwe::script::ApiCall& call) -> dwe::core::Result<dwe::script::ApiValue> {
            const auto operand =
                call.arguments().empty() ? dwe::core::Result<std::int64_t>{0} : call.integer(0);
            if (!operand) {
              return std::unexpected{operand.error()};
            }
            if (name != "has_condition" &&
                (*operand < 0 || std::cmp_greater_equal(*operand, story_lua::TableSize))) {
              return dwe::core::fail(std::format("{} has no entry {}", name, *operand));
            }
            return answer(*world_, static_cast<std::size_t>(*operand));
          }));
    };
    using dwe::script::ApiValue;
    read("stat", [](const World& world, std::size_t n) {
      return ApiValue{std::int64_t{world.stats.at(n)}};
    });
    read("cards", [](const World& world, std::size_t n) {
      return ApiValue{std::int64_t{world.cards.at(n)}};
    });
    read("items", [](const World& world, std::size_t n) {
      return ApiValue{std::int64_t{world.items.at(n)}};
    });
    read("knows_move",
         [](const World& world, std::size_t n) { return ApiValue{world.moves.at(n)}; });
    read("has_condition", [](const World& world, std::size_t mask) {
      return ApiValue{(world.condition & mask) != 0};
    });
    read("money",
         [](const World& world, std::size_t /*n*/) { return ApiValue{std::int64_t{world.money}}; });
    REQUIRE(registry.publish(
        dwe::session::StoryNamespace, "set_selection",
        [this](const dwe::script::ApiCall& call) -> dwe::core::Result<ApiValue> {
          const auto key = call.text(0);
          const auto count = call.integer(1);
          if (!key || !count) {
            return dwe::core::fail("set_selection takes a text key and a count");
          }
          if (!recorder_->record(std::format("set_selection({},{})", *key, *count))) {
            return dwe::core::fail(std::string{story_play::LimitReached});
          }
          const std::uint32_t pick = world_->picks.at(picked_++ % world_->picks.size());
          return ApiValue{static_cast<std::int64_t>(pick % static_cast<std::uint32_t>(*count + 1))};
        }));
    for (unsigned code = 0; code <= 0xFF; ++code) {
      const auto opcode = static_cast<std::uint8_t>(code);
      const std::string_view op = script::opcodeName(opcode);
      const std::string_view name = op.empty() ? op : script::storyFunctionName(op);
      if (name.empty() || registry.isPublished(dwe::session::StoryNamespace, name)) {
        continue;
      }
      const bool endsScript = script::opcodeFlowRole(opcode) == FlowRole::MapChange;
      stubbed_.insert(std::string{name});
      REQUIRE(registry.publish(dwe::session::StoryNamespace, name,
                               story_play::recordedCall(*recorder_, std::string{name},
                                                        endsScript ? ApiValue{true} : ApiValue{})));
    }
  }

  void reset(const World& world) {
    world_ = &world;
    picked_ = 0;
  }

  // Whether an event is a call of a stub.
  [[nodiscard]] bool isStubCall(std::string_view event) const {
    const std::size_t open = event.find('(');
    return open != std::string_view::npos && stubbed_.contains(std::string{event.substr(0, open)});
  }

private:
  story_play::Recorder* recorder_;
  const World* world_ = nullptr;
  std::size_t picked_ = 0;
  std::set<std::string> stubbed_;
};

std::vector<std::int64_t> variablesOf(const World& world) {
  return {world.vars.begin(), world.vars.end()};
}

Played playIn(StoryPlay& play, Stubs& stubs, SectionRef section, const World& world,
              std::uint32_t seed, std::size_t eventLimit) {
  play.state().reset(world.triggers, variablesOf(world));
  stubs.reset(world);
  play.recorder().limit = eventLimit;
  return play.play(section, seed);
}

// Equal runs: the same status and the same events. A cut is a run that reached the event limit,
// so two cut runs both hold exactly that many events.
bool sameEffects(const Played& played, const story_lua::Outcome& expected, std::size_t eventLimit) {
  if (played.status == "cut" && expected.status == "cut" &&
      (played.events.size() != eventLimit || expected.events.size() != eventLimit)) {
    return false;
  }
  return played.status == expected.status && played.events == expected.events;
}

std::string describe(const std::vector<std::string>& events, std::string_view status) {
  std::string out = std::string{status} + ":";
  for (const std::string& event : events) {
    out += " " + event;
  }
  return out;
}

struct Mounted {
  dwe::assets::DiskFileSystem disk;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files;
};

std::unique_ptr<Mounted> mountContent(const std::filesystem::path& content) {
  auto mounted = std::make_unique<Mounted>();
  const auto packs = dwe::assets::findPacks(mounted->disk, content);
  REQUIRE_MESSAGE(packs.has_value(), story_play::errorText(packs));
  const auto opened =
      dwe::assets::openPacks(mounted->disk, packs->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE_MESSAGE(opened.has_value(), story_play::errorText(opened));
  auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(vfs.has_value(), story_play::errorText(vfs));
  mounted->files = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*vfs));
  return mounted;
}

} // namespace

TEST_CASE("the user's disc, extracted: every story section plays as its listing runs") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  const auto listings = readListings(*content / "base");
  const auto mounted = mountContent(*content);
  StoryPlay play{*mounted->files};
  Stubs stubs{play};
  std::size_t& tooDeep = story_play::loggedLinesWith("past the guard of");
  const std::size_t tooDeepBefore = tooDeep;

  std::size_t runs = 0;
  std::size_t agreeing = 0;
  std::size_t publishedOnly = 0;
  std::size_t withStubs = 0;
  std::size_t cutOnBoth = 0;
  std::size_t cutOnOne = 0;
  std::size_t deepestStack = 0;
  std::size_t deepestStackAtFileJump = 0;
  std::uint32_t deepestLink = 0;
  std::size_t mapheadCalls = 0;
  std::size_t mapheadBranches = 0;
  std::set<std::string> namesUsed;
  for (const auto& [id, listing] : listings) {
    std::set<std::uint16_t> sections;
    for (const script::ListingSection& section : listing.sections) {
      sections.insert(section.id);
    }
    for (const std::uint16_t section : sections) {
      for (std::uint32_t seed = 1; seed <= Seeds; ++seed) {
        const SectionRef ref{.script = id, .section = section};
        CAPTURE(id);
        CAPTURE(section);
        CAPTURE(seed);
        const World world = story_lua::randomWorld(seed);
        Reference reference{listings, world, seed, story_lua::EventLimit};
        const ReferenceRun expected = reference.run(ref);
        const Played played = playIn(play, stubs, ref, world, seed, story_lua::EventLimit);
        ++runs;
        const bool agrees = sameEffects(played, expected.outcome, story_lua::EventLimit);
        agreeing += agrees ? 1 : 0;
        CHECK_MESSAGE(agrees, "lua ", describe(played.events, played.status), " (", played.failure,
                      ")\nreference ", describe(expected.outcome.events, expected.outcome.status));
        const bool stubbed = std::ranges::any_of(
            played.events, [&](const std::string& event) { return stubs.isStubCall(event); });
        (stubbed ? withStubs : publishedOnly) += 1;
        const bool luaCut = played.status == "cut";
        const bool referenceCut = expected.outcome.status == "cut";
        cutOnBoth += luaCut && referenceCut ? 1 : 0;
        cutOnOne += luaCut != referenceCut ? 1 : 0;
        CHECK_MESSAGE(luaCut == referenceCut, "cut on one side: ", id, ":", section, " lua ",
                      played.status, " (", played.failure, "), reference ",
                      expected.outcome.status);
        deepestStack = std::max(deepestStack, expected.deepestStack);
        deepestStackAtFileJump = std::max(deepestStackAtFileJump, expected.deepestStackAtFileJump);
        if (played.status != "cut" && expected.outcome.status != "cut") {
          CHECK(played.deepestLink == expected.deepestStackAtFileJump);
        }
        deepestLink = std::max(deepestLink, played.deepestLink);
        mapheadCalls += expected.mapheadCalls;
        mapheadBranches += expected.mapheadBranchesAfterReturn;
      }
    }
  }
  MESSAGE("story runs: ", runs, ", agreeing ", agreeing, "; (i) only published ops ", publishedOnly,
          ", (ii) with stubs ", withStubs, "; cut on both sides ", cutOnBoth, ", on one side ",
          cutOnOne);
  MESSAGE("deepest return stack ", deepestStack, ", at a jump_to_file ", deepestStackAtFileJump,
          ", deepest link of a jump ", deepestLink, "; jump_and_link in MAPHEAD ", mapheadCalls,
          ", MAPHEAD branches after their return ", mapheadBranches);
  CHECK(runs == agreeing);
  CHECK(cutOnOne == 0);
  CHECK(publishedOnly > 0);
  CHECK(deepestStack <= OriginalStackFrames);
  CHECK(deepestLink <= OriginalStackFrames);
  CHECK(tooDeep == tooDeepBefore);
}

TEST_CASE("the user's disc, extracted: the training loop of script 49 runs as tail jumps") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  const auto listings = readListings(*content / "base");
  const auto mounted = mountContent(*content);
  StoryPlay play{*mounted->files};
  Stubs stubs{play};
  std::size_t& tooDeep = story_play::loggedLinesWith("past the guard of");
  const std::size_t tooDeepBefore = tooDeep;
  std::size_t mostRounds = 0;
  std::size_t mostJumps = 0;
  std::size_t peakSections = 0;
  for (std::uint32_t seed = 1; seed <= LoopSeeds; ++seed) {
    CAPTURE(seed);
    World world = story_lua::randomWorld(seed);
    world.vars.at(LoopCounter) = 0;
    Reference reference{listings, world, seed, LoopEventLimit};
    const ReferenceRun expected = reference.run(LoopStart);
    const Played played = playIn(play, stubs, LoopStart, world, seed, LoopEventLimit);
    CHECK(played.status != "cut");
    CHECK_MESSAGE(sameEffects(played, expected.outcome, LoopEventLimit), "lua ", played.status, " ",
                  played.failure, ", reference ", expected.outcome.status);
    CHECK(played.deepestLink == 0);
    CHECK(played.peakSections == 1);
    const auto rounds =
        static_cast<std::size_t>(std::ranges::count_if(played.events, [](const std::string& event) {
          return event.starts_with("jump_to_file(49,");
        }));
    mostRounds = std::max(mostRounds, rounds);
    mostJumps = std::max(mostJumps, played.fileJumps);
    peakSections = std::max(peakSections, played.peakSections);
  }
  MESSAGE("most rounds of the loop in one run: ", mostRounds, ", tail jumps ", mostJumps,
          ", most sections open at once ", peakSections);
  CHECK(mostRounds == LoopRounds);
  CHECK(mostJumps >= 2 * LoopRounds);
  CHECK(tooDeep == tooDeepBefore);
}

// The op names are checked against the emitter's in story_lua_tests.cpp; the values and the reads
// are not ops, so the modules show the emitter writes them.
TEST_CASE("the user's disc, extracted: the modules use the story values and reads by name") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  std::string modules;
  for (const auto& entry : std::filesystem::directory_iterator{*content / "base" / "story"}) {
    if (entry.path().extension() == ".lua") {
      modules += readText(entry.path());
    }
  }
  for (const std::string_view name : {api::Ended, api::Trigger, api::Variable}) {
    CAPTURE(name);
    CHECK(modules.contains(std::format("story.{}", name)));
  }
  // The listing's words for the player and the variables never reach the Lua.
  CHECK(modules.contains("story.PLAYER"));
  CHECK_FALSE(modules.contains("TAMER"));
  CHECK_FALSE(modules.contains("pstat"));
}
