#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/formats/script/lua_reader.hpp>
#include <dwe/session/story_lua.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "story_lua_fixture.hpp"

#ifndef DWE_WRITTEN_STORY
#error "DWE_WRITTEN_STORY must name the folder of the map editor's written story"
#endif

using story_play::Played;
using story_play::SectionRef;
using story_play::StoryPlay;

namespace {

constexpr std::string_view Header = "-- dwx lua 3: a module of the tests\n";

// The base module of the tests: script 1 sets trigger 1 in section 1 and trigger 2 in section 2.
constexpr std::string_view BaseModule = R"lua(
local story <const> = story
local ENDED <const> = story.ENDED
return { script = 1, sections = {
  [1] = function()
    story.set_trigger(1)
    story.delay(2)
    return ENDED
  end,
  [2] = function()
    story.set_trigger(2)
    return ENDED
  end,
} }
)lua";

struct PackFile {
  std::string_view path;
  std::string_view text;
};

// A pack of the tests: its id, layer, priority and files.
struct TestPack {
  std::string_view id;
  std::string_view layer;
  int priority = 0;
  std::initializer_list<PackFile> files;
};

// The base pack with script 1, and the packs over it. Not movable: the VFS points into it.
class PatchPacks {
public:
  explicit PatchPacks(std::initializer_list<TestPack> packs) {
    add({.id = "base", .layer = "base", .priority = 0, .files = {}},
        dwe::assets::PackOrigin::Engine);
    memory_.addFile("base/story/001.lua", std::format("{}{}", Header, BaseModule));
    for (const TestPack& pack : packs) {
      add(pack, dwe::assets::PackOrigin::Mod);
    }
    auto vfs = dwe::assets::VirtualFileSystem::mount(locations_);
    REQUIRE_MESSAGE(vfs.has_value(), story_play::errorText(vfs));
    files_ = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*vfs));
  }
  PatchPacks(const PatchPacks&) = delete;
  PatchPacks& operator=(const PatchPacks&) = delete;
  PatchPacks(PatchPacks&&) = delete;
  PatchPacks& operator=(PatchPacks&&) = delete;
  ~PatchPacks() = default;

  [[nodiscard]] const dwe::assets::VirtualFileSystem& files() const { return *files_; }
  // A map editor saving a file while the game runs: written, then the packs listed again.
  void write(std::string_view path, std::string_view text) {
    memory_.addFile(path, text);
    REQUIRE(files_->rescan());
  }

private:
  void add(const TestPack& pack, dwe::assets::PackOrigin origin) {
    memory_.addFile(std::format("{}/pack.yaml", pack.id),
                    std::format("id: {}\nversion: 0.1.0\nlayer: {}\npriority: {}\n", pack.id,
                                pack.layer, pack.priority));
    for (const PackFile& file : pack.files) {
      memory_.addFile(std::format("{}/{}", pack.id, file.path), std::string{file.text});
    }
    locations_.push_back(dwe::assets::PackLocation{.fileSystem = &memory_,
                                                   .root = std::string{pack.id},
                                                   .fillsGaps = false,
                                                   .origin = origin});
  }

  dwe::assets::MemoryFileSystem memory_;
  std::vector<dwe::assets::PackLocation> locations_;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files_;
};

Played play(const PatchPacks& packs, std::uint16_t section) {
  StoryPlay player{packs.files()};
  return player.play(SectionRef{.script = 1, .section = section}, 1);
}

// A file of the map editor's written story (DWE_WRITTEN_STORY): the module story/900.lua and the
// patch story/patches/mapper_900.lua it saves after editing section 0x51, with a routine of the
// module's copied into the patch. The editor's own tests check that it writes the patch so.
std::string writtenStory(std::string_view path) {
  const std::filesystem::path file = std::filesystem::path{DWE_WRITTEN_STORY} / path;
  std::ifstream in{file, std::ios::binary};
  REQUIRE_MESSAGE(in.good(), file.string());
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

} // namespace

TEST_CASE("a story patch replaces one section and leaves the module's others") {
  const PatchPacks packs{{.id = "my_mod",
                          .layer = "mods",
                          .priority = 0,
                          .files = {{.path = "story/patches/first.lua", .text = R"lua(
return { script = 1, sections = {
  [1] = function()
    story.set_trigger(10)
    return story.ENDED
  end,
} }
)lua"}}}};
  const Played patched = play(packs, 1);
  CHECK(patched.status == "ended");
  CHECK(patched.events == std::vector<std::string>{"trigger 10 = 1"});
  const Played kept = play(packs, 2);
  CHECK(kept.events == std::vector<std::string>{"trigger 2 = 1"});
}

TEST_CASE("a story patch can wrap the section it replaces, waits included") {
  const PatchPacks packs{{.id = "my_mod",
                          .layer = "mods",
                          .priority = 0,
                          .files = {{.path = "story/patches/wrap.lua", .text = R"lua(
return { script = 1, sections = {
  [1] = function(original)
    story.set_var(5, 1)
    local result = original()
    story.set_var(5, 2)
    return result
  end,
  [3] = function(original)
    story.set_var(6, original == nil and 1 or 2)
  end,
} }
)lua"}}}};
  const Played wrapped = play(packs, 1);
  CHECK(wrapped.status == "ended");
  CHECK(wrapped.events ==
        std::vector<std::string>{"var 5 = 1", "trigger 1 = 1", "delay(2)", "var 5 = 2"});
  // A section the module does not have: the patch adds it, with no original.
  const Played added = play(packs, 3);
  CHECK(added.events == std::vector<std::string>{"var 6 = 1"});
}

TEST_CASE("the patch highest in the pack stack wins a section, and the log names the loser") {
  std::size_t& shadowed = story_play::loggedLinesWith("section 0x0001 runs high_mod:");
  const std::size_t before = shadowed;
  constexpr std::string_view Low = R"lua(
return { script = 1, sections = { [1] = function() story.set_trigger(20) end,
                                  [2] = function() story.set_trigger(21) end } }
)lua";
  constexpr std::string_view High = R"lua(
return { script = 1, sections = { [1] = function() story.set_trigger(30) end } }
)lua";
  const PatchPacks packs{{.id = "high_mod",
                          .layer = "mods",
                          .priority = 5,
                          .files = {{.path = "story/patches/b.lua", .text = High}}},
                         {.id = "low_mod",
                          .layer = "mods",
                          .priority = 1,
                          .files = {{.path = "story/patches/a.lua", .text = Low}}}};
  StoryPlay player{packs.files()};
  CHECK(player.host().patchOf({.script = 1, .section = 1}) ==
        std::optional<std::string>{"high_mod:story/patches/b"});
  CHECK(player.host().patchOf({.script = 1, .section = 2}) ==
        std::optional<std::string>{"low_mod:story/patches/a"});
  CHECK_FALSE(player.host().patchOf({.script = 2, .section = 1}).has_value());
  CHECK(shadowed == before + 1);
  CHECK(player.play(SectionRef{.script = 1, .section = 1}, 1).events ==
        std::vector<std::string>{"trigger 30 = 1"});
  CHECK(player.play(SectionRef{.script = 1, .section = 2}, 1).events ==
        std::vector<std::string>{"trigger 21 = 1"});
}

TEST_CASE("a patch runs in its pack's environment") {
  const PatchPacks packs{{.id = "my_mod",
                          .layer = "mods",
                          .priority = 0,
                          .files = {{.path = "story/patches/probe.lua", .text = R"lua(
return { script = 1, sections = { [2] = function(original)
  probe.pack()
  return original()
end } }
)lua"}}}};
  StoryPlay player{packs.files()};
  std::vector<std::string> callers;
  REQUIRE(player.vm().api().publish(
      "probe", "pack",
      [&callers](const dwe::script::ApiCall& call) -> dwe::core::Result<dwe::script::ApiValue> {
        callers.emplace_back(call.pack());
        return dwe::script::ApiValue{};
      }));
  const Played played = player.play(SectionRef{.script = 1, .section = 2}, 1);
  CHECK(played.status == "ended");
  CHECK(played.events == std::vector<std::string>{"trigger 2 = 1"});
  CHECK(callers == std::vector<std::string>{"my_mod"});
}

TEST_CASE("a broken story patch is left out and the module's section runs") {
  std::size_t& noScript = story_play::loggedLinesWith("`script` must be a script id");
  std::size_t& failed = story_play::loggedLinesWith("story patch bad_mod:story/patches/syntax");
  const std::size_t noScriptBefore = noScript;
  const std::size_t failedBefore = failed;
  const PatchPacks packs{
      {.id = "bad_mod",
       .layer = "mods",
       .priority = 0,
       .files = {{.path = "story/patches/no_script.lua",
                  .text = "return { sections = { [2] = function() story.set_trigger(40) end } }"},
                 {.path = "story/patches/syntax.lua", .text = "return { script = "}}}};
  const Played played = play(packs, 2);
  CHECK(played.events == std::vector<std::string>{"trigger 2 = 1"});
  CHECK(noScript == noScriptBefore + 1);
  CHECK(failed == failedBefore + 1);
}

TEST_CASE("reloading the scripts reads edited modules and new patches, and keeps the state") {
  PatchPacks packs{{.id = "my_mod", .layer = "mods", .priority = 0, .files = {}}};
  StoryPlay player{packs.files()};
  CHECK(player.play(SectionRef{.script = 1, .section = 2}, 1).events ==
        std::vector<std::string>{"trigger 2 = 1"});

  // The base module's section 2 edited and a patch of section 1 added: until the reload, the
  // loaded ones run.
  packs.write("base/story/001.lua", std::format("{}{}", Header, R"lua(
return { script = 1, sections = { [2] = function()
  story.set_trigger(7)
  return story.ENDED
end } }
)lua"));
  packs.write("my_mod/story/patches/new.lua", R"lua(
return { script = 1, sections = { [1] = function()
  story.set_trigger(8)
  return story.ENDED
end } }
)lua");
  CHECK(player.play(SectionRef{.script = 1, .section = 2}, 1).events ==
        std::vector<std::string>{"trigger 2 = 1"});

  REQUIRE(player.host().reloadScripts());
  CHECK(player.play(SectionRef{.script = 1, .section = 2}, 1).events ==
        std::vector<std::string>{"trigger 7 = 1"});
  CHECK(player.play(SectionRef{.script = 1, .section = 1}, 1).events ==
        std::vector<std::string>{"trigger 8 = 1"});
  CHECK(player.host().patchOf({.script = 1, .section = 1}) ==
        std::optional<std::string>{"my_mod:story/patches/new"});
  // The story's state is the host's caller's: the triggers set before the reload stay set.
  CHECK(player.state().trigger(2).value_or(false));
}

TEST_CASE("the scripts do not reload while a section runs") {
  const PatchPacks packs{};
  StoryPlay player{packs.files()};
  auto handle = player.host().start(SectionRef{.script = 1, .section = 1});
  REQUIRE(handle.has_value());
  const auto refused = player.host().reloadScripts();
  REQUIRE_FALSE(refused.has_value());
  CHECK(refused.error().message == "a story section is running");
  player.host().close(*handle);
  CHECK(player.host().reloadScripts());
}

TEST_CASE("a patch the map editor wrote runs its own copy of the module's shared routine") {
  const std::string module = writtenStory("story/900.lua");
  const std::string patch = writtenStory("story/patches/mapper_900.lua");
  const PatchPacks packs{{.id = "mapper",
                          .layer = "mods",
                          .priority = 5,
                          .files = {{.path = "story/patches/mapper_900.lua", .text = patch}}},
                         {.id = "story_900",
                          .layer = "mods",
                          .priority = 1,
                          .files = {{.path = "story/900.lua", .text = module}}}};
  StoryPlay player{packs.files()};
  for (const std::string_view name : {"set_dialog_owner", "show_textbox"}) {
    REQUIRE(player.vm().api().publish(
        "story", name,
        [&player,
         name](const dwe::script::ApiCall& call) -> dwe::core::Result<dwe::script::ApiValue> {
          const auto argument = call.arguments().empty()
                                    ? std::string{}
                                    : story_play::textOf(call.arguments().front());
          player.recorder().record(std::format("{}({})", name, argument));
          return dwe::script::ApiValue{};
        }));
  }
  CHECK(player.host().patchOf({.script = 900, .section = 0x51}) ==
        std::optional<std::string>{"mapper:story/patches/mapper_900"});
  CHECK_FALSE(player.host().patchOf({.script = 900, .section = 0x52}).has_value());

  const Played open = player.play(SectionRef{.script = 900, .section = 0x51}, 1);
  CHECK(open.status == "ended");
  CHECK(open.events == std::vector<std::string>{"set_dialog_owner(5)",
                                                "show_textbox(story_900.text.0200)",
                                                "trigger 73 = 1", "var 5 = 2"});

  REQUIRE(player.state().setTrigger(72, true));
  REQUIRE(player.state().setTrigger(73, false));
  const Played shut = player.play(SectionRef{.script = 900, .section = 0x51}, 1);
  CHECK(shut.status == "ended");
  CHECK(shut.events.empty());
  CHECK_FALSE(player.state().trigger(73).value_or(true));

  // The section the patch leaves: the module's own routine, not the patch's copy.
  const Played kept = player.play(SectionRef{.script = 900, .section = 0x52}, 1);
  CHECK(kept.status == "ended");
  CHECK(kept.events == std::vector<std::string>{"trigger 74 = 1", "var 5 = 1"});
  CHECK(player.state().variable(5).value_or(0) == 1);
}

TEST_CASE("the walks read the map editor's patch, its own routine composed under a name of its") {
  namespace script = dwe::formats::script;
  const std::string module = writtenStory("story/900.lua");
  const auto patch =
      script::readLuaPatch(writtenStory("story/patches/mapper_900.lua"), "mapper_900");
  REQUIRE_MESSAGE(patch.has_value(), story_play::errorText(patch));
  REQUIRE(patch->routines.size() == 1);
  CHECK(patch->routines.front().name == "at_0100");
  const std::vector<script::LuaPatch> patches{*patch};
  const auto composed = script::composeLuaPatches(module, 900, patches);
  REQUIRE_MESSAGE(composed.has_value(), story_play::errorText(composed));
  CHECK(composed->code.contains("function routines.patch1_at_0100()"));
  CHECK(composed->code.contains("      return routines.patch1_at_0100()\n"));
  CHECK(composed->code.contains("    [0x0051] = routines.patch_0051,\n"
                                "    [0x0052] = routines.at_0080,\n"));
  const auto read = script::readLuaModule(composed->code);
  REQUIRE_MESSAGE(read.has_value(), composed->located(story_play::errorText(read)));
  CHECK(read->script == 900);
  CHECK(read->listing.sections.size() == 2);
}

TEST_CASE("a section a tool sends runs once in place of the packs' and says where it waits") {
  const PatchPacks packs{};
  StoryPlay player{packs.files()};
  constexpr std::string_view Sent = R"lua(return { script = 1, sections = {
  [1] = function(original)
    story.set_trigger(30)
    story.delay(3)
    return story.ENDED
  end,
} }
)lua";
  REQUIRE(player.host().stageSection({.script = 1, .section = 1}, Sent));
  CHECK(player.host().hasStagedSection());
  // Sent code runs where its line is: the delay waits on line 4 of the source.
  auto handle = player.host().start(SectionRef{.script = 1, .section = 1});
  REQUIRE(handle.has_value());
  CHECK_FALSE(player.host().hasStagedSection());
  static_cast<void>(player.host().resume(*handle, {}));
  CHECK(player.host().lines(*handle) == std::vector<std::int32_t>{4});
  CHECK_FALSE(player.host().stageSection({.script = 1, .section = 1}, Sent));
  player.host().close(*handle);
  // Once: the next start runs the module's section again.
  CHECK(play(packs, 1).events == std::vector<std::string>{"trigger 1 = 1", "delay(2)"});

  REQUIRE(player.host().stageSection({.script = 1, .section = 1}, Sent));
  const Played sent = player.play(SectionRef{.script = 1, .section = 1}, 1);
  CHECK(sent.events == std::vector<std::string>{"trigger 30 = 1", "delay(3)"});
}

TEST_CASE("a sent section that does not load, is for another script or lacks it is refused") {
  const PatchPacks packs{};
  StoryPlay player{packs.files()};
  CHECK_FALSE(player.host().stageSection({.script = 1, .section = 1}, "return {"));
  CHECK_FALSE(player.host().stageSection(
      {.script = 1, .section = 1}, "return { script = 2, sections = { [1] = function() end } }"));
  CHECK_FALSE(player.host().stageSection(
      {.script = 1, .section = 1}, "return { script = 1, sections = { [2] = function() end } }"));
  CHECK_FALSE(player.host().stageSection(
      {.script = 1, .section = 1}, "return { script = 1, sections = { [1] = function() end } }",
      "no_such_pack"));
  CHECK_FALSE(player.host().hasStagedSection());
}
