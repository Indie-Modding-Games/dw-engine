#include <dwe/ui/skin.hpp>
#include <dwe/ui/ui_contract.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ui_contract.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view GeneratedStart = "<!-- ui-api:generated -->";
constexpr std::string_view GeneratedEnd = "<!-- /ui-api:generated -->";

// A screen and the scripts its documents run: the logic and the common scripts it links.
struct ScreenScripts {
  std::string_view screen;
  std::array<std::string_view, 6> files;
};

constexpr auto Screens = std::to_array<ScreenScripts>({
    {.screen = "hud", .files = {"hud/presenter.lua", "hud/view.lua", "hud/document.rml"}},
    {.screen = "pause_menu",
     .files = {"common/ui_skin_choice.lua", "common/original_box.lua", "common/panel_enter.lua",
               "common/game_options.lua", "pause_menu/logic.lua", "pause_menu/document.rml"}},
    {.screen = "partner_menu",
     .files = {"common/original_box.lua", "common/panel_enter.lua", "common/bag_slot.lua",
               "common/screen_fit.lua", "partner_menu/logic.lua", "partner_menu/document.rml"}},
    {.screen = "skin_picker", .files = {"skin_picker/logic.lua", "skin_picker/document.rml"}},
});

std::string readText(const std::filesystem::path& path) {
  std::ifstream stream{path, std::ios::binary};
  REQUIRE_MESSAGE(stream.good(), path.string());
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

bool writeRequested() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* write = std::getenv("DWE_WRITE_API_DOC");
  return write != nullptr && std::string_view{write} == "1";
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out{path, std::ios::binary};
  out << text;
  REQUIRE(out.good());
}

// Whether a literal name a document reads or sends is in the contract: a setting it reads is a
// setting or an option, a name it sends a command or an option, a name it reports a report.
bool declared(std::string_view screen, std::string_view call, std::string_view name) {
  const auto contract = gameUiContract();
  if (call == "setting") {
    return ui::findContractEntry(contract, screen, ui::UiContractKind::Setting, name) != nullptr ||
           ui::findContractEntry(contract, screen, ui::UiContractKind::Option, name) != nullptr;
  }
  if (call == "report") {
    return ui::findContractEntry(contract, screen, ui::UiContractKind::Report, name) != nullptr;
  }
  return ui::findContractEntry(contract, screen, ui::UiContractKind::Command, name) != nullptr ||
         ui::findContractEntry(contract, screen, ui::UiContractKind::Option, name) != nullptr;
}

// Whether the contract declares a name of a kind on any screen or namespace.
bool declaredAnywhere(ui::UiContractKind kind, std::string_view name) {
  return std::ranges::any_of(gameUiContract(), [&](const ui::UiContractEntry& entry) {
    return entry.kind == kind && ui::matchesContractName(entry.name, name);
  });
}

// The files under `root` with an extension, sorted.
std::vector<std::filesystem::path> filesUnder(const std::filesystem::path& root,
                                              std::string_view extension) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
    if (entry.is_regular_file() && entry.path().extension() == extension) {
      files.push_back(entry.path());
    }
  }
  std::ranges::sort(files);
  return files;
}

// The UI API's adapters: content/packs/game/ui/api/<namespace>.lua.
std::vector<std::filesystem::path> apiScripts() {
  return filesUnder(std::filesystem::path{DWE_GAME_PACK} / "ui" / "api", ".lua");
}

// The roots the skin and script walks cover: every pack and every test mod.
std::array<std::filesystem::path, 2> contentRoots() {
  return {std::filesystem::path{DWE_PACKS_DIR}, std::filesystem::path{DWE_TEST_MODS_DIR}};
}

// Every skin folder: a folder with a skin.yaml under some pack's ui/skins/.
std::vector<std::filesystem::path> skinFolders() {
  std::vector<std::filesystem::path> folders;
  for (const std::filesystem::path& root : contentRoots()) {
    for (const std::filesystem::path& yaml : filesUnder(root, ".yaml")) {
      if (yaml.filename() == "skin.yaml" &&
          yaml.parent_path().parent_path().filename() == "skins") {
        folders.push_back(yaml.parent_path());
      }
    }
  }
  return folders;
}

// Every file of an interface (under a ui/ folder) with an extension, in every pack and test mod.
std::vector<std::filesystem::path> interfaceFiles(std::string_view extension) {
  std::vector<std::filesystem::path> files;
  for (const std::filesystem::path& root : contentRoots()) {
    for (const std::filesystem::path& file : filesUnder(root, extension)) {
      for (const std::filesystem::path& part : file.lexically_relative(root)) {
        if (part == "ui") {
          files.push_back(file);
          break;
        }
      }
    }
  }
  return files;
}

ui::Skin readSkin(const std::filesystem::path& folder) {
  auto skin = ui::parseSkin(readText(folder / "skin.yaml"));
  REQUIRE_MESSAGE(skin.has_value(), folder.string());
  return std::move(*skin);
}

} // namespace

TEST_CASE("the UI API page and its JSON twin list the game's UI contract") {
  // DWE_WRITE_API_DOC=1 rewrites them instead of checking them.
  const std::filesystem::path page = std::filesystem::path{DWE_DOCS_DIR} / "modding" / "ui-api.md";
  std::string text = readText(page);
  const std::size_t start = text.find(GeneratedStart);
  const std::size_t end = text.find(GeneratedEnd);
  REQUIRE(start != std::string::npos);
  REQUIRE(end != std::string::npos);
  const std::size_t from = start + GeneratedStart.size();
  REQUIRE(from <= end);
  const std::string markdown = ui::contractMarkdown(gameUiContract());

  const std::filesystem::path json =
      std::filesystem::path{DWE_SCHEMA_DIR} / "ui_api" / "ui_api.json";
  const std::string generatedJson = ui::contractJson(gameUiContract());
  if (writeRequested()) {
    text.replace(from, end - from, markdown);
    writeText(page, text);
    writeText(json, generatedJson);
    return;
  }
  CHECK_MESSAGE(text.substr(from, end - from) == markdown,
                "docs/modding/ui-api.md is out of date: run the game tests with "
                "DWE_WRITE_API_DOC=1");
  CHECK_MESSAGE(readText(json) == generatedJson,
                "content/schemas/ui_api/ui_api.json is out of date: run the game tests with "
                "DWE_WRITE_API_DOC=1");
}

TEST_CASE("every setting a screen's scripts read and every name they send is in the contract") {
  // A literal name only: a name built at run time (`"hud.meter." .. index`) is the prefix of a
  // declared pattern and is left out.
  const std::regex call{R"(ui\.(setting|send|report)\(\s*["']([^"']+)["']\s*[,)])"};
  const std::filesystem::path ui = std::filesystem::path{DWE_GAME_PACK} / "ui";
  for (const ScreenScripts& screen : Screens) {
    for (const std::string_view file : screen.files) {
      if (file.empty()) {
        continue;
      }
      const std::string text = readText(ui / file);
      for (std::sregex_iterator found{text.begin(), text.end(), call};
           found != std::sregex_iterator{}; ++found) {
        const std::string kind = (*found)[1].str();
        const std::string name = (*found)[2].str();
        const std::string missing =
            std::format("{}: {} uses ui.{}(\"{}\"), which apps/game/src/ui_contract.cpp lacks",
                        screen.screen, file, kind, name);
        CHECK_MESSAGE(declared(screen.screen, kind, name), missing);
      }
    }
  }
}

TEST_CASE("every argument a screen's scripts show another screen with is in the contract") {
  // ui.show("pause_menu", true, { entry = opened_on }): the names of a literal table, for the
  // screens the contract covers.
  const std::regex show{R"(ui\.show\(\s*["']([^"']+)["'][^{})]*\{([^}]*)\})"};
  const std::regex key{R"(([A-Za-z_][A-Za-z0-9_]*)\s*=)"};
  const std::filesystem::path ui = std::filesystem::path{DWE_GAME_PACK} / "ui";
  const auto contract = gameUiContract();
  for (const ScreenScripts& screen : Screens) {
    for (const std::string_view file : screen.files) {
      if (file.empty()) {
        continue;
      }
      const std::string text = readText(ui / file);
      for (std::sregex_iterator found{text.begin(), text.end(), show};
           found != std::sregex_iterator{}; ++found) {
        const std::string target = (*found)[1].str();
        const std::string table = (*found)[2].str();
        for (std::sregex_iterator name{table.begin(), table.end(), key};
             name != std::sregex_iterator{}; ++name) {
          const std::string argument = (*name)[1].str();
          const std::string missing =
              std::format("{}: {} shows {} with {}, which apps/game/src/ui_contract.cpp lacks",
                          screen.screen, file, target, argument);
          CHECK_MESSAGE(ui::findContractEntry(contract, target, ui::UiContractKind::Argument,
                                              argument) != nullptr,
                        missing);
        }
      }
    }
  }
}

TEST_CASE("every function of the UI API's adapters is in the contract, and the other way round") {
  // `function bag.use(index)`: a query or an action of the namespace the file is named after.
  const std::regex definition{R"(^function ([a-z_]+)\.([a-z_]+)\()"};
  std::set<std::string> defined;
  REQUIRE_FALSE(apiScripts().empty());
  for (const std::filesystem::path& file : apiScripts()) {
    std::istringstream lines{readText(file)};
    for (std::string line; std::getline(lines, line);) {
      std::smatch found;
      if (!std::regex_search(line, found, definition)) {
        continue;
      }
      const std::string space = found[1].str();
      const std::string name = space + "." + found[2].str();
      CHECK_MESSAGE(
          space == file.stem().string(),
          std::format("{} defines {}, outside its namespace", file.filename().string(), name));
      const bool declared = ui::findContractEntry(gameUiContract(), space,
                                                  ui::UiContractKind::Query, name) != nullptr ||
                            ui::findContractEntry(gameUiContract(), space,
                                                  ui::UiContractKind::Action, name) != nullptr;
      CHECK_MESSAGE(declared, std::format("{} defines {}, which apps/game/src/ui_contract.cpp "
                                          "lacks",
                                          file.filename().string(), name));
      defined.insert(name);
    }
  }
  for (const ui::UiContractEntry& entry : gameUiContract()) {
    if (entry.kind == ui::UiContractKind::Query || entry.kind == ui::UiContractKind::Action) {
      CHECK_MESSAGE(
          defined.contains(std::string{entry.name}),
          std::format("the contract declares {}, which no ui/api script defines", entry.name));
    }
  }
}

TEST_CASE("every event of the UI API comes from the settings the contract names") {
  // event_source("bag.revision", "bag.changed"); a setting named by a variable (the options' loop)
  // stands for the contract's source "@options".
  const std::regex literal{R"(event_source\(\s*["']([^"']+)["']\s*,\s*["']([^"']+)["']\s*\))"};
  const std::regex looped{
      R"(event_source\(\s*[A-Za-z_][A-Za-z0-9_.]*\s*,\s*["']([^"']+)["']\s*\))"};
  std::map<std::string, std::set<std::string>> fired;
  std::set<std::string> everyOption;
  for (const std::filesystem::path& file : apiScripts()) {
    const std::string text = readText(file);
    for (std::sregex_iterator found{text.begin(), text.end(), literal};
         found != std::sregex_iterator{}; ++found) {
      fired[(*found)[2].str()].insert((*found)[1].str());
    }
    for (std::sregex_iterator found{text.begin(), text.end(), looped};
         found != std::sregex_iterator{}; ++found) {
      everyOption.insert((*found)[1].str());
    }
  }
  std::set<std::string> declared;
  for (const ui::UiContractEntry& entry : gameUiContract()) {
    if (entry.kind != ui::UiContractKind::Event) {
      continue;
    }
    const std::string name{entry.name};
    declared.insert(name);
    if (entry.source == "@options") {
      CHECK_MESSAGE(everyOption.contains(name), std::format("{} fires on no option", name));
      continue;
    }
    std::istringstream sources{std::string{entry.source}};
    std::set<std::string> named;
    for (std::string setting; sources >> setting;) {
      named.insert(setting);
      const bool declaredSetting = declaredAnywhere(ui::UiContractKind::Setting, setting) ||
                                   declaredAnywhere(ui::UiContractKind::Option, setting);
      CHECK_MESSAGE(
          declaredSetting,
          std::format("{} comes from {}, which the contract does not declare", name, setting));
    }
    CHECK_MESSAGE(fired[name] == named,
                  std::format("the ui/api scripts fire {} from other settings than the "
                              "contract's source",
                              name));
  }
  for (const auto& [event, settings] : fired) {
    CHECK_MESSAGE(declared.contains(event),
                  std::format("ui/api fires {}, which the contract lacks", event));
  }
  for (const std::string& event : everyOption) {
    CHECK_MESSAGE(declared.contains(event),
                  std::format("ui/api fires {}, which the contract lacks", event));
  }
}

TEST_CASE("the UI API's adapters read and send only names in the contract") {
  const std::regex call{R"(ui\.(setting|send|report)\(\s*["']([^"']+)["']\s*[,)])"};
  for (const std::filesystem::path& file : apiScripts()) {
    const std::string text = readText(file);
    for (std::sregex_iterator found{text.begin(), text.end(), call};
         found != std::sregex_iterator{}; ++found) {
      const std::string kind = (*found)[1].str();
      const std::string name = (*found)[2].str();
      const bool declared = declaredAnywhere(ui::UiContractKind::Option, name) ||
                            declaredAnywhere(kind == "setting"  ? ui::UiContractKind::Setting
                                             : kind == "report" ? ui::UiContractKind::Report
                                                                : ui::UiContractKind::Command,
                                             name);
      CHECK_MESSAGE(declared,
                    std::format("{} uses ui.{}(\"{}\"), which apps/game/src/ui_contract.cpp lacks",
                                file.filename().string(), kind, name));
    }
  }
}

TEST_CASE("a skin only restyles: no markup, no Lua, and layout options the documents know") {
  // A layout option is known when a script reads it (ui.layout("menu")) or a style sheet styles
  // its class (.layout-menu-original).
  const std::regex read{R"(ui\.layout\(\s*["']([^"']+)["']\s*\))"};
  const std::regex styled{R"(layout-([a-z0-9_]+)-)"};
  std::set<std::string> known;
  for (const std::filesystem::path& file : interfaceFiles(".lua")) {
    const std::string text = readText(file);
    for (std::sregex_iterator found{text.begin(), text.end(), read};
         found != std::sregex_iterator{}; ++found) {
      known.insert((*found)[1].str());
    }
  }
  for (const std::filesystem::path& file : interfaceFiles(".rcss")) {
    const std::string text = readText(file);
    for (std::sregex_iterator found{text.begin(), text.end(), styled};
         found != std::sregex_iterator{}; ++found) {
      known.insert((*found)[1].str());
    }
  }
  const std::vector<std::filesystem::path> folders = skinFolders();
  REQUIRE(folders.size() >= 3);
  for (const std::filesystem::path& folder : folders) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator{folder}) {
      const std::filesystem::path extension = entry.path().extension();
      CHECK_MESSAGE((extension != ".lua" && extension != ".rml"),
                    std::format("{}: a skin restyles the UI's documents and holds no markup or "
                                "Lua of its own",
                                entry.path().lexically_relative(folder.parent_path()).string()));
    }
    const ui::Skin skin = readSkin(folder);
    for (const auto& [screen, options] : skin.layout) {
      for (const auto& [option, value] : options) {
        CHECK_MESSAGE(known.contains(option),
                      std::format("skin {}: layout {}: {} is read by no script and styled by no "
                                  "sheet",
                                  skin.id, screen, option));
      }
    }
  }
}

TEST_CASE("no interface script asks which skin is on or reads a skin's classes") {
  // A script that needs to know how a skin lays a screen out reads ui.layout; the classes a skin
  // puts on elements (document_classes) are only the sheets'.
  std::set<std::string> skinClasses;
  for (const std::filesystem::path& folder : skinFolders()) {
    for (const auto& [document, elements] : readSkin(folder).documentClasses) {
      for (const auto& [element, classes] : elements) {
        skinClasses.insert(classes.begin(), classes.end());
      }
    }
  }
  REQUIRE_FALSE(skinClasses.empty());
  const std::regex asksSkin{
      R"((ui\.setting|options\.get)\(\s*["']ui_skin["']\s*\)\s*[=~]=\s*["'])"
      R"(|["'][^"']*["']\s*[=~]=\s*(ui\.setting|options\.get)\(\s*["']ui_skin["'])"};
  const std::regex classCall{R"((has_class|set_class)\(\s*["']([^"']+)["'])"};
  for (const std::filesystem::path& file : interfaceFiles(".lua")) {
    const std::string text = readText(file);
    const std::string where =
        file.filename().string() + " in " + file.parent_path().filename().string();
    CHECK_MESSAGE(!std::regex_search(text, asksSkin),
                  std::format("{} compares ui_skin with a skin's id", where));
    for (std::sregex_iterator found{text.begin(), text.end(), classCall};
         found != std::sregex_iterator{}; ++found) {
      const std::string name = (*found)[2].str();
      const bool skinClass = skinClasses.contains(name) || name.starts_with("skin-") ||
                             name.starts_with(ui::LayoutClassPrefix);
      CHECK_MESSAGE(!skinClass, std::format("{} uses {}(\"{}\"), a skin's class", where,
                                            (*found)[1].str(), name));
    }
  }
}

} // namespace dwe::app
