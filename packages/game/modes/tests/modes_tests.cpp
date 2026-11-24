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

using dwe::game::modes::ClassicMode;
using dwe::game::modes::currentModeId;
using dwe::game::modes::DefaultMode;
using dwe::game::modes::FolderMode;
using dwe::game::modes::GameMode;
using dwe::game::modes::ModeCatalog;
using dwe::game::modes::OptionPin;
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
                          .order = 10,
                          .options = {}});
}

TEST_CASE("a mode manifest may offer randomized runs") {
  const auto mode = parseGameMode(std::string{ArenaMode} + "randomizer: true\n", "arena");
  REQUIRE_MESSAGE(mode.has_value(), errorOf(mode));
  CHECK(mode->randomizer);
  const auto plain = parseGameMode(ArenaMode, "arena");
  REQUIRE(plain.has_value());
  CHECK_FALSE(plain->randomizer);
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

TEST_CASE("a mode manifest may name its battle system and the ones its player may switch to") {
  GameMode plain = *parseGameMode(ArenaMode, "arena");
  CHECK(plain.battle == "classic");
  CHECK(plain.battles == std::vector<std::string>{"classic"});
  const auto folder = parseGameMode(
      std::string{ArenaMode} + "battle: folder\nbattles: [folder, classic]\n", "arena");
  REQUIRE_MESSAGE(folder.has_value(), errorOf(folder));
  CHECK(folder->battle == "folder");
  CHECK(folder->battles == std::vector<std::string>{"folder", "classic"});
  const auto alone = parseGameMode(std::string{ArenaMode} + "battle: folder\n", "arena");
  REQUIRE(alone.has_value());
  CHECK(alone->battles == std::vector<std::string>{"folder"});
  CHECK(errorOf(parseGameMode(std::string{ArenaMode} + "battle: turns\n", "arena"))
            .contains("no battle system"));
  CHECK(errorOf(
            parseGameMode(std::string{ArenaMode} + "battle: folder\nbattles: [classic]\n", "arena"))
            .contains("not one of its battles"));
  CHECK(errorOf(parseGameMode(std::string{ArenaMode} + "battles: [classic, classic]\n", "arena"))
            .contains("listed twice"));
}

TEST_CASE("a mode manifest may fix options of the options page at one value each") {
  std::string fixed{ArenaMode};
  fixed += "options:\n  difficulty: normal\n  rare_items: false\n  gym_sessions: 1\n";
  const auto mode = parseGameMode(fixed, "arena");
  REQUIRE_MESSAGE(mode.has_value(), errorOf(mode));
  CHECK(mode->options == std::vector<OptionPin>{{.option = "difficulty", .value = "normal"},
                                                {.option = "rare_items", .value = "false"},
                                                {.option = "gym_sessions", .value = "1"}});
  CHECK(parseGameMode(ArenaMode, "arena")->options.empty());
}

TEST_CASE("a mode's fixed options must be a mapping of option names to one value") {
  CHECK(errorOf(parseGameMode(std::string{ArenaMode} + "options: [difficulty]\n", "arena"))
            .contains("options must be a mapping"));
  CHECK(errorOf(parseGameMode(std::string{ArenaMode} + "options:\n  Bad: true\n", "arena"))
            .contains("is not an option name"));
  CHECK(errorOf(parseGameMode(std::string{ArenaMode} + "options:\n  difficulty: [a, b]\n", "arena"))
            .contains("must be fixed to one value"));
}

TEST_CASE("the game pack ships the two variants, Classic Enhanced first, and Folder Mode is the "
          "default") {
  const Packs packs{};
  const ModeCatalog catalog = ModeCatalog::load(packs.files());
  REQUIRE(catalog.modes().size() == 2);
  CHECK(catalog.skipped().empty());
  CHECK(catalog.asksMode());
  const GameMode& classic = catalog.modes()[0];
  CHECK(classic.id == ClassicMode);
  CHECK(classic.ruleset == "original");
  CHECK(classic.rulesets == std::vector<std::string>{"original"});
  CHECK(classic.battle == "classic");
  CHECK(classic.battles == std::vector<std::string>{"classic"});
  CHECK(classic.options ==
        std::vector<OptionPin>{{.option = "difficulty", .value = "normal"},
                               {.option = "rare_items", .value = "false"},
                               {.option = "free_exploration", .value = "false"},
                               {.option = "extra_toilets", .value = "false"},
                               {.option = "choose_egg_after_lives_lost", .value = "false"},
                               {.option = "boosted_techniques", .value = "false"},
                               {.option = "roaming_rare_chance", .value = "original"}});
  CHECK_FALSE(classic.randomizer);
  const GameMode* folder = catalog.defaultMode();
  REQUIRE(folder != nullptr);
  CHECK(folder->id == FolderMode);
  CHECK(folder == &catalog.modes()[1]);
  CHECK(folder->ruleset == "original");
  CHECK(folder->rulesets == std::vector<std::string>{"original", "oculto", "oculto_mega", "todas",
                                                     "todas_oculto", "todas_oculto_mega"});
  CHECK(folder->options.empty());
  CHECK(folder->randomizer);
  CHECK(folder->battle == "folder");
  CHECK(folder->battles == std::vector<std::string>{"folder", "classic"});
}

TEST_CASE("a Folder Mode save takes progress from a Classic Enhanced world, not the other way") {
  const Packs packs{};
  const ModeCatalog catalog = ModeCatalog::load(packs.files());
  const GameMode* classic = catalog.find(ClassicMode);
  const GameMode* folder = catalog.find(FolderMode);
  REQUIRE(classic != nullptr);
  REQUIRE(folder != nullptr);
  CHECK(takesProgressFrom(*classic, *classic));
  CHECK(takesProgressFrom(*folder, *folder));
  CHECK(takesProgressFrom(*folder, *classic));
  CHECK_FALSE(takesProgressFrom(*classic, *folder));
  GameMode narrow = *folder;
  narrow.id = "narrow";
  narrow.rulesets = {"original", "oculto"};
  CHECK_FALSE(takesProgressFrom(narrow, *folder));
  CHECK(takesProgressFrom(narrow, *classic));
}

TEST_CASE("a save from before the variants plays Folder Mode") {
  CHECK(DefaultMode == FolderMode);
  CHECK(currentModeId("original") == FolderMode);
  CHECK(currentModeId("") == FolderMode);
  CHECK(currentModeId("classic") == ClassicMode);
  CHECK(currentModeId("arena") == "arena");
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
  REQUIRE(catalog.modes().size() == 3);
  CHECK(catalog.modes()[0].id == ClassicMode);
  CHECK(catalog.modes()[1].id == FolderMode);
  CHECK(catalog.modes()[2].id == "arena");
  CHECK(catalog.asksMode());
  CHECK(catalog.find("arena") != nullptr);
  CHECK(catalog.find("broken") == nullptr);
  REQUIRE(catalog.skipped().size() == 2);
  CHECK(catalog.skipped()[0].id == "broken");
  CHECK(catalog.skipped()[1].reason == "mode 'lost': no pack has the ruleset 'nowhere'");
}

TEST_CASE("without Folder Mode the default mode is the first of the list") {
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
