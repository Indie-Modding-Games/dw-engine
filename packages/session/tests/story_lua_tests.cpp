#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/story_api_names.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_lua.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "story_lua_fixture.hpp"

using story_play::Played;
using story_play::SectionRef;
using story_play::StoryPlay;

namespace {

constexpr std::string_view Header = R"lua(-- dwx lua 3: a module of the tests
local story <const> = story
local ENDED <const> = story.ENDED
local routines = {}
)lua";

using Modules = std::initializer_list<std::pair<std::uint16_t, std::string_view>>;

// Story modules in the base pack and in a mod over it, by script id. Not movable: the VFS points
// into it.
class StoryPacks {
public:
  explicit StoryPacks(Modules base, Modules mod = {}) {
    add("base", "base", base);
    std::vector<dwe::assets::PackLocation> packs{
        dwe::assets::PackLocation{.fileSystem = &memory_,
                                  .root = "base",
                                  .fillsGaps = false,
                                  .origin = dwe::assets::PackOrigin::Engine}};
    if (mod.size() > 0) {
      add("my_mod", "mods", mod);
      packs.push_back(dwe::assets::PackLocation{.fileSystem = &memory_,
                                                .root = "my_mod",
                                                .fillsGaps = false,
                                                .origin = dwe::assets::PackOrigin::Mod});
    }
    auto vfs = dwe::assets::VirtualFileSystem::mount(packs);
    REQUIRE_MESSAGE(vfs.has_value(), story_play::errorText(vfs));
    files_ = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*vfs));
  }
  StoryPacks(const StoryPacks&) = delete;
  StoryPacks& operator=(const StoryPacks&) = delete;
  StoryPacks(StoryPacks&&) = delete;
  StoryPacks& operator=(StoryPacks&&) = delete;
  ~StoryPacks() = default;

  [[nodiscard]] const dwe::assets::VirtualFileSystem& files() const { return *files_; }

private:
  void add(std::string_view pack, std::string_view layer, Modules modules) {
    memory_.addFile(std::format("{}/pack.yaml", pack),
                    std::format("id: {}\nversion: 0.1.0\nlayer: {}\n{}", pack, layer,
                                layer == "base" ? "" : "api: \"1.x\"\n"));
    for (const auto& [script, body] : modules) {
      memory_.addFile(std::format("{}/story/{:03}.lua", pack, script),
                      std::format("{}{}", Header, body));
    }
  }

  dwe::assets::MemoryFileSystem memory_;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files_;
};

Played playSection(std::string_view body, std::uint32_t seed = 1) {
  const StoryPacks packs{{{1, body}}};
  StoryPlay play{packs.files()};
  return play.play(SectionRef{.script = 1, .section = 1}, seed);
}

} // namespace

TEST_CASE("the story orders write the state and delay waits whole frames") {
  const Played played = playSection(R"lua(
function routines.at_0001()
  story.set_trigger(5)
  story.unset_trigger(6)
  story.set_var(3, 300)
  story.add_to_var(3, 10)
  story.reduce_var(4, 1)
  story.copy_var(3, 7)
  if story.trigger(5) and not story.trigger(6) and story.var(7) == 255 then
    story.delay(0)
    story.delay(3)
  end
  return ENDED
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua");
  CHECK(played.status == "ended");
  CHECK(played.events == std::vector<std::string>{"trigger 5 = 1", "trigger 6 = 0", "var 3 = 255",
                                                  "var 3 = 255", "var 4 = 0", "var 7 = 255",
                                                  "delay(1)", "delay(3)"});
}

TEST_CASE("store_random draws as the original's randomLimit, with the session's numbers") {
  const Played played = playSection(R"lua(
function routines.at_0001()
  story.store_random(2, 8)
  story.store_random(2, 300)
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua",
                                    7);
  story_play::SeededRandom random;
  random.reset(7);
  const std::int64_t first = (9 * random.next()) >> 15;
  const std::int64_t second = (256 * random.next()) >> 15;
  CHECK(played.events == std::vector<std::string>{std::format("var 2 = {}", first),
                                                  std::format("var 2 = {}", second)});
  // A section that returns with nothing to return to is an error of the runner.
  CHECK(played.status == "returned");
}

TEST_CASE("a tail jump_to_file loops between two scripts without holding the sections it left") {
  const StoryPacks packs{{{1, R"lua(
function routines.at_0001()
  story.add_to_var(1, 1)
  if story.var(1) >= 30 then
    return ENDED
  end
  return story.jump_to_file(2, 4)
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua"},
                          {2, R"lua(
function routines.at_0004()
  return story.jump_to_file(1, 1)
end
return { script = 2, sections = { [4] = routines.at_0004 } }
)lua"}}};
  StoryPlay play{packs.files()};
  const Played played = play.play(SectionRef{.script = 1, .section = 1}, 1);
  CHECK(played.status == "ended");
  CHECK(played.fileJumps == 58);
  CHECK(played.deepestLink == 0);
  CHECK(play.state().variable(1).value_or(0) == 30);
}

TEST_CASE("a jump_to_file from a subroutine goes on in its caller when the target returns") {
  const StoryPacks packs{{{1, R"lua(
function routines.at_0010()
  return story.jump_to_file(2, 1)
end

function routines.at_0001()
  if routines.at_0010() == ENDED then
    return ENDED
  end
  story.set_trigger(9)
  return ENDED
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua"},
                          {2, R"lua(
function routines.at_0001()
  story.set_trigger(8)
end
return { script = 2, sections = { [1] = routines.at_0001 } }
)lua"}}};
  StoryPlay play{packs.files()};
  const Played played = play.play(SectionRef{.script = 1, .section = 1}, 1);
  CHECK(played.events ==
        std::vector<std::string>{"jump_to_file(2,1)", "trigger 8 = 1", "trigger 9 = 1"});
  CHECK(played.status == "ended");
  CHECK(played.deepestLink == 1);
}

TEST_CASE("a mod's story module replaces the base one and runs in the mod's environment") {
  const StoryPacks packs{{{1, R"lua(
function routines.at_0001()
  story.set_trigger(1)
  return ENDED
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua"}},
                         {{1, R"lua(
function routines.at_0001()
  probe.pack()
  story.set_trigger(2)
  return ENDED
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua"}}};
  StoryPlay play{packs.files()};
  std::vector<std::string> callers;
  REQUIRE(play.vm().api().publish(
      "probe", "pack",
      [&callers](const dwe::script::ApiCall& call) -> dwe::core::Result<dwe::script::ApiValue> {
        callers.emplace_back(call.pack());
        return dwe::script::ApiValue{};
      }));
  const Played played = play.play(SectionRef{.script = 1, .section = 1}, 1);
  CHECK(played.status == "ended");
  CHECK(played.events == std::vector<std::string>{"trigger 2 = 1"});
  CHECK(callers == std::vector<std::string>{"my_mod"});
}

TEST_CASE("an op the story API does not publish stops its section with a Lua error") {
  const Played played = playSection(R"lua(
function routines.at_0001()
  story.set_trigger(1)
  story.show_textbox("story_001.text.0010")
  story.set_trigger(2)
  return ENDED
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua");
  CHECK(played.status == "error");
  CHECK(played.failure.contains("show_textbox"));
  CHECK(played.events == std::vector<std::string>{"trigger 1 = 1"});
}

TEST_CASE("a section gives back story.ENDED or nothing, and another value is an error") {
  const Played played = playSection(R"lua(
function routines.at_0001()
  return 1
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua");
  CHECK(played.status == "error");
  CHECK(played.failure == dwe::session::messages::BadSectionReturn);
}

TEST_CASE("a script no pack has and a jump past the section ids are errors") {
  const Played missing = playSection(R"lua(
function routines.at_0001()
  return story.jump_to_file(3, 1)
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua");
  CHECK(missing.status == "error");
  CHECK(missing.failure.contains("story/003"));
  const Played outside = playSection(R"lua(
function routines.at_0001()
  return story.jump_to_file(2, 65536)
end
return { script = 1, sections = { [1] = routines.at_0001 } }
)lua");
  CHECK(outside.status == "error");
  CHECK(outside.failure.contains("65536"));
}

TEST_CASE("the story namespace holds exactly the published story API") {
  const StoryPacks packs{{}};
  StoryPlay play{packs.files()};
  std::vector<std::string> expected(dwe::session::story_api::Published.begin(),
                                    dwe::session::story_api::Published.end());
  std::ranges::sort(expected);
  CHECK(play.vm().api().publishedNames(dwe::session::StoryNamespace) == expected);
}

TEST_CASE("the story API publishes the names the Lua emitter writes, free of forbidden words") {
  std::set<std::string_view> opNames;
  for (unsigned code = 0; code <= 0xFF; ++code) {
    const std::string_view op = dwe::formats::script::opcodeName(static_cast<std::uint8_t>(code));
    if (!op.empty()) {
      opNames.insert(dwe::formats::script::storyFunctionName(op));
    }
  }
  namespace api = dwe::session::story_api;
  // The values and the reads are not ops; the extracted modules show the emitter writes them
  // (story_lua_disc_tests.cpp).
  const std::set<std::string_view> notOps{api::Ended, api::Trigger, api::Variable};
  for (const std::string_view name : api::Published) {
    CAPTURE(name);
    CHECK_FALSE(dwe::formats::script::forbiddenWordIn(name).has_value());
    CHECK((notOps.contains(name) || opNames.contains(name)));
  }
  CHECK(std::set<std::string_view>(api::Published.begin(), api::Published.end()).size() ==
        api::Published.size());
  for (const std::string_view name : api::PropPublished) {
    CAPTURE(name);
    CHECK(opNames.contains(name));
  }
}
