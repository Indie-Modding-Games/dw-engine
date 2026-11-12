#include <dwe/ui/ui_addon.hpp>
#include <dwe/ui/ui_contract.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

// DevAssetsBanner loads this one itself, only when the development assets are in use.
constexpr std::string_view DevAssetsDirectory = "dev_assets";

dwe::ui::UiAddon defaultUi() {
  const std::filesystem::path manifest = std::filesystem::path{DWE_GAME_PACK} / "ui" / "addon.yaml";
  std::ifstream in{manifest};
  REQUIRE(in.good());
  std::ostringstream text;
  text << in.rdbuf();
  auto addon = dwe::ui::parseUiAddon(text.str(), dwe::ui::DefaultUiFolder);
  if (!addon) {
    FAIL(addon.error().message);
  }
  return *addon;
}

} // namespace

TEST_CASE(
    "the default UI shows every screen with the document of the game pack's ui named after it") {
  const dwe::ui::UiAddon addon = defaultUi();
  CHECK(addon.id == dwe::ui::DefaultUiId);
  CHECK(addon.type == dwe::ui::UiAddonType::Ui);
  CHECK(addon.uiApi.contains(dwe::ui::CurrentUiApiVersion));
  const std::filesystem::path ui = std::filesystem::path{DWE_GAME_PACK} / "ui";
  std::size_t documents = 0;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{ui}) {
    const std::string directory = entry.path().filename().string();
    if (!entry.is_directory() || directory == DevAssetsDirectory ||
        !std::filesystem::exists(entry.path() / "document.rml")) {
      continue;
    }
    ++documents;
    INFO(directory);
    const auto screen = addon.screens.find(directory);
    REQUIRE(screen != addon.screens.end());
    CHECK(screen->second.str() == std::format("ui/{}/document", directory));
  }
  CHECK(documents == addon.screens.size());
}
