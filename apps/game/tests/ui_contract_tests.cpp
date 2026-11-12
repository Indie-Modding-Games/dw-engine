#include <dwe/ui/ui_contract.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>

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
    {.screen = "hud", .files = {"hud/logic.lua", "hud/document.rml"}},
    {.screen = "pause_menu",
     .files = {"common/ui_skin_choice.lua", "common/original_box.lua", "common/panel_enter.lua",
               "common/game_options.lua", "pause_menu/logic.lua", "pause_menu/document.rml"}},
    {.screen = "partner_menu",
     .files = {"common/original_box.lua", "common/panel_enter.lua", "common/bag_slot.lua",
               "common/screen_fit.lua", "partner_menu/logic.lua", "partner_menu/document.rml"}},
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
// setting or an option, a name it sends a command or an option.
bool declared(std::string_view screen, std::string_view call, std::string_view name) {
  const auto contract = gameUiContract();
  if (call == "setting") {
    return ui::findContractEntry(contract, screen, ui::UiContractKind::Setting, name) != nullptr ||
           ui::findContractEntry(contract, screen, ui::UiContractKind::Option, name) != nullptr;
  }
  return ui::findContractEntry(contract, screen, ui::UiContractKind::Command, name) != nullptr ||
         ui::findContractEntry(contract, screen, ui::UiContractKind::Option, name) != nullptr;
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
  const std::regex call{R"(ui\.(setting|send)\(\s*["']([^"']+)["']\s*[,)])"};
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

} // namespace dwe::app
