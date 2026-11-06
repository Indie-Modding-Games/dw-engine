#include <dwe/test_support/temp_folder.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dwx.hpp"
#include "map_documents.hpp"
#include "script_warps_command.hpp"

using dwe::dwx::DerivedScriptWarps;
using dwe::dwx::deriveScriptWarps;
using dwe::dwx::scriptWarpsCommand;
using dwe::test_support::TempFolder;

namespace {

// MAPHEAD: entering twna01 (map 104 of the fixture table) loads script 1.
constexpr std::string_view MapHeadModule = R"lua(local story <const> = story
local ENDED <const> = story.ENDED
local routines = {}

function routines.town()
  story.set_script(1, 104) -- script, map
  return ENDED
end

return { script = 0, sections = { [0x0068] = routines.town } }
)lua";

// The town's script: a step that warps to room08 behind a trigger, and a section that opens the
// transport menu.
constexpr std::string_view TownModule = R"lua(local story <const> = story
local ENDED <const> = story.ENDED
local routines = {}

function routines.step()
  if story.trigger(10) then
    return story.warp_to(118, 2, 255)
  end
  return ENDED
end

function routines.carrier()
  story.call_routine(10)
  return ENDED
end

return { script = 1, sections = { [7] = routines.carrier, [51] = routines.step } }
)lua";

constexpr std::string_view StepRecord = R"(- map: twna01
  via: step
  section: 51
  to:
    map: room08
    exit: 2
  guard:
    terms:
    - if:
      - test: trigger
        trigger: 10
      holds: true
  source:
    script: 1
    section: 51
)";

constexpr std::string_view Transport = R"(destinations:
  - {map: 118, entry: 9, trigger: 30, cost: 100}
)";

std::string readText(const std::filesystem::path& file) {
  std::ifstream stream{file, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, {}};
}

// An invented content folder: a base pack with the fixture map table, two maps and the story Lua,
// and a rules pack with the transport menu's destinations.
void writeContent(const TempFolder& folder, std::string_view town = TownModule) {
  folder.write("content/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  folder.write("content/base/data/maps.yaml", std::string{dwe::assets::testing::FixtureTable});
  folder.write("content/base/map/twna01/data.yaml", dwe::assets::testing::fixtureMap());
  folder.write("content/base/map/room08/data.yaml", dwe::assets::testing::fixtureMap());
  folder.write("content/base/story/000.lua", std::string{MapHeadModule});
  folder.write("content/base/story/001.lua", std::string{town});
  folder.write("rules/pack.yaml", "id: rules\nversion: 1.0.0\nlayer: base\npriority: 1\n");
  folder.write("rules/rules/original/bird_transport.yaml", std::string{Transport});
}

std::vector<std::filesystem::path> foldersOf(const TempFolder& folder) {
  return {folder.path() / "content", folder.path() / "rules"};
}

DerivedScriptWarps derive(const TempFolder& folder,
                          std::string_view rules = dwe::dwx::DefaultTransportRules) {
  const auto folders = foldersOf(folder);
  auto derived = deriveScriptWarps(folders, rules);
  REQUIRE_MESSAGE(derived.has_value(), (derived ? std::string{} : derived.error().message));
  return std::move(*derived);
}

bool hasNote(const DerivedScriptWarps& derived, std::string_view text) {
  return std::ranges::any_of(derived.notes,
                             [text](const std::string& note) { return note.contains(text); });
}

} // namespace

TEST_CASE("dwx script-warps derives the map changes of the story Lua, transport picks and all") {
  const TempFolder folder;
  writeContent(folder);
  const DerivedScriptWarps derived = derive(folder);
  CHECK(derived.modules == 2);
  CHECK(derived.warps == 2);
  CHECK(derived.yaml.starts_with("warps:\n"));
  CHECK(derived.yaml.contains(StepRecord));
  // The pick of the one destination: room08's entry 9, with its cost.
  CHECK(derived.yaml.contains("    map: room08\n    exit: 9\n"));
  CHECK(derived.yaml.contains("  transport:\n    entry: 0\n    cost: 100\n"));
  // Without line maps the records have no offsets, and the notes say why.
  CHECK_FALSE(derived.yaml.contains("offset:"));
  CHECK(hasNote(derived, "there is no story/001_lines"));
}

TEST_CASE("dwx script-warps leaves the transport out for a ruleset without destinations") {
  const TempFolder folder;
  writeContent(folder);
  const DerivedScriptWarps derived = derive(folder, "other");
  CHECK(derived.warps == 1);
  CHECK(derived.yaml.contains(StepRecord));
  CHECK(hasNote(derived, "there is no rules/other/bird_transport"));
}

TEST_CASE("dwx script-warps compares with the packs' field/script_warps and writes it with -o") {
  const TempFolder folder;
  writeContent(folder);
  const std::string derived = derive(folder).yaml;
  folder.write("content/base/field/script_warps.yaml", derived);
  const auto folders = foldersOf(folder);
  CHECK(scriptWarpsCommand(folders, std::nullopt, dwe::dwx::DefaultTransportRules) ==
        dwe::dwx::ExitOk);

  // A mod moves the warp in the Lua: the packed document no longer matches until it is written.
  std::string edited{TownModule};
  edited.replace(edited.find("warp_to(118, 2"), std::string_view{"warp_to(118, 2"}.size(),
                 "warp_to(118, 3");
  folder.write("content/base/story/001.lua", edited);
  CHECK(scriptWarpsCommand(folders, std::nullopt, dwe::dwx::DefaultTransportRules) ==
        dwe::dwx::ExitUnknown);
  const std::filesystem::path out = folder.path() / "content/base/field/script_warps.yaml";
  CHECK(scriptWarpsCommand(folders, out, dwe::dwx::DefaultTransportRules) == dwe::dwx::ExitOk);
  const std::string written = readText(out);
  CHECK(written != derived);
  CHECK(written.contains("    map: room08\n    exit: 3\n"));
  CHECK(scriptWarpsCommand(folders, std::nullopt, dwe::dwx::DefaultTransportRules) ==
        dwe::dwx::ExitOk);
}

TEST_CASE("dwx script-warps fails, naming the module and its line, on Lua it cannot follow") {
  const TempFolder folder;
  std::string broken{TownModule};
  broken.replace(broken.find("  if story.trigger(10) then"),
                 std::string_view{"  if story.trigger(10) then"}.size(),
                 "  for i = 1, 2 do end\n  if story.trigger(10) then");
  writeContent(folder, broken);
  const auto folders = foldersOf(folder);
  const auto derived = deriveScriptWarps(folders, dwe::dwx::DefaultTransportRules);
  REQUIRE_FALSE(derived.has_value());
  CHECK(derived.error().message.contains("story/001: line 6"));
  const std::filesystem::path out = folder.path() / "out.yaml";
  CHECK(scriptWarpsCommand(folders, out, dwe::dwx::DefaultTransportRules) == dwe::dwx::ExitFailed);
  CHECK_FALSE(std::filesystem::exists(out));
}

TEST_CASE("dwx script-warps does not use a line map that the Lua no longer matches") {
  const TempFolder folder;
  writeContent(folder);
  // Only the warp's line has an offset: the if before it has none.
  folder.write("content/base/story/001_lines.yaml", "7: 0x0010\n");
  const DerivedScriptWarps derived = derive(folder);
  CHECK(derived.yaml.contains(StepRecord));
  CHECK(hasNote(derived, "story/001: its line map is not used"));
}

// Runs only when DWE_CONTENT names a content folder that dwx extracted (scripts/dev.sh mounts it),
// with the game pack's transport destinations: the story Lua gives the base pack's
// field/script_warps byte for byte.
TEST_CASE("the user's disc, extracted: the story Lua gives field/script_warps as extracted") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  const std::vector<std::filesystem::path> folders{*content, DWE_GAME_PACK};
  CHECK(scriptWarpsCommand(folders, std::nullopt, dwe::dwx::DefaultTransportRules) ==
        dwe::dwx::ExitOk);
}

TEST_CASE("dwx script-warps applies the packs' story patches, the highest winning a section") {
  const TempFolder folder;
  writeContent(folder);
  // A mod wraps the town's step: a new warp behind trigger 11, then the original's.
  folder.write("content/low_mod/pack.yaml", "id: low_mod\nversion: 1.0.0\nlayer: mods\n");
  folder.write("content/low_mod/story/patches/town.lua", R"lua(local story <const> = story
return { script = 1, sections = {
  [51] = function(original)
    if story.trigger(11) then
      return story.warp_to(118, 4, 255)
    end
    return original()
  end,
} }
)lua");
  const DerivedScriptWarps wrapped = derive(folder);
  CHECK(wrapped.warps == 3);
  CHECK(wrapped.yaml.contains("    map: room08\n    exit: 4\n"));
  CHECK(wrapped.yaml.contains("    map: room08\n    exit: 2\n"));
  CHECK(hasNote(wrapped, "story/001: patched by low_mod:story/patches/town.lua"));

  // A higher pack replaces the section outright: the original's warp is gone.
  folder.write("content/high_mod/pack.yaml",
               "id: high_mod\nversion: 1.0.0\nlayer: mods\npriority: 5\n");
  folder.write("content/high_mod/story/patches/town.lua", R"lua(return { script = 1, sections = {
  [51] = function() return story.warp_to(118, 5, 255) end,
} }
)lua");
  const DerivedScriptWarps replaced = derive(folder);
  CHECK(replaced.warps == 2);
  CHECK(replaced.yaml.contains("    map: room08\n    exit: 5\n"));
  CHECK_FALSE(replaced.yaml.contains("    map: room08\n    exit: 2\n"));
}

TEST_CASE("dwx script-warps fails, naming the patch and its line, on a patch it cannot follow") {
  const TempFolder folder;
  writeContent(folder);
  folder.write("content/my_mod/pack.yaml", "id: my_mod\nversion: 1.0.0\nlayer: mods\n");
  folder.write("content/my_mod/story/patches/wrap.lua", R"lua(return { script = 1, sections = {
  [51] = function(original)
    story.set_var(5, 1)
    local result = original()
    return result
  end,
} }
)lua");
  const auto folders = foldersOf(folder);
  const auto derived = deriveScriptWarps(folders, dwe::dwx::DefaultTransportRules);
  REQUIRE_FALSE(derived.has_value());
  CHECK(derived.error().message.contains("story/001: my_mod:story/patches/wrap.lua:4: "));
}
