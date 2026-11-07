#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/lua_emitter.hpp>
#include <dwe/formats/script/lua_reader.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/script_yaml.hpp>
#include <dwe/formats/script/story_api_names.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "story_lua_harness.hpp"

using dwe::core::YamlNode;
using dwe::formats::script::convertScriptToLua;
using dwe::formats::script::LuaModule;
using dwe::formats::script::LuaModuleName;
using dwe::formats::script::ScriptListing;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

const LuaModuleName testName{.script = 900, .source = "script/dg/900", .textSource = "story_900"};

YamlNode text(std::string value) {
  return YamlNode::scalar(std::move(value));
}

YamlNode label(std::uint32_t offset) {
  return text(dwe::formats::script::labelName(offset));
}

// An IF entry: a test with its operands, or a jump entry.
struct Entry {
  std::string test;
  std::vector<std::pair<std::string, std::string>> fields;
};

// Builds a listing as its YAML, the way dwx writes it and a mod edits it.
class Listing {
public:
  Listing& section(std::uint16_t id, std::uint32_t at) {
    YamlNode node = YamlNode::mapping();
    node.set("id", text(std::format("{:#06x}", id)));
    node.set("at", label(at));
    sections_.append(std::move(node));
    return *this;
  }

  // An instruction with the given operands; the ones not given are 0.
  Listing& op(std::uint32_t at, const std::string& name,
              std::vector<std::pair<std::string, std::string>> fields = {}) {
    instructions_.append(instruction(at, name, std::move(fields)));
    return *this;
  }

  Listing& jump(std::uint32_t at, const std::string& name, std::uint32_t target) {
    YamlNode node = instruction(at, name, {});
    node.set("target", label(target));
    instructions_.append(std::move(node));
    return *this;
  }

  Listing& branch(std::uint32_t at, const std::vector<Entry>& entries) {
    YamlNode conditions = YamlNode::sequence();
    for (const Entry& entry : entries) {
      YamlNode node = YamlNode::mapping();
      node.set("test", text(entry.test));
      for (const auto& [key, value] : entry.fields) {
        node.set(key, text(value));
      }
      conditions.append(std::move(node));
    }
    YamlNode node = instruction(at, "if", {});
    node.set("conditions", std::move(conditions));
    instructions_.append(std::move(node));
    return *this;
  }

  Listing& choice(std::uint32_t at, const std::string& name, std::uint8_t pstat,
                  const std::vector<std::uint32_t>& targets) {
    const bool selection = name == "set_selection";
    YamlNode node = instruction(at, name, {{"count", std::to_string(targets.size())}});
    YamlNode labels = YamlNode::sequence();
    for (const std::uint32_t target : targets) {
      labels.append(label(target));
    }
    node.set(selection ? "options" : "cases", std::move(labels));
    if (selection) {
      node.set("text", YamlNode::scalar("Yes\nNo", YamlNode::ScalarStyle::Quoted));
    } else {
      node.set("pstat", text(std::to_string(pstat)));
    }
    instructions_.append(std::move(node));
    return *this;
  }

  [[nodiscard]] dwe::core::Result<ScriptListing> read() const {
    YamlNode listing = YamlNode::mapping();
    listing.set("sections", sections_);
    listing.set("instructions", instructions_);
    return dwe::formats::script::readScriptListing(listing);
  }

  [[nodiscard]] ScriptListing listing() const {
    auto read = this->read();
    REQUIRE_MESSAGE(read.has_value(), errorOf(read));
    return std::move(*read);
  }

private:
  // Every operand of the op is 0 but the given ones (an operand the op does not have is left out)
  // and the labels, which the caller sets.
  static YamlNode instruction(std::uint32_t at, const std::string& name,
                              std::vector<std::pair<std::string, std::string>> fields) {
    YamlNode node = YamlNode::mapping();
    node.set("at", text(std::format("{:#06x}", at)));
    const auto opcode = dwe::formats::script::opcodeNamed(name);
    const auto operands = opcode ? dwe::formats::script::opcodeOperands(*opcode)
                                 : std::vector<dwe::formats::script::OperandField>{};
    node.set("op", text(name));
    for (const auto& field : operands) {
      if (field.kind == dwe::formats::script::ArgumentKind::Label) {
        continue;
      }
      std::string value = "0";
      for (auto& [key, given] : fields) {
        if (key == field.name) {
          value = given;
        }
      }
      node.set(std::string{field.name}, text(std::move(value)));
    }
    if (name == "show_textbox") {
      node.set("text", YamlNode::scalar("Hello", YamlNode::ScalarStyle::Quoted));
    }
    return node;
  }

  YamlNode sections_ = YamlNode::sequence();
  YamlNode instructions_ = YamlNode::sequence();
};

LuaModule convert(const ScriptListing& listing) {
  auto module = convertScriptToLua(listing, testName);
  REQUIRE_MESSAGE(module.has_value(), errorOf(module));
  const auto error = story_lua::compileError(module->code, "module");
  REQUIRE_MESSAGE(!error.has_value(), error.value_or(""), "\n", module->code);
  return std::move(*module);
}

bool hasJumpStatement(std::string_view code) {
  static const std::regex jumpWord{R"(\bgoto\b)"};
  return std::regex_search(code.begin(), code.end(), jumpWord);
}

// A listing converts, and its section 0xFE runs in Lua until an error() whose message has
// `message`.
void checkRaises(const ScriptListing& listing, std::string_view message) {
  const LuaModule module = convert(listing);
  CHECK_MESSAGE(module.code.contains(message), module.code);
  const auto outcome =
      story_lua::runModule(module.code, 0xFE, story_lua::randomWorld(1), story_lua::FastLimits);
  CHECK_MESSAGE(outcome.status == "error", story_lua::describe(outcome));
}

// Every section of a listing runs in Lua as it runs in the reference interpreter, for `seeds`
// worlds.
void checkRunsLikeTheListing(const ScriptListing& listing, const LuaModule& module,
                             std::uint32_t seeds,
                             const story_lua::RunLimits& limits = story_lua::FullLimits) {
  std::set<std::uint16_t> ids;
  for (const auto& section : listing.sections) {
    CAPTURE(section.id);
    // A later section with an id already seen is never run by its id (getScriptSection takes the
    // first).
    if (!ids.insert(section.id).second) {
      continue;
    }
    for (std::uint32_t seed = 1; seed <= seeds; ++seed) {
      CAPTURE(seed);
      const story_lua::World world = story_lua::randomWorld(seed);
      const std::size_t entry =
          listing.indexAt(section.entry).value_or(listing.instructions.size());
      REQUIRE(entry < listing.instructions.size());
      const auto expected =
          story_lua::runReference(listing, entry, world, testName.textSource, limits);
      const auto actual = story_lua::runModule(module.code, section.id, world, limits);
      const bool same = story_lua::sameRun(actual, expected, limits);
      CHECK_MESSAGE(same, "expected ", story_lua::describe(expected), "\nactual   ",
                    story_lua::describe(actual), "\n", module.code);
      if (!same) {
        return;
      }
    }
  }
}

} // namespace

TEST_CASE("a section id the table holds twice runs its first section as the game does") {
  Listing builder;
  builder.section(0x51, 0)
      .section(0x51, 16)
      .op(0, "play_sound", {{"bank", "1"}, {"sound", "2"}})
      .op(8, "end_section")
      .op(16, "play_sound", {{"bank", "3"}, {"sound", "4"}})
      .op(24, "end_section");
  const ScriptListing listing = builder.listing();
  const auto module = convert(listing);
  CHECK_MESSAGE(module.code.contains("    [0x0051] = routines.at_0000,\n"), module.code);
  CHECK_MESSAGE(module.code.contains(
                    "-- [0x0051] = routines.at_0010: the game runs the one at 0x0000 instead"),
                module.code);
  checkRunsLikeTheListing(listing, module, 1);
  const auto outcome =
      story_lua::runModule(module.code, 0x51, story_lua::randomWorld(1), story_lua::FullLimits);
  CHECK_MESSAGE(story_lua::describe(outcome).contains("play_sound(1,2)"),
                story_lua::describe(outcome));
}

TEST_CASE("an IF becomes its entries folded left to right with explicit parentheses") {
  Listing builder;
  builder.section(0x51, 0)
      .branch(0,
              {{.test = "trigger", .fields = {{"trigger", "3"}}},
               {.test = "pstat",
                .fields = {{"compare", "ge"}, {"combine", "and"}, {"pstat", "5"}, {"value", "2"}}},
               {.test = "money", .fields = {{"compare", "lt"}, {"combine", "or"}, {"value", "-1"}}},
               {.test = "goto_if_true", .fields = {{"target", "l_0010"}}}})
      .op(8, "end_section")
      .op(16, "play_sound", {{"bank", "1"}, {"sound", "2"}})
      .op(24, "end_section");
  const auto module = convert(builder.listing());
  CHECK_MESSAGE(
      module.code.find(
          "if (story.trigger(3) and story.var(5) >= 2) or story.money() < 4294967295 then") !=
          std::string::npos,
      module.code);
  CHECK(module.code.find("story.play_sound(1, 2) -- bank, sound") != std::string::npos);
  checkRunsLikeTheListing(builder.listing(), module, 16);
}

TEST_CASE("an IF entry with no AND or OR replaces the result so far, and the Lua says so") {
  // story/128 (0x0094): "trigger 8, trigger 9, and trigger 10" is "9 and 10" in the original
  // (script_interp.c: scriptIfInstruction sets the result to an entry with no connector).
  Listing builder;
  builder.section(0x51, 0)
      .branch(0, {{.test = "trigger", .fields = {{"trigger", "8"}}},
                  {.test = "trigger", .fields = {{"trigger", "9"}}},
                  {.test = "trigger", .fields = {{"combine", "and"}, {"trigger", "10"}}},
                  {.test = "goto_if_false", .fields = {{"target", "l_0010"}}}})
      .op(8, "play_sound", {{"bank", "1"}, {"sound", "2"}})
      .op(16, "end_section");
  const auto module = convert(builder.listing());
  CHECK_MESSAGE(module.code.find("if story.trigger(9) and story.trigger(10) then -- the original "
                                 "drops story.trigger(8): a test with no and/or replaces the "
                                 "result") != std::string::npos,
                module.code);
  checkRunsLikeTheListing(builder.listing(), module, 16);
}

TEST_CASE("a happiness test compares signed and a cleared trigger test is a not") {
  Listing builder;
  builder.section(0x51, 0)
      .branch(
          0,
          {{.test = "stat", .fields = {{"stat", "9"}, {"compare", "lt"}, {"value", "65534"}}},
           {.test = "trigger", .fields = {{"clear", "true"}, {"combine", "and"}, {"trigger", "7"}}},
           {.test = "goto_if_false", .fields = {{"target", "l_0010"}}}})
      .op(8, "end_section")
      .op(16, "end_script");
  const auto module = convert(builder.listing());
  CHECK_MESSAGE(module.code.find("story.stat(9) < -2 and not story.trigger(7)") !=
                    std::string::npos,
                module.code);
  checkRunsLikeTheListing(builder.listing(), module, 16);
}

TEST_CASE("a subroutine that ends the section ends the caller too") {
  Listing builder;
  builder.section(0x51, 0)
      .jump(0, "jump_and_link", 24)
      .op(8, "play_sound")
      .op(16, "end_section")
      .branch(24, {{.test = "pstat", .fields = {{"compare", "eq"}, {"pstat", "1"}, {"value", "0"}}},
                   {.test = "goto_if_true", .fields = {{"target", "l_0028"}}}})
      .op(32, "jump_return")
      .op(40, "end_section");
  const auto module = convert(builder.listing());
  CHECK(module.code.find("if routines.at_0018() == ENDED then return ENDED end") !=
        std::string::npos);
  checkRunsLikeTheListing(builder.listing(), module, 16);
}

TEST_CASE(
    "a wait loop is a while loop with a break and a selection is an if chain with the cancel last") {
  Listing builder;
  builder.section(0x51, 0)
      .branch(0, {{.test = "trigger", .fields = {{"trigger", "4"}}},
                  {.test = "goto_if_true", .fields = {{"target", "l_0010"}}}})
      .jump(8, "jump_to", 0)
      .choice(16, "set_selection", 0, {32, 40})
      .op(24, "end_section")
      .op(32, "warp_to", {{"screen", "12"}, {"exit", "2"}, {"section", "60"}})
      .op(40, "jump_to_file", {{"script", "63"}, {"section", "5"}});
  const auto module = convert(builder.listing());
  CHECK(module.code.find("while true do") != std::string::npos);
  CHECK(module.code.find("break") != std::string::npos);
  CHECK(
      module.code.find("local choice <const> = story.set_selection(\"story_900.text.0010\", 2)") !=
      std::string::npos);
  CHECK(module.code.find("return story.warp_to(12, 2, 60)") != std::string::npos);
  REQUIRE(module.texts.size() == 1);
  CHECK(module.texts[0].text == "Yes\nNo");
  checkRunsLikeTheListing(builder.listing(), module, 16);
}

TEST_CASE("a selection without its end marker asks before its cancel arm, and reads back so") {
  Listing builder;
  builder.section(0x51, 0)
      .choice(0, "set_selection", 0, {16, 24})
      .op(8, "play_sound", {{"bank", "1"}, {"sound", "2"}})
      .op(16, "set_trigger", {{"trigger", "203"}})
      .op(24, "end_section");
  ScriptListing listing = builder.listing();
  REQUIRE_FALSE(listing.instructions[0].cancelMarker);
  const auto guarded = convert(listing);
  const std::string guard = std::format("    else\n      {}\n", dwe::formats::script::CancelGuard);
  CHECK_MESSAGE(guarded.code.contains(guard), guarded.code);
  // With the improvement off the cancel runs on as in the original.
  checkRunsLikeTheListing(listing, guarded, 16);
  const auto read = dwe::formats::script::readLuaModule(guarded.code);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  const auto selection = std::ranges::find(read->listing.instructions, "set_selection",
                                           &dwe::formats::script::ListingInstruction::op);
  REQUIRE(selection != read->listing.instructions.end());
  CHECK_FALSE(selection->cancelMarker);

  // With its end marker (cancel_marker in the listing) the cancel arm is the original's own.
  listing.instructions[0].cancelMarker = true;
  const auto marked = convert(listing);
  CHECK_FALSE(marked.code.contains(dwe::formats::script::CancelGuard));
  const auto readMarked = dwe::formats::script::readLuaModule(marked.code);
  REQUIRE_MESSAGE(readMarked.has_value(), errorOf(readMarked));
  const auto markedSelection = std::ranges::find(readMarked->listing.instructions, "set_selection",
                                                 &dwe::formats::script::ListingInstruction::op);
  REQUIRE(markedSelection != readMarked->listing.instructions.end());
  CHECK(markedSelection->cancelMarker);
}

TEST_CASE("a switch past its last case runs the last arm") {
  Listing builder;
  builder.section(0x51, 0)
      .choice(0, "switch", 7, {8, 16, 24})
      .op(8, "end_section")
      .op(16, "show_textbox")
      .op(24, "entity_walk_to", {{"entity", "tamer"}, {"x", "-40"}, {"z", "120"}})
      .op(32, "end_script");
  const auto module = convert(builder.listing());
  CHECK(module.code.find("local value <const> = story.var(7)") != std::string::npos);
  CHECK(module.code.find("story.entity_walk_to(PLAYER, -40, 120, 0, 0)") != std::string::npos);
  checkRunsLikeTheListing(builder.listing(), module, 32);
}

TEST_CASE("two sections that share code and a loop with two ways in run as the listing runs") {
  Listing builder;
  builder.section(0x51, 0)
      .section(0x52, 24)
      .branch(0, {{.test = "trigger", .fields = {{"trigger", "1"}}},
                  {.test = "goto_if_true", .fields = {{"target", "l_0020"}}}})
      .op(8, "play_sound")
      .op(16, "delay")
      .op(24, "give_item")
      .op(32, "play_sound")
      .branch(40, {{.test = "trigger", .fields = {{"trigger", "2"}}},
                   {.test = "goto_if_true", .fields = {{"target", "l_0010"}}}})
      .op(48, "end_section");
  const ScriptListing listing = builder.listing();
  const auto structured = dwe::formats::script::structureScript(listing);
  REQUIRE(structured.has_value());
  const auto module = convert(listing);
  CHECK_FALSE(hasJumpStatement(module.code));
  checkRunsLikeTheListing(listing, module, 32);
}

TEST_CASE(
    "the module of a listing is the same bytes every time and its lines name their instructions") {
  Listing builder;
  builder.section(0x51, 0)
      .op(0, "show_textbox")
      .choice(8, "set_selection", 0, {24, 16})
      .op(16, "end_section")
      .op(24, "set_trigger", {{"trigger", "203"}})
      .op(32, "end_script");
  const ScriptListing listing = builder.listing();
  const auto first = convert(listing);
  const auto second = convert(listing);
  CHECK(first.code == second.code);
  const auto lines = dwe::core::emitYaml(dwe::formats::script::luaLinesToYaml(first));
  REQUIRE(lines.has_value());
  CHECK(*lines == dwe::core::emitYaml(dwe::formats::script::luaLinesToYaml(second)).value_or(""));
  std::vector<std::string_view> codeLines;
  for (std::size_t start = 0; start < first.code.size();) {
    const std::size_t end = first.code.find('\n', start);
    codeLines.emplace_back(first.code.data() + start, end - start);
    start = end + 1;
  }
  REQUIRE_FALSE(first.lines.empty());
  for (const auto& line : first.lines) {
    REQUIRE(line.line >= 1);
    REQUIRE(line.line <= codeLines.size());
    const std::size_t index = listing.indexAt(line.offset).value_or(listing.instructions.size());
    REQUIRE(index < listing.instructions.size());
    const std::string& op = listing.instructions[index].op;
    const bool flow = op == "if" || op == "switch";
    if (!flow && line.line > 1 && !codeLines[line.line - 1].contains("function")) {
      CHECK_MESSAGE(codeLines[line.line - 1].contains(op), codeLines[line.line - 1]);
    }
  }
  const auto texts = dwe::core::emitYaml(dwe::formats::script::luaTextsToYaml(first));
  REQUIRE(texts.has_value());
  CHECK(texts->find("text.0000: \"Hello\"") != std::string::npos);
  CHECK(texts->find("text.0008: \"Yes\\nNo\"") != std::string::npos);
}

namespace {

// How many random listings a parity run builds, in how many worlds each section runs, and with what
// limits.
struct ParityRun {
  int rounds = 0;
  std::uint32_t seeds = 0;
  story_lua::RunLimits limits;
};

// What every commit runs, in about 5 seconds. Compared with FullParity it no longer covers:
// - shapes and worlds past the first 120 listings and 4 worlds of each (the seed is the same, so
// these are a prefix);
// - a loop that makes a call less often than about every 25 listing steps: it is cut before
// EventLimit calls, and
//   two such cut runs are compared only on the calls both made, so a difference after the shorter
//   cut, or a Lua run that is cut where the reference would still go on, is not seen.
constexpr ParityRun FastParity{.rounds = 120, .seeds = 4, .limits = story_lua::FastLimits};
// The whole check, run when DWE_FULL_PARITY is set: every loop with a call reaches EventLimit
// calls, and cut runs must match call for call. Several minutes.
constexpr ParityRun FullParity{.rounds = 300, .seeds = 8, .limits = story_lua::FullLimits};

void checkRandomListings(const ParityRun& run) {
  story_lua::Random random{4242};
  std::size_t converted = 0;
  const int rounds = run.rounds;
  for (int round = 0; round < rounds; ++round) {
    CAPTURE(round);
    const std::uint32_t count = 4 + random.next(16);
    const auto at = [](std::uint32_t index) { return index * 8; };
    const auto anywhere = [&] { return at(random.next(count)); };
    const auto small = [&] { return std::to_string(random.next(4)); };
    const auto compare = [&] {
      constexpr std::array<std::string_view, 6> Names = {"eq", "ne", "ge", "le", "gt", "lt"};
      return std::string{Names.at(random.next(6))};
    };
    const auto combine = [&](std::vector<std::pair<std::string, std::string>> fields) {
      const auto pick = random.next(4);
      if (pick == 1) {
        fields.emplace_back("combine", "and");
      } else if (pick == 2) {
        fields.emplace_back("combine", "or");
      }
      return fields;
    };
    Listing builder;
    builder.section(0xFE, 0);
    if (count > 6) {
      builder.section(0x51, anywhere());
    }
    for (std::uint32_t index = 0; index + 1 < count; ++index) {
      const std::uint32_t here = at(index);
      switch (random.next(14)) {
      case 0:
        builder.jump(here, "jump_to", anywhere());
        break;
      case 1:
      case 2: {
        std::vector<Entry> entries;
        const std::uint32_t tests = 1 + random.next(3);
        for (std::uint32_t test = 0; test < tests; ++test) {
          switch (random.next(8)) {
          case 0:
            entries.push_back({.test = "trigger", .fields = combine({{"trigger", small()}})});
            if (random.next(2) == 0) {
              entries.back().fields.emplace_back("clear", "true");
            }
            break;
          case 1:
            entries.push_back(
                {.test = "pstat",
                 .fields =
                     combine({{"compare", compare()}, {"pstat", small()}, {"value", small()}})});
            break;
          case 2: {
            constexpr std::array<std::string_view, 4> Values = {"0", "1", "65535", "65534"};
            entries.push_back(
                {.test = "stat",
                 .fields = combine({{"stat", random.next(2) == 0 ? "9" : "3"},
                                    {"compare", compare()},
                                    {"value", std::string{Values.at(random.next(4))}}})});
            break;
          }
          case 3:
            entries.push_back(
                {.test = "money",
                 .fields = combine(
                     {{"compare", compare()}, {"value", random.next(2) == 0 ? "-1" : small()}})});
            break;
          case 4:
            entries.push_back(
                {.test = "card",
                 .fields =
                     combine({{"card", small()}, {"compare", compare()}, {"value", small()}})});
            break;
          case 5:
            entries.push_back(
                {.test = "item_count",
                 .fields =
                     combine({{"item", small()}, {"compare", compare()}, {"value", small()}})});
            break;
          case 6:
            entries.push_back(
                {.test = "move", .fields = combine({{"move", small()}, {"negate", small()}})});
            break;
          default:
            entries.push_back(
                {.test = "condition", .fields = combine({{"mask", small()}, {"negate", small()}})});
            break;
          }
          if (random.next(3) == 0) {
            entries.push_back(
                {.test = random.next(2) == 0 ? "goto_if_true" : "goto_if_false",
                 .fields = {{"target", dwe::formats::script::labelName(anywhere())}}});
          }
        }
        entries.push_back({.test = random.next(2) == 0 ? "goto_if_true" : "goto_if_false",
                           .fields = {{"target", dwe::formats::script::labelName(anywhere())}}});
        builder.branch(here, entries);
        break;
      }
      case 3:
        builder.op(here, random.next(3) == 0 ? "end_section" : "delay", {{"frames", small()}});
        break;
      case 4: {
        std::vector<std::uint32_t> targets;
        const std::uint32_t arms = 1 + random.next(3);
        targets.reserve(arms);
        for (std::uint32_t arm = 0; arm < arms; ++arm) {
          targets.push_back(anywhere());
        }
        builder.choice(here, random.next(2) == 0 ? "switch" : "set_selection",
                       static_cast<std::uint8_t>(random.next(4)), targets);
        break;
      }
      case 5:
        builder.jump(here, "jump_and_link", anywhere());
        break;
      case 6:
        builder.op(here, "jump_return");
        break;
      case 7: {
        constexpr std::array<std::string_view, 6> Leaves = {"jump_to_file", "warp_to",
                                                            "teleport",     "jump_to_file_and_link",
                                                            "set_script",   "end_script"};
        builder.op(here, std::string{Leaves.at(random.next(6))},
                   {{"script", small()}, {"section", small()}});
        break;
      }
      case 8:
        builder.op(here, "show_textbox");
        break;
      case 9:
        builder.op(
            here, "entity_walk_to",
            {{"entity", random.next(2) == 0 ? "tamer" : "partner"}, {"x", "-5"}, {"z", small()}});
        break;
      default:
        builder.op(here, "play_sound", {{"bank", small()}, {"sound", small()}});
        break;
      }
    }
    builder.op(at(count - 1), "end_script");
    const auto listing = builder.read();
    REQUIRE_MESSAGE(listing.has_value(), errorOf(listing));
    const auto structured = dwe::formats::script::structureScript(*listing);
    if (!structured) {
      continue; // flow that does not build, such as a call to the middle of an IF chain
    }
    auto module = dwe::formats::script::emitLuaModule(*listing, *structured, testName);
    REQUIRE_MESSAGE(module.has_value(), errorOf(module));
    const auto error = story_lua::compileError(module->code, "module");
    REQUIRE_MESSAGE(!error.has_value(), error.value_or(""), "\n", module->code);
    CHECK_FALSE(hasJumpStatement(module->code));
    CHECK_FALSE(dwe::formats::script::forbiddenWordIn(module->code).has_value());
    // The listing's words for the player and the variables never reach the Lua.
    CHECK_FALSE(module->code.contains("TAMER"));
    CHECK_FALSE(module->code.contains("pstat"));
    checkRunsLikeTheListing(*listing, *module, run.seeds, run.limits);
    ++converted;
  }
  MESSAGE("listings converted and run: ", converted, " of ", rounds);
  CHECK(converted > static_cast<std::size_t>(rounds) * 9 / 10);
}

} // namespace

TEST_CASE("an op the interpreter does not have raises an error where it would run") {
  checkRaises(Listing{}
                  .section(0xFE, 0x00)
                  .op(0x00, "play_sound", {{"bank", "1"}, {"sound", "2"}})
                  .op(0x04, "not_an_op")
                  .op(0x08, "end_section")
                  .listing(),
              "not_an_op is not an instruction the interpreter has");
}

TEST_CASE("a data block raises an error if the flow reaches it") {
  checkRaises(Listing{}
                  .section(0xFE, 0x00)
                  .op(0x00, "play_sound", {{"bank", "1"}, {"sound", "2"}})
                  .op(0x04, "data")
                  .listing(),
              "a data block is not code");
}

TEST_CASE("a switch with no cases raises an error") {
  checkRaises(
      Listing{}.section(0xFE, 0x00).choice(0x00, "switch", 3, {}).op(0x08, "end_section").listing(),
      "a switch with no cases");
}

TEST_CASE("every story API name and operand name is free of the forbidden words") {
  using dwe::formats::script::forbiddenWordIn;
  std::size_t ops = 0;
  for (unsigned code = 0; code <= 0xFF; ++code) {
    const auto opcode = static_cast<std::uint8_t>(code);
    const std::string_view op = dwe::formats::script::opcodeName(opcode);
    if (op.empty()) {
      continue;
    }
    ++ops;
    CAPTURE(op);
    CHECK_FALSE(forbiddenWordIn(dwe::formats::script::storyFunctionName(op)).has_value());
    for (const auto& operand : dwe::formats::script::opcodeOperands(opcode)) {
      CAPTURE(operand.name);
      CHECK_FALSE(
          forbiddenWordIn(dwe::formats::script::storyOperandName(operand.name)).has_value());
    }
  }
  CHECK(ops > 100);
  // Each rename names an op of the interpreter and gives it a name no other op has.
  for (const auto& rename : dwe::formats::script::storyFunctionRenames()) {
    CAPTURE(rename.listing);
    CHECK(dwe::formats::script::opcodeNamed(rename.listing).has_value());
    CHECK_FALSE(dwe::formats::script::opcodeNamed(rename.story).has_value());
  }
  CHECK(forbiddenWordIn("story.Load_DIGIMON(3)") == "digimon");
  CHECK(forbiddenWordIn("\xC9SONY\xFF") == "sony");
  CHECK_FALSE(forbiddenWordIn("a digit and a species").has_value());
}

TEST_CASE("no two ops share a story name and no op has two operands of one name") {
  std::set<std::string_view> functions;
  std::size_t ops = 0;
  for (unsigned code = 0; code <= 0xFF; ++code) {
    const std::string_view op = dwe::formats::script::opcodeName(static_cast<std::uint8_t>(code));
    if (!op.empty()) {
      ++ops;
      const auto name = dwe::formats::script::storyFunctionName(op);
      CAPTURE(name);
      CHECK(functions.insert(name).second);
      // An op's operands keep distinct names once renamed.
      std::set<std::string_view> operands;
      for (const auto& operand :
           dwe::formats::script::opcodeOperands(static_cast<std::uint8_t>(code))) {
        const auto operandName = dwe::formats::script::storyOperandName(operand.name);
        CAPTURE(operandName);
        CHECK(operands.insert(operandName).second);
      }
    }
  }
  CHECK(functions.size() == ops);
  std::set<std::string_view> renamed;
  for (const auto& rename : dwe::formats::script::storyFunctionRenames()) {
    CAPTURE(rename.story);
    CHECK(renamed.insert(rename.story).second);
  }
  std::set<std::string_view> renamedOperands;
  for (const auto& rename : dwe::formats::script::storyOperandRenames()) {
    CAPTURE(rename.story);
    CHECK(renamedOperands.insert(rename.story).second);
  }
}

TEST_CASE("a renamed op is called by its story name with its operands renamed") {
  const LuaModule module =
      convert(Listing{}
                  .section(0xFE, 0x00)
                  .op(0x00, "set_digimon", {{"digimon", "7"}, {"slot", "2"}, {"autotalk", "1"}})
                  .op(0x04, "end_section")
                  .listing());
  CHECK_MESSAGE(module.code.contains("story.spawn_npc(7, 2, 1) -- species, slot, autotalk"),
                module.code);
  CHECK_FALSE(dwe::formats::script::forbiddenWordIn(module.code).has_value());
}

TEST_CASE("every section of a random listing runs in Lua as the listing runs") {
  checkRandomListings(FastParity);
}

TEST_CASE(
    "every section of a random listing runs in Lua as the listing runs with the full limits") {
  const char* full = std::getenv("DWE_FULL_PARITY");
  if (full == nullptr || *full == '\0') {
    MESSAGE("DWE_FULL_PARITY is not set: skipping the full parity run");
    return;
  }
  checkRandomListings(FullParity);
}

TEST_CASE("a section that starts where no instruction decoded is an error and not a crash") {
  // One section (0xFE at 8) whose first opcode, 0x40, the interpreter does not have: the listing
  // has no instruction.
  const std::vector<std::byte> bytes = {std::byte{0x08}, std::byte{0x00}, std::byte{0xFE},
                                        std::byte{0x00}, std::byte{0x08}, std::byte{0x00},
                                        std::byte{0xFF}, std::byte{0xFF}, std::byte{0x40},
                                        std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}};
  const auto disassembly = dwe::formats::script::disassembleScript(bytes);
  REQUIRE(disassembly.has_value());
  const ScriptListing listing = dwe::formats::script::listingOf(*disassembly);
  const auto module = convertScriptToLua(listing, testName);
  REQUIRE_FALSE(module.has_value());
  CHECK(module.error().message.contains("0x0008"));
}
