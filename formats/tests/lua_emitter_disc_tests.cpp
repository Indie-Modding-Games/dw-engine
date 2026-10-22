// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has
// one. Nothing read from the disc is printed but counts.

#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/lua_emitter.hpp>
#include <dwe/formats/script/scn_file.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/script_yaml.hpp>
#include <dwe/formats/script/story_api_names.hpp>
#include <dwe/formats/script/structuring.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "story_lua_harness.hpp"

namespace script = dwe::formats::script;
namespace disc = dwe::formats::disc;

namespace {

// What the US disc gives. Triggers: every static trigger operand (set_trigger, unset_trigger,
// spawn_chest, compare_date and the IF tests); none reaches 800, past the 100 bytes of
// ScriptState.triggers (script.h), so the runtime treats one as out of range
// (docs/architecture/interprete-historia.md#storystate).
constexpr std::size_t ExpectedScripts = 222;
constexpr std::size_t ExpectedTriggerOperands = 3453;
constexpr std::int32_t ExpectedHighestTrigger = 716;
constexpr std::int32_t TriggerCount = 800;
// The worlds each section runs in, in Lua and in the reference interpreter.
constexpr std::uint32_t ParitySeeds = 2;

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

struct Script {
  std::uint16_t id = 0;
  script::Disassembly disassembly;
};

// MAPHEAD.SCN as script 0 and DG.SCN's scripts from 1 (its script 0 is a copy that the game never
// reads).
std::optional<std::vector<Script>> readScripts() {
  const std::optional<std::filesystem::path> path = dwe::test_support::userDisc();
  if (!path) {
    MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
    return std::nullopt;
  }
  auto image = disc::DiscImage::open(*path);
  REQUIRE_MESSAGE(image.has_value(), errorOf(image));
  auto volume = disc::readIsoVolume(*image);
  REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));
  const auto read = [&](std::string_view name) {
    const auto found = std::ranges::find(volume->files, name, &disc::IsoFile::path);
    REQUIRE(found != volume->files.end());
    auto bytes = disc::readIsoFile(*image, *found);
    REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
    return std::move(*bytes);
  };
  std::vector<Script> scripts;
  const auto add = [&](std::uint16_t id, std::span<const std::byte> bytes) {
    auto disassembly = script::disassembleScript(bytes);
    REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
    scripts.push_back({.id = id, .disassembly = std::move(*disassembly)});
  };
  add(0, read("SCN/MAPHEAD.SCN"));
  const auto pack = read("SCN/DG.SCN");
  const auto slots = script::readScriptTable(pack);
  REQUIRE_MESSAGE(slots.has_value(), errorOf(slots));
  for (std::size_t id = 1; id < slots->size(); ++id) {
    add(static_cast<std::uint16_t>(id),
        std::span{pack}.subspan((*slots)[id].offset, (*slots)[id].size));
  }
  return scripts;
}

script::LuaModuleName nameOf(std::uint16_t id) {
  if (id == 0) {
    return {.script = 0, .source = "script/maphead", .textSource = "story_000"};
  }
  return {.script = id,
          .source = std::format("script/dg/{:03}", id),
          .textSource = std::format("story_{:03}", id)};
}

script::ScriptListing readBack(const script::Disassembly& disassembly) {
  const auto text = dwe::core::emitYaml(script::scriptToYaml(disassembly));
  REQUIRE_MESSAGE(text.has_value(), errorOf(text));
  const auto parsed = dwe::core::parseYaml(*text);
  REQUIRE_MESSAGE(parsed.has_value(), errorOf(parsed));
  auto listing = script::readScriptListing(*parsed);
  REQUIRE_MESSAGE(listing.has_value(), errorOf(listing));
  return std::move(*listing);
}

} // namespace

TEST_CASE(
    "the user's disc: every script's Lua compiles the same every time with no jump or forbidden word") {
  const auto scripts = readScripts();
  if (!scripts) {
    return;
  }
  static const std::regex jumpWord{R"(\bgoto\b)"};
  std::size_t bytes = 0;
  std::size_t texts = 0;
  std::size_t differ = 0;
  std::size_t failures = 0;
  std::size_t forbidden = 0;
  for (const Script& entry : *scripts) {
    CAPTURE(entry.id);
    const script::LuaModuleName name = nameOf(entry.id);
    const auto fromYaml = script::convertScriptToLua(readBack(entry.disassembly), name);
    REQUIRE_MESSAGE(fromYaml.has_value(), errorOf(fromYaml));
    const auto fromDisassembly =
        script::convertScriptToLua(script::listingOf(entry.disassembly), name);
    REQUIRE_MESSAGE(fromDisassembly.has_value(), errorOf(fromDisassembly));
    const auto again = script::convertScriptToLua(script::listingOf(entry.disassembly), name);
    REQUIRE(again.has_value());
    if (fromYaml->code != fromDisassembly->code || again->code != fromDisassembly->code ||
        dwe::core::emitYaml(script::luaLinesToYaml(*again)).value_or("") !=
            dwe::core::emitYaml(script::luaLinesToYaml(*fromYaml)).value_or("-")) {
      ++differ;
    }
    if (const auto error = story_lua::compileError(fromYaml->code, name.source)) {
      ++failures;
      MESSAGE(*error);
    }
    CHECK_FALSE(std::regex_search(fromYaml->code, jumpWord));
    if (const auto word = script::forbiddenWordIn(fromYaml->code)) {
      ++forbidden;
      MESSAGE("a forbidden word: ", *word);
    }
    bytes += fromYaml->code.size();
    texts += fromYaml->texts.size();
  }
  MESSAGE("scripts ", scripts->size(), ", Lua bytes ", bytes, ", texts ", texts,
          ", modules that differ ", differ, ", that do not compile ", failures,
          ", with a forbidden word ", forbidden);
  CHECK(scripts->size() == ExpectedScripts);
  CHECK(differ == 0);
  CHECK(failures == 0);
  CHECK(forbidden == 0);
}

TEST_CASE("the user's disc: no event script names a trigger past the 800 that ScriptState holds") {
  const auto scripts = readScripts();
  if (!scripts) {
    return;
  }
  std::size_t operands = 0;
  std::int32_t highest = 0;
  std::size_t outside = 0;
  const auto count = [&](const script::Argument& argument) {
    if (argument.kind != script::ArgumentKind::Trigger) {
      return;
    }
    ++operands;
    highest = std::max(highest, argument.value);
    outside += argument.value >= TriggerCount ? 1 : 0;
  };
  for (const Script& entry : *scripts) {
    for (const auto& instruction : script::listingOf(entry.disassembly).instructions) {
      std::ranges::for_each(instruction.arguments, count);
      for (const auto& condition : instruction.conditions) {
        std::ranges::for_each(condition.arguments, count);
      }
    }
  }
  MESSAGE("trigger operands ", operands, ", the highest ", highest, ", past 799 ", outside);
  CHECK(operands == ExpectedTriggerOperands);
  CHECK(highest == ExpectedHighestTrigger);
  CHECK(outside == 0);
}

TEST_CASE(
    "the user's disc: every section runs in Lua as its listing runs in the reference interpreter") {
  const auto scripts = readScripts();
  if (!scripts) {
    return;
  }
  std::size_t sections = 0;
  std::size_t differ = 0;
  std::size_t shadowed = 0;
  for (const Script& entry : *scripts) {
    CAPTURE(entry.id);
    const script::LuaModuleName name = nameOf(entry.id);
    const script::ScriptListing listing = script::listingOf(entry.disassembly);
    const auto module = script::convertScriptToLua(listing, name);
    REQUIRE_MESSAGE(module.has_value(), errorOf(module));
    std::set<std::uint16_t> ids;
    for (const auto& section : listing.sections) {
      // A later section with an id already seen is never run by its id (getScriptSection takes the
      // first).
      if (!ids.insert(section.id).second) {
        ++shadowed;
        continue;
      }
      ++sections;
      for (std::uint32_t seed = 1; seed <= ParitySeeds; ++seed) {
        const story_lua::World world = story_lua::randomWorld(seed);
        const std::size_t start =
            listing.indexAt(section.entry).value_or(listing.instructions.size());
        REQUIRE(start < listing.instructions.size());
        const auto expected =
            story_lua::runReference(listing, start, world, name.textSource, story_lua::FullLimits);
        const auto actual =
            story_lua::runModule(module->code, section.id, world, story_lua::FullLimits);
        if (!story_lua::sameRun(actual, expected, story_lua::FullLimits)) {
          ++differ;
          MESSAGE(std::format(
              "script {} section {:#06x} seed {}: expected {} calls ({}), Lua {} calls ({})",
              entry.id, section.id, seed, expected.events.size(), expected.status,
              actual.events.size(), actual.status));
          break;
        }
      }
    }
  }
  MESSAGE("sections ", sections, ", that run differently ", differ,
          ", shadowed by an earlier one with their id ", shadowed);
  CHECK(differ == 0);
}
