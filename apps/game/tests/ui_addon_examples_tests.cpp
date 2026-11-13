// The example addons (examples/ui-addons/, docs/modding/interfaces.md): each one reads, suits this
// UI API, uses only names the UI contract declares and texts the game has, and goes on in the
// game's UI without a script error, as a player who copies its folder, or a .zip of it, would see
// it.
#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/content/user_ui_addons.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/test_support/zip_writer.hpp>
#include <dwe/ui/ui.hpp>
#include <dwe/ui/ui_addon.hpp>
#include <dwe/ui/ui_contract.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui_addon_setting.hpp"
#include "ui_contract.hpp"

namespace dwe::app {
namespace {

std::string readText(const std::filesystem::path& path) {
  std::ifstream stream{path, std::ios::binary};
  REQUIRE_MESSAGE(stream.good(), path.string());
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

// Every example: a folder of examples/ui-addons/, sorted.
std::vector<std::filesystem::path> examples() {
  std::vector<std::filesystem::path> folders;
  for (const auto& entry : std::filesystem::directory_iterator{DWE_UI_EXAMPLES_DIR}) {
    if (entry.is_directory()) {
      folders.push_back(entry.path());
    }
  }
  std::ranges::sort(folders);
  return folders;
}

// The errors and warnings written to the log since the last clear().
class Problems {
public:
  Problems() {
    static const bool added = [] {
      core::addLogSink([](core::LogLevel level, std::string_view message) {
        if (level == core::LogLevel::Warning || level == core::LogLevel::Error) {
          lines().emplace_back(message);
        }
      });
      return true;
    }();
    static_cast<void>(added);
    clear();
  }

  static void clear() { lines().clear(); }
  [[nodiscard]] static std::string joined() {
    std::string text;
    for (const std::string& line : lines()) {
      text += line + "\n";
    }
    return text;
  }

private:
  static std::vector<std::string>& lines() {
    static std::vector<std::string> written;
    return written;
  }
};

// How a player installs an example: its folder, or a .zip of it with addon.yaml at the root or
// with the folder in it.
enum class Copied : std::uint8_t {
  Folder,
  ZipRoot,
  ZipFolder,
};

// The game pack and one example copied where a player puts it: ui/addons/<folder>/ of a mod pack,
// or a .zip in ui/addons/ of the user data folder (content::UserUiAddonsFileSystem).
struct Installed {
  assets::MemoryFileSystem memory;
  test_support::TempFolder userData;
  std::unique_ptr<content::UserUiAddonsFileSystem> userAddons;
  std::unique_ptr<assets::VirtualFileSystem> files;
  std::unique_ptr<localization::Catalog> texts;

  explicit Installed(const std::filesystem::path& example, Copied copied = Copied::Folder) {
    const auto copy = [this](const std::filesystem::path& from, const std::filesystem::path& to) {
      for (const auto& entry : std::filesystem::recursive_directory_iterator{from}) {
        if (entry.is_regular_file()) {
          memory.addFile(to / entry.path().lexically_relative(from), readText(entry.path()));
        }
      }
    };
    copy(DWE_GAME_PACK, "packs/game");
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    const auto location = [this](std::string_view root, assets::PackOrigin origin) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = origin};
    };
    std::vector locations{location("packs/base", assets::PackOrigin::Engine),
                          location("packs/game", assets::PackOrigin::Engine)};
    if (copied == Copied::Folder) {
      copy(example, std::filesystem::path{"packs/addons/ui/addons"} / example.filename());
      memory.addFile("packs/addons/pack.yaml",
                     "id: addons\nversion: 0.1.0\nlayer: mods\npriority: 0\n");
      locations.push_back(location("packs/addons", assets::PackOrigin::Mod));
    } else {
      const std::string prefix =
          copied == Copied::ZipFolder ? example.filename().string() + "/" : std::string{};
      test_support::writeZip(userData.path() / "ui/addons" /
                                 std::format("{}.zip", example.filename().string()),
                             test_support::zipFilesOf(example, prefix));
      userAddons = std::make_unique<content::UserUiAddonsFileSystem>(userData.path());
      REQUIRE(userAddons->leftOutArchives().empty());
      REQUIRE(userAddons->hasAddons());
      locations.push_back(assets::PackLocation{.fileSystem = userAddons.get(),
                                               .root = userData.path(),
                                               .origin = assets::PackOrigin::Mod});
    }
    auto mounted = assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
    auto catalog = localization::Catalog::load(*files, "es");
    REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
    texts = std::make_unique<localization::Catalog>(std::move(*catalog));
  }

  [[nodiscard]] std::unique_ptr<ui::Ui> createUi() const {
    auto created = ui::Ui::create(ui::UiDesc{.files = files.get(),
                                             .texts = texts.get(),
                                             .width = 1280,
                                             .height = 720,
                                             .clock = {},
                                             .scripts = {},
                                             .cursor = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    return std::move(*created);
  }
};

// The one addon an example installs.
ui::UiAddon readExample(const Installed& installed, const std::filesystem::path& example) {
  const auto found = ui::findUiAddons(*installed.files);
  REQUIRE(found.size() == 1);
  CHECK(found[0].folder == std::format("ui/addons/{}", example.filename().string()));
  REQUIRE_MESSAGE(found[0].addon.has_value(), found[0].addon.error().message);
  return *found[0].addon;
}

bool declaredAnywhere(ui::UiContractKind kind, std::string_view name) {
  return std::ranges::any_of(gameUiContract(), [&](const ui::UiContractEntry& entry) {
    return entry.kind == kind && ui::matchesContractName(entry.name, name);
  });
}

} // namespace

TEST_CASE("there are example addons: a whole UI and a plugin") {
  bool whole = false;
  bool plugin = false;
  for (const std::filesystem::path& example : examples()) {
    const Installed installed{example};
    const ui::UiAddon addon = readExample(installed, example);
    whole = whole || addon.type == ui::UiAddonType::Ui;
    plugin = plugin || addon.type == ui::UiAddonType::Plugin;
  }
  CHECK(whole);
  CHECK(plugin);
}

TEST_CASE("every example addon reads, suits this UI API and uses only what the contract declares") {
  const std::regex call{R"(ui\.(setting|send|report)\(\s*["']([^"']+)["']\s*[,)])"};
  const std::regex api{
      R"(\b(partner|clock|bag|player|field|options|battle|versus|story|dialog|naming)\.([a-z_]+)\()"};
  const std::regex text{R"(\bt\(\s*["']([^"']+)["'])"};
  for (const std::filesystem::path& example : examples()) {
    CAPTURE(example.filename().string());
    const Installed installed{example};
    const ui::UiAddon addon = readExample(installed, example);
    CHECK(addon.uiApi.contains(ui::CurrentUiApiVersion));
    auto english = localization::Catalog::load(*installed.files, "en");
    REQUIRE(english.has_value());
    for (const auto& entry : std::filesystem::recursive_directory_iterator{example}) {
      if (!entry.is_regular_file() || entry.path().extension() != ".lua") {
        continue;
      }
      const std::string source = readText(entry.path());
      const std::string file = entry.path().filename().string();
      for (std::sregex_iterator found{source.begin(), source.end(), call};
           found != std::sregex_iterator{}; ++found) {
        const std::string kind = (*found)[1].str();
        const std::string name = (*found)[2].str();
        if (kind == "send") {
          CHECK_MESSAGE(addonMaySend(name),
                        std::format("{} sends {}, which no addon may", file, name));
        } else {
          const bool declared = declaredAnywhere(ui::UiContractKind::Option, name) ||
                                declaredAnywhere(kind == "setting" ? ui::UiContractKind::Setting
                                                                   : ui::UiContractKind::Report,
                                                 name);
          CHECK_MESSAGE(declared, std::format("{} uses ui.{}(\"{}\"), which the contract lacks",
                                              file, kind, name));
        }
      }
      for (std::sregex_iterator found{source.begin(), source.end(), api};
           found != std::sregex_iterator{}; ++found) {
        const std::string name = (*found)[1].str() + "." + (*found)[2].str();
        const bool declared = declaredAnywhere(ui::UiContractKind::Query, name) ||
                              declaredAnywhere(ui::UiContractKind::Action, name);
        CHECK_MESSAGE(declared, std::format("{} calls {}, which the contract lacks", file, name));
      }
      for (std::sregex_iterator found{source.begin(), source.end(), text};
           found != std::sregex_iterator{}; ++found) {
        const std::string key = (*found)[1].str();
        CHECK_MESSAGE(installed.texts->find(key).has_value(),
                      std::format("{} shows {}, which the Spanish texts lack", file, key));
        CHECK_MESSAGE(english->find(key).has_value(),
                      std::format("{} shows {}, which the English texts lack", file, key));
      }
    }
  }
}

namespace {

// Puts an installed example on over the game's UI, as the game does, and checks it goes on without
// a script error and sends only what an addon may.
void goesOn(const Installed& installed, const std::filesystem::path& example) {
  const Problems problems;
  const ui::UiAddon addon = readExample(installed, example);
  auto ui = installed.createUi();
  ui->setAddonCommandFilter(
      [&ui](std::string_view command) { return addonMaySend(command, battleWindowsOf(*ui)); });
  // The game's documents of the screens a whole UI covers, as the game loads them.
  auto game = ui::readUiAddon(*installed.files, ui::DefaultUiFolder);
  REQUIRE(game.has_value());
  for (const auto& [screen, document] : addon.screens) {
    const auto found = game->screens.find(screen);
    REQUIRE_MESSAGE(found != game->screens.end(), screen);
    REQUIRE(ui->loadDocument(screen, found->second));
  }
  // The clock running, as in the field.
  ui->setSetting("hud.shown", true);
  ui->setSetting("hud.clock.hour", 7.0);
  ui->setSetting("hud.clock.minute", 5.0);
  ui->setSetting("hud.clock.day", 3.0);
  Problems::clear();
  if (addon.type == ui::UiAddonType::Ui) {
    REQUIRE_MESSAGE(ui->useUiAddon(addon).has_value(), Problems::joined());
    for (const auto& [screen, document] : addon.screens) {
      ui->requestScreen(screen);
      CHECK(ui->isScreenOpen(screen));
      CHECK(ui->screenDocument(screen) == std::format("{}.{}", addon.id, screen));
    }
  } else {
    REQUIRE_MESSAGE(ui->addUiPlugin(addon).has_value(), Problems::joined());
    CHECK(ui->uiPlugins() == std::vector<std::string>{addon.id});
  }
  ui->update();
  // A battle starts and its fight runs: two fighters and their HP.
  ui->setSetting("battle.fighter.count", 2.0);
  ui->setSetting("battle.fighter.0.hp", 80.0);
  ui->setSetting("battle.fighter.0.max_hp", 100.0);
  ui->setSetting("battle.fighter.1.hp", 40.0);
  ui->setSetting("battle.fighter.1.max_hp", 120.0);
  ui->setSetting("battle.phase", std::string{"fight"});
  ui->setSetting("battle.active", true);
  ui->setSetting("battle.revision", 1.0);
  ui->update();
  ui->setSetting("battle.phase", std::string{"end"});
  ui->setSetting("battle.active", false);
  ui->update();
  CHECK_MESSAGE(Problems::joined().empty(), Problems::joined());
  // It takes nothing from the game it may not.
  for (const ui::UiCommand& command : ui->takeCommands()) {
    CHECK(addonMaySend(command.name));
  }
}

} // namespace

TEST_CASE("every example addon goes on over the game's UI without a script error") {
  for (const std::filesystem::path& example : examples()) {
    CAPTURE(example.filename().string());
    goesOn(Installed{example}, example);
  }
}

TEST_CASE("every example addon zipped reads and goes on the same as its folder") {
  for (const std::filesystem::path& example : examples()) {
    CAPTURE(example.filename().string());
    const ui::UiAddon folder = readExample(Installed{example}, example);
    for (const Copied copied : {Copied::ZipRoot, Copied::ZipFolder}) {
      CAPTURE(static_cast<int>(copied));
      const Installed installed{example, copied};
      const ui::UiAddon zipped = readExample(installed, example);
      CHECK(zipped.id == folder.id);
      CHECK(zipped.name == folder.name);
      CHECK(zipped.folder == folder.folder);
      CHECK(zipped.screens == folder.screens);
      CHECK(zipped.documents == folder.documents);
      goesOn(installed, example);
    }
  }
}

TEST_CASE("the battle example shows every fighter's HP while a fight runs, and nothing after it") {
  const std::filesystem::path example = std::filesystem::path{DWE_UI_EXAMPLES_DIR} / "battle_hp";
  const Installed installed{example};
  const ui::UiAddon addon = readExample(installed, example);
  auto ui = installed.createUi();
  REQUIRE(ui->addUiPlugin(addon).has_value());
  const std::string document = "battle_hp.document";
  CHECK(ui->elementHasClass(document, "panel", "hidden"));
  ui->setSetting("battle.fighter.count", 2.0);
  ui->setSetting("battle.fighter.0.hp", 80.0);
  ui->setSetting("battle.fighter.0.max_hp", 100.0);
  ui->setSetting("battle.fighter.1.hp", 40.0);
  ui->setSetting("battle.fighter.1.max_hp", 160.0);
  ui->setSetting("battle.phase", std::string{"fight"});
  ui->setSetting("battle.active", true);
  ui->update();
  CHECK_FALSE(ui->elementHasClass(document, "panel", "hidden"));
  CHECK(ui->elementText(document, "hp-1") == "PV 80");
  CHECK(ui->elementText(document, "hp-2") == "PV 40");
  CHECK(ui->styleProperty(document, "fill-2", "width") == "25%");
  CHECK(ui->elementHasClass(document, "row-3", "hidden"));
  // It steps aside while the bag is open over the fight.
  ui->setSetting("battle.bag.open", true);
  ui->setSetting("battle.revision", 1.0);
  ui->update();
  CHECK(ui->elementHasClass(document, "panel", "hidden"));
  ui->setSetting("battle.bag.open", false);
  ui->setSetting("battle.revision", 2.0);
  ui->update();
  CHECK_FALSE(ui->elementHasClass(document, "panel", "hidden"));
  // A hit: the battle's revision redraws it.
  ui->setSetting("battle.fighter.1.hp", 0.0);
  ui->setSetting("battle.revision", 3.0);
  ui->update();
  CHECK(ui->elementText(document, "hp-2") == "PV 0");
  ui->setSetting("battle.active", false);
  ui->update();
  CHECK(ui->elementHasClass(document, "panel", "hidden"));
}

TEST_CASE("the story log example keeps the last pages said, with their speakers") {
  const Problems problems;
  const std::filesystem::path example = std::filesystem::path{DWE_UI_EXAMPLES_DIR} / "story_log";
  const Installed installed{example};
  const ui::UiAddon addon = readExample(installed, example);
  auto ui = installed.createUi();
  ui->setAddonCommandFilter([](std::string_view command) { return addonMaySend(command); });
  Problems::clear();
  REQUIRE_MESSAGE(ui->addUiPlugin(addon).has_value(), Problems::joined());
  const std::string log = addon.id + ".document";
  // The story's box shows two pages, one said by a character and one by nobody.
  const auto page = [&](double number, std::string_view speaker, std::string_view text) {
    const bool named = !speaker.empty();
    ui->setSetting("story_textbox.open", true);
    ui->setSetting("story_textbox.row_count", named ? 2.0 : 1.0);
    ui->setSetting("story_textbox.speaker", std::string{speaker});
    ui->setSetting("story_textbox.speaker_kind", std::string{named ? "character" : "none"});
    const std::string first = named ? std::string{speaker} : std::string{text};
    ui->setSetting("story_textbox.row.0.runs", 1.0);
    ui->setSetting("story_textbox.row.0.run.0.text", first);
    ui->setSetting("story_textbox.row.1.runs", 1.0);
    ui->setSetting("story_textbox.row.1.run.0.text", std::string{text});
    ui->setSetting("story_textbox.revision", number);
    ui->setSetting("story_textbox.page", number);
    ui->update();
  };
  page(1.0, "Host", "Welcome.");
  page(2.0, "", "A sign.");
  CHECK_FALSE(ui->elementHasClass(log, "log", "hidden"));
  CHECK(ui->elementText(log, "speaker-1") == "Host");
  CHECK(ui->elementText(log, "text-1") == "Welcome.");
  CHECK(ui->elementHasClass(log, "speaker-2", "hidden"));
  CHECK(ui->elementText(log, "text-2") == "A sign.");
  CHECK(ui->elementHasClass(log, "line-3", "hidden"));
  // The box closes: the log hides with it.
  ui->setSetting("story_textbox.open", false);
  ui->setSetting("story_textbox.revision", 3.0);
  ui->update();
  CHECK(ui->elementHasClass(log, "log", "hidden"));
  CHECK_MESSAGE(Problems::joined().empty(), Problems::joined());
  CHECK(ui->takeCommands().empty());
}

TEST_CASE("an addon's call of a story, dialog or naming action sends only what the game takes") {
  // A plugin that asks each box through its namespace's action, while the boxes wait for it.
  const test_support::TempFolder folder;
  const std::filesystem::path probe = folder.path() / "api_probe";
  std::filesystem::create_directories(probe);
  const auto write = [&](std::string_view name, std::string_view text) {
    std::ofstream{probe / name, std::ios::binary} << text;
  };
  write("addon.yaml", std::format("id: api_probe\nname: Probe\nversion: 1.0.0\nui_api: \"{}.{}\"\n"
                                  "type: plugin\ndocuments:\n  - document\n",
                                  ui::CurrentUiApiVersion.major, ui::CurrentUiApiVersion.minor));
  write("document.rml", "<rml><head><script src=\"logic.lua\"></script></head><body>"
                        "<p id=\"story\"></p><p id=\"dialog\"></p><p id=\"naming\"></p>"
                        "</body></rml>");
  write("logic.lua", R"(local function try(call)
  local ok, problem = pcall(call)
  return ok and "sent" or tostring(problem)
end
function on_show(document)
  document:get("story"):set_text(try(story.advance))
  document:get("dialog"):set_text(try(dialog.cancel))
  document:get("naming"):set_text(try(naming.erase))
end
)");
  const Installed installed{probe};
  const ui::UiAddon addon = readExample(installed, probe);
  auto ui = installed.createUi();
  // The game would take these three now; this filter takes none of them from an addon.
  ui->setAddonCommandFilter([](std::string_view command) {
    return addonMaySend(command) && command != "story_advance" && command != "dialog_answer" &&
           command != "naming_erase";
  });
  auto game = ui::readUiAddon(*installed.files, ui::DefaultUiFolder);
  REQUIRE(game.has_value());
  for (const std::string_view screen : {"dialog", "naming_box"}) {
    const auto found = game->screens.find(std::string{screen});
    REQUIRE_MESSAGE(found != game->screens.end(), screen);
    REQUIRE(ui->loadDocument(screen, found->second));
    ui->show(screen);
  }
  ui->setSetting("story_textbox.input", std::string{"page"});
  ui->update();
  (void)ui->takeCommands();
  REQUIRE(ui->addUiPlugin(addon).has_value());
  ui->update();
  const std::string document = addon.id + ".document";
  CHECK(ui->elementText(document, "story").contains("an addon may not send story_advance"));
  CHECK(ui->elementText(document, "dialog").contains("an addon may not send dialog_answer"));
  CHECK(ui->elementText(document, "naming").contains("an addon may not send naming_erase"));
  CHECK(ui->takeCommands().empty());
}

} // namespace dwe::app
