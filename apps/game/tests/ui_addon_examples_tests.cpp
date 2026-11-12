// The example addons (examples/ui-addons/, docs/modding/interfaces.md): each one reads, suits this
// UI API, uses only names the UI contract declares and texts the game has, and goes on in the
// game's UI without a script error, as a player who copies its folder would see it.
#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/ui/ui.hpp>
#include <dwe/ui/ui_addon.hpp>
#include <dwe/ui/ui_contract.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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

// The game pack and one example copied where a player puts it: ui/addons/<folder>/ of a mod pack.
struct Installed {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;
  std::unique_ptr<localization::Catalog> texts;

  explicit Installed(const std::filesystem::path& example) {
    const auto copy = [this](const std::filesystem::path& from, const std::filesystem::path& to) {
      for (const auto& entry : std::filesystem::recursive_directory_iterator{from}) {
        if (entry.is_regular_file()) {
          memory.addFile(to / entry.path().lexically_relative(from), readText(entry.path()));
        }
      }
    };
    copy(DWE_GAME_PACK, "packs/game");
    copy(example, std::filesystem::path{"packs/addons/ui/addons"} / example.filename());
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    memory.addFile("packs/addons/pack.yaml",
                   "id: addons\nversion: 0.1.0\nlayer: mods\npriority: 0\n");
    const auto location = [this](std::string_view root, assets::PackOrigin origin) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = origin};
    };
    auto mounted = assets::VirtualFileSystem::mount(
        std::vector{location("packs/base", assets::PackOrigin::Engine),
                    location("packs/game", assets::PackOrigin::Engine),
                    location("packs/addons", assets::PackOrigin::Mod)});
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

TEST_CASE("every example addon reads, suits this UI API and uses only what the contract declares") {
  const std::regex call{R"(ui\.(setting|send|report)\(\s*["']([^"']+)["']\s*[,)])"};
  const std::regex api{R"(\b(partner|clock|bag|player|field|options)\.([a-z_]+)\()"};
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

TEST_CASE("every example addon goes on over the game's UI without a script error") {
  const Problems problems;
  for (const std::filesystem::path& example : examples()) {
    CAPTURE(example.filename().string());
    const Installed installed{example};
    const ui::UiAddon addon = readExample(installed, example);
    auto ui = installed.createUi();
    ui->setAddonCommandFilter(addonMaySend);
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
    CHECK_MESSAGE(Problems::joined().empty(), Problems::joined());
    // It takes nothing from the game it may not.
    for (const ui::UiCommand& command : ui->takeCommands()) {
      CHECK(addonMaySend(command.name));
    }
  }
}

} // namespace dwe::app
