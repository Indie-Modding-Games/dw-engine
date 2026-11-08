// The game events mods handle (game_hooks.hpp) and the mods' entry scripts (mod_scripts.hpp).
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/script/api_version.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/game_hooks.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/mod_scripts.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "session_fixture.hpp"

namespace {

using dwe::session::GameSession;
using dwe::session::ModScripts;
using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::testing::errorOf;
using dwe::session::testing::OriginalTables;

// Made-up triggers the test mod sets when an event reaches it with the table it expects.
constexpr std::int32_t FirstHeardTrigger = 700;

// The test mod's entry script: each handler checks its table and sets its trigger.
constexpr std::string_view HookModMain = R"(
local heard = 700
local function mark(index) story.set_trigger(heard + index) end
hooks.on("game.new_game", function(t)
  if t.version == 1 and t.partner ~= nil then mark(0) end
end)
hooks.on("game.loaded", function(t) if t.version == 1 then mark(1) end end)
hooks.on("field.map_entered", function(t)
  if t.map == "map_b" and t.previous == "map_a" then mark(2) end
end)
hooks.on("field.map_left", function(t)
  if t.map == "map_a" and t.next == "map_b" then mark(3) end
end)
hooks.on("battle.started", function(t)
  if t.kind == "normal" and t.enemies == 2 and t.first_enemy == "enemy_a" and not t.scripted then
    mark(4)
  end
end)
hooks.on("battle.ended", function(t)
  if t.outcome == "victory" and t.won and t.recruit == nil then mark(5) end
end)
hooks.on("evolution.evolved", function(t)
  if t.from == "form_a" and t.to == "form_b" then mark(6) end
end)
hooks.on("clock.new_day", function(t) if t.year == 1 and t.day == 5 then mark(7) end end)
hooks.on("inventory.item_used", function(t)
  if t.item == "item_a" and t.context == "field" then mark(8) end
  -- A handler's table is its own: the next handler does not see this.
  t.item = "changed"
end)
hooks.on("inventory.item_used", function(t) if t.item == "item_a" then mark(9) end end)
)";

// The game pack over a stand-in base pack, and a mod pack with an entry script, all in memory but
// the game pack.
class ModPacks {
public:
  explicit ModPacks(std::string_view main) {
    memory_.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
    for (const std::string_view role : OriginalTables) {
      memory_.addFile(std::format("base/data/{}.yaml", role), std::format("{}: []\n", role));
    }
    memory_.addFile("hook_mod/pack.yaml",
                    "id: hook_mod\nversion: 1.0.0\nlayer: mods\napi: \"1.4\"\n");
    memory_.addFile("hook_mod/scripts/main.lua", main);
    const std::array<dwe::assets::PackLocation, 3> packs{
        dwe::assets::PackLocation{.fileSystem = &memory_,
                                  .root = "base",
                                  .fillsGaps = false,
                                  .origin = dwe::assets::PackOrigin::Engine},
        dwe::assets::PackLocation{.fileSystem = &disk_,
                                  .root = DWE_GAME_PACK,
                                  .fillsGaps = false,
                                  .origin = dwe::assets::PackOrigin::Engine},
        dwe::assets::PackLocation{.fileSystem = &memory_,
                                  .root = "hook_mod",
                                  .fillsGaps = false,
                                  .origin = dwe::assets::PackOrigin::Mod}};
    auto vfs = dwe::assets::VirtualFileSystem::mount(packs);
    REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
    files_ = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*vfs));
  }

  [[nodiscard]] const dwe::assets::VirtualFileSystem& files() const { return *files_; }

private:
  dwe::assets::MemoryFileSystem memory_;
  dwe::assets::DiskFileSystem disk_;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files_;
};

struct HookSession {
  std::unique_ptr<GameSession> session;
  StoryAdapter* story = nullptr;
  ModScripts* mods = nullptr;
};

HookSession hookSession(const ModPacks& packs) {
  auto story =
      std::make_unique<StoryAdapter>(dwe::session::StoryAdapterDesc{.files = &packs.files()});
  auto mods = std::make_unique<ModScripts>(*story);
  HookSession made{.session = nullptr, .story = story.get(), .mods = mods.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(story));
  systems.push_back(std::move(mods));
  auto session = GameSession::create({.files = &packs.files(),
                                      .rulesetId = "original",
                                      .rules = std::nullopt,
                                      .species = dwe::session::testing::fixtureFormsTable(),
                                      .seed = 7,
                                      .storage = nullptr,
                                      .gameVersion = "test",
                                      .wallClock = [] { return dwe::session::testing::SaveDate; },
                                      .systems = std::move(systems)});
  REQUIRE_MESSAGE(session.has_value(), errorOf(session));
  auto started = (*session)->startNewGame();
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));
  made.session = std::move(*session);
  return made;
}

bool heard(const StoryAdapter& story, std::int32_t index) {
  return story.state().trigger(FirstHeardTrigger + index).value_or(false);
}

} // namespace

TEST_CASE("a mod's entry script runs in its environment and hears the game's events") {
  const ModPacks packs{HookModMain};
  HookSession game = hookSession(packs);
  CHECK(game.mods->ran() == std::vector<std::string>{"hook_mod"});
  CHECK(game.session->hooks().bound());
  // The new game fires after the systems started it, so its story state keeps what the handler set.
  CHECK(heard(*game.story, 0));

  dwe::session::GameHooks& hooks = game.session->hooks();
  hooks.mapLeft("map_a", "map_b");
  hooks.mapEntered("map_b", "map_a");
  hooks.battleStarted(
      {.kind = "normal", .map = "map_b", .enemies = 2, .firstEnemy = "enemy_a", .scripted = false});
  hooks.battleEnded({.kind = "normal", .outcome = "victory", .recruit = ""});
  hooks.evolved("form_a", "form_b");
  hooks.newDay(1, 5);
  hooks.itemUsed("item_a", "field");
  for (std::int32_t index = 2; index <= 9; ++index) {
    CHECK_MESSAGE(heard(*game.story, index), "event " << index);
  }

  auto file = game.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  REQUIRE(game.story->state().setTrigger(FirstHeardTrigger + 1, false).has_value());
  REQUIRE(game.session->loadFile(*file).has_value());
  CHECK(heard(*game.story, 1));
}

TEST_CASE("a mod whose entry script fails is logged and the game goes on without its handlers") {
  const ModPacks packs{"hooks.on(\"game.new_game\", function() end)\nerror(\"broken\")\n"};
  HookSession game = hookSession(packs);
  CHECK(game.mods->ran().empty());
  CHECK(game.session->hooks().bound());
}

TEST_CASE("a mod cannot handle an event the game does not publish") {
  const ModPacks packs{"hooks.on(\"field.no_such_event\", function() end)\n"};
  HookSession game = hookSession(packs);
  CHECK(game.mods->ran().empty());
}

TEST_CASE("every game event is declared once and named as hooks.on wants") {
  for (std::size_t index = 0; index < dwe::session::GameEvents.size(); ++index) {
    const std::string_view event = dwe::session::GameEvents[index].name;
    CHECK(event.contains('.'));
    for (std::size_t other = index + 1; other < dwe::session::GameEvents.size(); ++other) {
      CHECK(event != dwe::session::GameEvents[other].name);
    }
  }
}

namespace {

// The part of docs/modding/api-lua.md this test writes: between these markers, the API the game
// gives mods as the code publishes it.
constexpr std::string_view GeneratedStart = "<!-- api-lua:generated -->";
constexpr std::string_view GeneratedEnd = "<!-- /api-lua:generated -->";

std::string fieldList(const dwe::session::GameEventInfo& event) {
  if (event.fields.empty()) {
    return "solo `version`";
  }
  std::string text;
  for (const dwe::session::GameEventField& field : event.fields) {
    if (!text.empty()) {
      text += ", ";
    }
    const bool optional = field.type.ends_with('?');
    const std::string_view type =
        optional ? field.type.substr(0, field.type.size() - 1) : field.type;
    text += std::format("`{}` ({}{})", field.name, type, optional ? " o nil" : "");
  }
  return text;
}

std::string generatedApi(const std::vector<std::string>& story) {
  std::string text =
      std::format("\nVersión de la API: **{}.{}.{}**. Tabla de cada evento: versión {}.\n\n",
                  dwe::script::LuaApiVersion.major, dwe::script::LuaApiVersion.minor,
                  dwe::script::LuaApiVersion.patch, dwe::session::GameEventTableVersion);
  text += "| Evento | Campos de la tabla, además de `version` |\n|---|---|\n";
  for (const dwe::session::GameEventInfo& event : dwe::session::GameEvents) {
    text += std::format("| `{}` | {} |\n", event.name, fieldList(event));
  }
  text += std::format("\nFunciones de `story` ({}):\n\n", story.size());
  for (const std::string& name : story) {
    text += std::format("- `story.{}`\n", name);
  }
  text += "\n";
  return text;
}

} // namespace

TEST_CASE("the Lua API page lists every game event and story function the game publishes") {
  // DWE_WRITE_API_DOC=1 rewrites the page's generated part instead of checking it.
  const ModPacks packs{""};
  HookSession game = hookSession(packs);
  dwe::script::ScriptVm* vm = game.story->vm();
  REQUIRE(vm != nullptr);
  const std::string generated = generatedApi(vm->api().publishedNames("story"));

  const std::filesystem::path page = std::filesystem::path{DWE_DOCS_DIR} / "modding" / "api-lua.md";
  std::string text;
  {
    std::ifstream stream{page, std::ios::binary};
    REQUIRE(stream.good());
    text.assign(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
  }
  const std::size_t start = text.find(GeneratedStart);
  const std::size_t end = text.find(GeneratedEnd);
  REQUIRE(start != std::string::npos);
  REQUIRE(end != std::string::npos);
  const std::size_t from = start + GeneratedStart.size();
  REQUIRE(from <= end);
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* write = std::getenv("DWE_WRITE_API_DOC");
  if (write != nullptr && std::string_view{write} == "1") {
    text.replace(from, end - from, generated);
    std::ofstream out{page, std::ios::binary};
    out << text;
    REQUIRE(out.good());
    return;
  }
  CHECK_MESSAGE(text.substr(from, end - from) == generated,
                "docs/modding/api-lua.md is out of date: run the session tests with "
                "DWE_WRITE_API_DOC=1");
}
