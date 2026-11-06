#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/guarded_walk.hpp>
#include <dwe/formats/script/lua_emitter.hpp>
#include <dwe/formats/script/lua_reader.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/script_warps.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "schema_check.hpp"

namespace script = dwe::formats::script;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

struct Prepared {
  script::ScriptListing listing;
  script::StructuredScript structured;
};

Prepared prepare(std::string_view yaml) {
  const auto node = dwe::core::parseYaml(yaml);
  REQUIRE_MESSAGE(node.has_value(), errorOf(node));
  auto listing = script::readScriptListing(*node);
  REQUIRE_MESSAGE(listing.has_value(), errorOf(listing));
  auto structured = script::structureScript(*listing);
  REQUIRE_MESSAGE(structured.has_value(), errorOf(structured));
  return {.listing = std::move(*listing), .structured = std::move(*structured)};
}

// MAPHEAD: map 5 and the new game's map load script 1.
constexpr std::string_view MapHead = R"(sections:
- id: 0x0005
  at: l_0008
- id: 0x00cc
  at: l_0010
instructions:
- at: 0x0008
  label: l_0008
  op: set_script
  script: 1
  map: 5
- at: 0x000e
  op: end_section
- at: 0x0010
  label: l_0010
  op: set_script
  script: 1
  map: 204
- at: 0x0016
  op: end_section
)";

// The map's script: chains of IF entries, a trigger set on the way, give_item, a subroutine, a
// SWITCH, a SET_SELECTION, the transport menu, a jump to another script and an NPC's section.
constexpr std::string_view MapScript = R"(sections:
- id: 0x0033
  at: l_0008
- id: 0x0051
  at: l_0048
- id: 0x0052
  at: l_0078
- id: 0x0053
  at: l_0090
- id: 0x0054
  at: l_0098
- id: 0x0003
  at: l_00a0
instructions:
- at: 0x0008
  label: l_0008
  op: if
  conditions:
  - test: trigger
    trigger: 10
  - test: trigger
    clear: true
    combine: and
    trigger: 11
  - test: goto_if_false
    target: l_0020
- at: 0x0010
  op: set_trigger
  trigger: 12
- at: 0x0018
  op: warp_to
  screen: 7
  exit: 2
  section: 255
- at: 0x0020
  label: l_0020
  op: if
  conditions:
  - test: pstat
    compare: ge
    pstat: 3
    value: 2
  - test: trigger
    combine: or
    trigger: 13
  - test: goto_if_false
    target: l_0030
- at: 0x0028
  op: warp_to
  screen: 8
  exit: 1
  section: 5
- at: 0x0030
  label: l_0030
  op: give_item
  item: 3
  amount: 1
- at: 0x0034
  op: if
  conditions:
  - test: trigger
    trigger: 0
  - test: goto_if_false
    target: l_0040
- at: 0x0038
  op: warp_to
  screen: 9
  exit: 0
  section: 255
- at: 0x0040
  label: l_0040
  op: jump_and_link
  target: l_0060
- at: 0x0044
  op: end_section
- at: 0x0048
  label: l_0048
  op: switch
  pstat: 4
  cases:
  - l_0050
  - l_0058
- at: 0x0050
  label: l_0050
  op: warp_to
  screen: 10
  exit: 1
  section: 255
- at: 0x0058
  label: l_0058
  op: end_section
- at: 0x0060
  label: l_0060
  op: if
  conditions:
  - test: trigger
    clear: true
    trigger: 14
  - test: goto_if_true
    target: l_0070
- at: 0x0068
  op: warp_to
  screen: 11
  exit: 3
  section: 255
- at: 0x0070
  label: l_0070
  op: jump_return
- at: 0x0078
  label: l_0078
  op: set_selection
  options:
  - l_0080
  - l_0088
  text: "Yes\nNo"
- at: 0x0080
  label: l_0080
  op: warp_to
  screen: 12
  exit: 0
  section: 255
- at: 0x0088
  label: l_0088
  op: end_section
- at: 0x0090
  label: l_0090
  op: jump_to_file
  script: 2
  section: 16
- at: 0x0098
  label: l_0098
  op: call_routine
  routine: 10
- at: 0x009c
  op: end_section
- at: 0x00a0
  label: l_00a0
  op: warp_to
  screen: 13
  exit: 4
  section: 255
)";

// Another script, whose section 16 the map's script jumps to.
constexpr std::string_view OtherScript = R"(sections:
- id: 0x0010
  at: l_0008
instructions:
- at: 0x0008
  label: l_0008
  op: warp_to
  screen: 14
  exit: 1
  section: 255
)";

std::map<std::uint16_t, Prepared> scripts() {
  std::map<std::uint16_t, Prepared> result;
  result.emplace(script::MapHeadScript, prepare(MapHead));
  result.emplace(1, prepare(MapScript));
  result.emplace(2, prepare(OtherScript));
  return result;
}

script::ScriptLookup lookupOf(const std::map<std::uint16_t, Prepared>& prepared) {
  return [&prepared](std::uint16_t id) -> std::optional<script::WalkableScript> {
    const auto found = prepared.find(id);
    if (found == prepared.end()) {
      return std::nullopt;
    }
    return script::WalkableScript{.listing = &found->second.listing,
                                  .structured = &found->second.structured};
  };
}

script::ScriptLookup lookupOf(const std::map<std::uint16_t, script::LuaListing>& modules) {
  return [&modules](std::uint16_t id) -> std::optional<script::WalkableScript> {
    const auto found = modules.find(id);
    if (found == modules.end()) {
      return std::nullopt;
    }
    return script::WalkableScript{.listing = &found->second.listing,
                                  .structured = &found->second.structured,
                                  .offsets = found->second.offsets};
  };
}

struct Emitted {
  std::string code;
  script::LuaLineMap lines;
};

Emitted emit(std::uint16_t id, const Prepared& prepared) {
  const script::LuaModuleName name{.script = id,
                                   .source = std::format("script/dg/{:03}", id),
                                   .textSource = std::format("story_{:03}", id)};
  const auto module = script::emitLuaModule(prepared.listing, prepared.structured, name);
  REQUIRE_MESSAGE(module.has_value(), errorOf(module));
  // Through the YAML of story/<n>_lines, as the command reads it.
  const auto lines = script::readLuaLineMap(script::luaLinesToYaml(*module));
  REQUIRE_MESSAGE(lines.has_value(), errorOf(lines));
  return {.code = module->code, .lines = *lines};
}

// Every script written to Lua and read back, with its line map or without.
std::map<std::uint16_t, script::LuaListing> readBack(const std::map<std::uint16_t, Prepared>& from,
                                                     bool withLines) {
  std::map<std::uint16_t, script::LuaListing> result;
  for (const auto& [id, prepared] : from) {
    const Emitted emitted = emit(id, prepared);
    auto module = script::readLuaModule(emitted.code, withLines ? &emitted.lines : nullptr);
    REQUIRE_MESSAGE(module.has_value(), errorOf(module), "\n", emitted.code);
    CHECK(module->script == id);
    CHECK(module->offsets == withLines);
    CHECK_FALSE(module->lineMapProblem.has_value());
    result.emplace(id, std::move(*module));
  }
  return result;
}

const script::ScriptWarpSources sources{
    .talks = [](std::uint16_t map, std::uint16_t section) { return map == 5 && section == 3; },
    .transport = {{.map = 15, .exit = 9, .trigger = 300, .cost = 1000}}};

std::optional<std::string> mapNameOf(std::uint16_t map) {
  if (map == 5) {
    return "room_a";
  }
  return map >= 7 && map <= 15 ? std::optional{std::format("room_{}", map)} : std::nullopt;
}

std::string warpsYaml(const script::ScriptLookup& lookup) {
  const auto document =
      script::scriptWarpsToYaml(script::findScriptWarps(lookup, sources), lookup, &mapNameOf);
  const auto problems = dwe::formats::test::checkAgainstSchema(document, "script_warps");
  CHECK_MESSAGE(problems.empty(), (problems.empty() ? std::string{} : problems.front()));
  const auto text = dwe::core::emitYaml(document);
  REQUIRE_MESSAGE(text.has_value(), errorOf(text));
  return *text;
}

std::string withoutOffsets(std::string_view yaml) {
  std::string result;
  std::size_t at = 0;
  while (at < yaml.size()) {
    const std::size_t end = std::min(yaml.find('\n', at), yaml.size());
    const std::string_view line = yaml.substr(at, end - at);
    if (!line.contains("offset:")) {
      result += line;
      result += '\n';
    }
    at = end + 1;
  }
  return result;
}

// A hand-written module: a map's MAPHEAD section and its own script.
constexpr std::string_view HandMapHead = R"lua(local story <const> = story
local ENDED <const> = story.ENDED
local routines = {}

function routines.room()
  story.set_script(1, 5)
  return ENDED
end

return { script = 0, sections = { [0x0005] = routines.room } }
)lua";

script::LuaListing read(std::string_view code, const script::LuaLineMap* lines = nullptr) {
  auto module = script::readLuaModule(code, lines);
  REQUIRE_MESSAGE(module.has_value(), errorOf(module));
  return std::move(*module);
}

std::string readError(std::string_view code) {
  const auto module = script::readLuaModule(code);
  REQUIRE_FALSE(module.has_value());
  return module.error().message;
}

std::string handWarps(std::string_view mapScript) {
  std::map<std::uint16_t, script::LuaListing> modules;
  modules.emplace(script::MapHeadScript, read(HandMapHead));
  modules.emplace(1, read(mapScript));
  return warpsYaml(lookupOf(modules));
}

} // namespace

TEST_CASE("a story module read back with its line map gives the listing's warps, offsets and all") {
  const auto original = scripts();
  const std::string expected = warpsYaml(lookupOf(original));
  // What the scripts have: the first section's three warps, its subroutine's and the others'.
  CHECK(expected.contains("    map: room_7\n"));
  CHECK(expected.contains("    map: room_14\n"));
  CHECK(expected.contains("  transport:\n"));
  CHECK(expected.contains("  via: talk\n"));
  CHECK(expected.contains("- forget:\n"));
  CHECK(expected.contains("- selection: 0\n"));
  CHECK(expected.contains("- switch: 0\n"));
  const auto modules = readBack(original, true);
  CHECK(warpsYaml(lookupOf(modules)) == expected);
  // The listing read back has the original's offsets for every instruction the Lua runs.
  const script::ScriptListing& listing = modules.at(1).listing;
  CHECK(listing.sections == original.at(1).listing.sections);
  for (const script::ListingInstruction& instruction : listing.instructions) {
    CHECK(original.at(1).listing.indexAt(instruction.offset).has_value());
  }
}

TEST_CASE("a story module read back without a line map gives the same warps without offsets") {
  const auto original = scripts();
  const auto modules = readBack(original, false);
  const std::string lifted = warpsYaml(lookupOf(modules));
  CHECK_FALSE(lifted.contains("offset:"));
  CHECK(lifted == withoutOffsets(warpsYaml(lookupOf(original))));
}

TEST_CASE("a line map that a later edit left behind is not used, and says why") {
  const Emitted emitted = emit(1, scripts().at(1));
  // One more line at the top moves every instruction one line down.
  const std::string edited = "-- a mod's note\n" + emitted.code;
  const script::LuaListing module = read(edited, &emitted.lines);
  CHECK_FALSE(module.offsets);
  REQUIRE(module.lineMapProblem.has_value());
  CHECK(module.lineMapProblem.value_or("").contains("line"));
}

TEST_CASE("a line map reads from its YAML and refuses what is not a line and an offset") {
  const auto good = dwe::core::parseYaml("10: 0x0040\n11: 72\n");
  REQUIRE(good.has_value());
  const auto lines = script::readLuaLineMap(*good);
  REQUIRE_MESSAGE(lines.has_value(), errorOf(lines));
  CHECK(*lines == script::LuaLineMap{{10, 0x40}, {11, 72}});
  const auto bad = dwe::core::parseYaml("x: 0x0040\n");
  REQUIRE(bad.has_value());
  CHECK_FALSE(script::readLuaLineMap(*bad).has_value());
}

TEST_CASE("a hand-written module gives its warps with their conditions, read left to right") {
  const std::string yaml = handWarps(R"lua(local story <const> = story
local ENDED <const> = story.ENDED
local routines = {}

-- Stepping on cell 51.
function routines.step()
  if story.trigger(10) and not story.trigger(11) then
    story.set_trigger(12)
    return story.warp_to(7, 2, 255)
  elseif story.trigger(20) and (story.var(3) >= 2 or story.items(4) < 1) then
    return story.warp_to(8, 1, 5)
  end
  if routines.door() == ENDED then return ENDED end
  return ENDED
end

function routines.door()
  if not (story.money() >= 4294967295) then
    return story.warp_to(9, 0, 255)
  end
  return
end

return { script = 1, sections = { [51] = routines.step } }
)lua");
  CHECK(yaml == R"(warps:
- map: room_a
  via: step
  section: 51
  to:
    map: room_7
    exit: 2
  guard:
    terms:
    - if:
      - test: trigger
        trigger: 10
      - test: trigger
        clear: true
        combine: and
        trigger: 11
      holds: true
  sets_flags:
  - trigger: 12
    set: true
  source:
    script: 1
    section: 51
- map: room_a
  via: step
  section: 51
  to:
    map: room_8
    exit: 1
  arrival_section: 5
  guard:
    terms:
    - if:
      - test: trigger
        trigger: 10
      - test: trigger
        clear: true
        combine: and
        trigger: 11
      holds: false
    - if:
      - test: pstat
        compare: ge
        pstat: 3
        value: 2
      - test: item_count
        combine: or
        item: 4
        compare: lt
        value: 1
      - test: trigger
        combine: and
        trigger: 20
      holds: true
  source:
    script: 1
    section: 51
- map: room_a
  via: step
  section: 51
  to:
    map: room_9
    exit: 0
  guard:
    terms:
    - if:
      - test: trigger
        trigger: 10
      - test: trigger
        clear: true
        combine: and
        trigger: 11
      holds: false
    - if:
      - test: pstat
        compare: ge
        pstat: 3
        value: 2
      - test: item_count
        combine: or
        item: 4
        compare: lt
        value: 1
      - test: trigger
        combine: and
        trigger: 20
      holds: false
    - if:
      - test: money
        compare: ge
        value: -1
      holds: false
  source:
    script: 1
    section: 51
)");
}

TEST_CASE("a hand-written choice, loop and state machine read as the emitter writes them") {
  const std::string yaml = handWarps(R"lua(local story <const> = story
local ENDED <const> = story.ENDED
local routines = {}

function routines.talk()
  do
    local choice <const> = story.set_selection("story_001.text.0010", 2)
    if choice == 0 then
      return story.warp_to(7, 0, 255)
    elseif choice == 1 then
      story.delay(10)
    else
      return ENDED
    end
  end
  while true do
    if story.trigger(30) then
      break
    end
    story.delay(1)
  end
  do
    local state1 = 0
    while true do
      if state1 == 0 then
        state1 = 1
      elseif state1 == 1 then
        return story.warp_to(8, 0, 255)
      end
    end
  end
end

return { script = 1, sections = { [0x0003] = routines.talk } }
)lua");
  CHECK(yaml.contains("    - selection: 0\n"));
  CHECK(yaml.contains("    map: room_7\n"));
  // The warp after the loop and inside the machine is unstructured.
  CHECK(yaml.contains("    unstructured: true\n"));
  CHECK(yaml.contains("    map: room_8\n"));
}

TEST_CASE("Lua the reader cannot follow is an error that names its line") {
  constexpr std::string_view Head = "local routines = {}\nfunction routines.a()\n";
  constexpr std::string_view Tail =
      "\nend\nreturn { script = 1, sections = { [1] = routines.a } }\n";
  const auto errorFor = [&](std::string_view body) {
    return readError(std::string{Head} + std::string{body} + std::string{Tail});
  };
  CHECK(errorFor("if (story.trigger(1) and story.trigger(2)) or (story.trigger(3) and "
                 "story.trigger(4)) then return end")
            .starts_with("line 3: a condition that is not one chain"));
  CHECK(errorFor("story.not_an_op(1)").contains("story.not_an_op is not a function"));
  CHECK(errorFor("story.warp_to(1, 2)").contains("takes 3 operands, not 2"));
  CHECK(errorFor("while story.trigger(1) do end").contains("while true do"));
  CHECK(errorFor("local x = 1").contains("a local that does not start"));
  CHECK(errorFor("helper()").contains("not the story API nor a routine"));
  CHECK(errorFor("return routines.missing()").contains("routines.missing is not defined"));
  CHECK(errorFor("return story.delay(1)").contains("does not end the section"));
  CHECK(errorFor("for i = 1, 2 do end").starts_with("line 3:"));
  CHECK(readError("return { script = 1 }").contains("does not return"));
}

TEST_CASE("an if whose right side is a combination reads with its sides swapped") {
  const script::LuaListing module = read(R"lua(local routines = {}
function routines.a()
  if story.trigger(1) and (story.trigger(2) or story.trigger(3)) then
    return story.warp_to(1, 2, 255)
  end
  return
end
return { script = 1, sections = { [1] = routines.a } }
)lua");
  const auto found = std::ranges::find(module.listing.instructions, std::string{"if"},
                                       &script::ListingInstruction::op);
  REQUIRE(found != module.listing.instructions.end());
  REQUIRE(found->conditions.size() == 3);
  const auto triggerOf = [](const script::Condition& condition) {
    return condition.arguments.front().value;
  };
  CHECK(triggerOf(found->conditions[0]) == 2);
  CHECK(found->conditions[1].combine == script::Combine::Or);
  CHECK(triggerOf(found->conditions[1]) == 3);
  CHECK(found->conditions[2].combine == script::Combine::And);
  CHECK(triggerOf(found->conditions[2]) == 1);
}
