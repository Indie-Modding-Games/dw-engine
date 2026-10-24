#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include "game_documents.hpp"

namespace {

// DevAssetsBanner loads this one itself, only when the development assets are in use.
constexpr std::string_view DevAssetsDirectory = "dev_assets";

bool loaded(std::string_view asset) {
  return std::ranges::any_of(
      dwe::app::GameDocuments,
      [asset](const dwe::app::GameDocument& document) { return document.asset == asset; });
}

} // namespace

TEST_CASE("game documents: every document of the game pack's ui is loaded") {
  const std::filesystem::path ui = std::filesystem::path{DWE_GAME_PACK} / "ui";
  REQUIRE(std::filesystem::is_directory(ui));
  int documents = 0;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{ui}) {
    const std::string directory = entry.path().filename().string();
    if (!entry.is_directory() || directory == DevAssetsDirectory ||
        !std::filesystem::exists(entry.path() / "document.rml")) {
      continue;
    }
    ++documents;
    const std::string asset = std::format("ui/{}/document", directory);
    INFO(asset);
    CHECK(loaded(asset));
  }
  CHECK(documents == static_cast<int>(dwe::app::GameDocuments.size()));
}

TEST_CASE("game documents: each name is loaded once") {
  for (const dwe::app::GameDocument& document : dwe::app::GameDocuments) {
    INFO(document.name);
    CHECK(std::ranges::count(dwe::app::GameDocuments, document.name,
                             &dwe::app::GameDocument::name) == 1);
  }
}
