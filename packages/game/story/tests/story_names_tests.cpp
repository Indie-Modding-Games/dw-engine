#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/story/story_names.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using dwe::game::story::parseStoryNames;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("story names label triggers and variables by number") {
  const auto names = parseStoryNames("triggers:\n  45: item_box\n  799: last_one\n"
                                     "variables:\n  0: first_var\n");
  REQUIRE_MESSAGE(names.has_value(), errorOf(names));
  REQUIRE(names->trigger(45) != nullptr);
  CHECK(*names->trigger(45) == "item_box");
  CHECK(*names->trigger(799) == "last_one");
  CHECK(names->trigger(46) == nullptr);
  REQUIRE(names->variable(0) != nullptr);
  CHECK(*names->variable(0) == "first_var");
  // Both kinds are optional, and one name may label a trigger and a variable.
  CHECK(parseStoryNames("variables:\n  1: only_var\n").has_value());
  CHECK(parseStoryNames("triggers:\n  1: same\nvariables:\n  1: same\n").has_value());
}

TEST_CASE("story names refuse numbers past the state, bad ids and repeated names") {
  CHECK(errorOf(parseStoryNames("triggers:\n  800: too_far\n"))
            .contains("triggers: '800' is not a number from 0 to 799"));
  CHECK(errorOf(parseStoryNames("variables:\n  256: too_far\n"))
            .contains("variables: '256' is not a number from 0 to 255"));
  CHECK(errorOf(parseStoryNames("variables:\n  0x10: hex\n")).contains("'0x10'"));
  CHECK(errorOf(parseStoryNames("triggers:\n  3: Not An Id\n")).contains("is not a name id"));
  CHECK(errorOf(parseStoryNames("triggers:\n  3: twin\n  4: twin\n"))
            .contains("the name 'twin' is given twice"));
  CHECK_FALSE(parseStoryNames("flags:\n  3: other\n").has_value());
}

TEST_CASE("the game pack's story names load, and without them there are none") {
  const dwe::assets::DiskFileSystem disk;
  const auto bytes = disk.readFile(std::filesystem::path{DWE_GAME_PACK} / "story/names.yaml");
  REQUIRE(bytes.has_value());
  const auto names = parseStoryNames(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  REQUIRE_MESSAGE(names.has_value(), errorOf(names));
  CHECK_FALSE(names->triggers.empty());

  dwe::assets::MemoryFileSystem memory;
  memory.addFile("game/pack.yaml", "id: game\nversion: 0.1.0\nlayer: base\n");
  const std::vector<std::filesystem::path> packs{"game"};
  const auto vfs = dwe::assets::VirtualFileSystem::mount(memory, packs);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  const auto none = dwe::game::story::loadStoryNames(*vfs);
  REQUIRE_MESSAGE(none.has_value(), errorOf(none));
  CHECK(none->triggers.empty());
  CHECK(none->variables.empty());
}
