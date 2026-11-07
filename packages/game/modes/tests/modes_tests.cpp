#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/modes/game_mode.hpp>
#include <dwe/game/modes/mode_catalog.hpp>

#include <doctest/doctest.h>

#include <array>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using dwe::game::modes::GameMode;
using dwe::game::modes::ModeCatalog;
using dwe::game::modes::OriginalMode;
using dwe::game::modes::parseGameMode;

namespace {

constexpr std::string_view ArenaMode = R"(id: arena
name: my_mod.arena_name
description: my_mod.arena_description
ruleset: arena_rules
rulesets: [arena_rules]
order: 10
)";

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

// The game pack over a base pack of these files, in memory.
class Packs {
public:
  explicit Packs(std::initializer_list<std::pair<std::string_view, std::string_view>> files) {
    memory_.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
    for (const auto& [path, text] : files) {
      memory_.addFile("base/" + std::string{path}, text);
    }
    const std::array<dwe::assets::PackLocation, 2> packs{
        dwe::assets::PackLocation{.fileSystem = &memory_,
                                  .root = "base",
                                  .fillsGaps = false,
                                  .origin = dwe::assets::PackOrigin::Engine},
        dwe::assets::PackLocation{.fileSystem = &disk_,
                                  .root = DWE_GAME_PACK,
                                  .fillsGaps = false,
                                  .origin = dwe::assets::PackOrigin::Engine}};
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

} // namespace

TEST_CASE("a mode manifest names its id, texts, rulesets and order") {
  const auto mode = parseGameMode(ArenaMode, "arena");
  REQUIRE_MESSAGE(mode.has_value(), errorOf(mode));
  CHECK(*mode == GameMode{.id = "arena",
                          .nameKey = "my_mod.arena_name",
                          .descriptionKey = "my_mod.arena_description",
                          .ruleset = "arena_rules",
                          .rulesets = {"arena_rules"},
                          .order = 10});
}

TEST_CASE("a mode manifest with a wrong id, a missing key or a ruleset it does not list fails") {
  CHECK(errorOf(parseGameMode(ArenaMode, "other")).contains("must be its folder's, 'other'"));
  std::string missing{ArenaMode};
  missing.erase(missing.find("order"));
  CHECK(errorOf(parseGameMode(missing, "arena")).contains("order"));
  std::string unlisted{ArenaMode};
  unlisted.replace(unlisted.find("[arena_rules]"), 13, "[other_rules]");
  CHECK(errorOf(parseGameMode(unlisted, "arena")).contains("must also be one of rulesets"));
  std::string twice{ArenaMode};
  twice.replace(twice.find("[arena_rules]"), 13, "[arena_rules, arena_rules]");
  CHECK(errorOf(parseGameMode(twice, "arena")).contains("listed twice"));
  std::string badId{ArenaMode};
  badId.replace(0, 9, "id: Arena");
  CHECK(errorOf(parseGameMode(badId, "Arena")).contains("is not a mode id"));
}

TEST_CASE("the game pack ships the original story as its only mode, and it is the default") {
  const Packs packs{};
  const ModeCatalog catalog = ModeCatalog::load(packs.files());
  REQUIRE(catalog.modes().size() == 1);
  CHECK(catalog.skipped().empty());
  const GameMode* original = catalog.defaultMode();
  REQUIRE(original != nullptr);
  CHECK(original->id == OriginalMode);
  CHECK(original->ruleset == "original");
  CHECK(original->rulesets == std::vector<std::string>{"original", "oculto", "oculto_mega"});
  CHECK_FALSE(catalog.asksMode());
}

TEST_CASE("a pack adds a mode, the list sorts by order, and a broken mode is left out") {
  const Packs packs{{"modes/arena/mode.yaml", ArenaMode},
                    {"rules/arena_rules/ruleset.yaml", "id: arena_rules\n"},
                    {"modes/broken/mode.yaml", "id: broken\n"},
                    {"modes/lost/mode.yaml", R"(id: lost
name: a
description: b
ruleset: nowhere
rulesets: [nowhere]
order: 1
)"}};
  const ModeCatalog catalog = ModeCatalog::load(packs.files());
  REQUIRE(catalog.modes().size() == 2);
  CHECK(catalog.modes()[0].id == OriginalMode);
  CHECK(catalog.modes()[1].id == "arena");
  CHECK(catalog.asksMode());
  CHECK(catalog.find("arena") != nullptr);
  CHECK(catalog.find("broken") == nullptr);
  REQUIRE(catalog.skipped().size() == 2);
  CHECK(catalog.skipped()[0].id == "broken");
  CHECK(catalog.skipped()[1].reason == "mode 'lost': no pack has the ruleset 'nowhere'");
}

TEST_CASE("without the original story the default mode is the first of the list") {
  const auto second = [](std::string id) {
    GameMode mode;
    mode.id = std::move(id);
    mode.order = 2;
    return mode;
  };
  const ModeCatalog catalog{std::vector<GameMode>{second("b"), second("a")}};
  REQUIRE(catalog.defaultMode() != nullptr);
  CHECK(catalog.defaultMode()->id == "a");
  CHECK(ModeCatalog{std::vector<GameMode>{}}.defaultMode() == nullptr);
}
