#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::app::test {

// The game pack's pause menu at 1280x720, in Spanish.
struct MenuUi {
  dwe::assets::MemoryFileSystem memory;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files;
  std::unique_ptr<dwe::localization::Catalog> texts;
  std::unique_ptr<dwe::ui::Ui> ui;

  MenuUi() {
    const std::filesystem::path pack{DWE_GAME_PACK};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{pack}) {
      if (entry.is_regular_file()) {
        std::ifstream stream{entry.path(), std::ios::binary};
        memory.addFile(
            std::filesystem::path{"packs/game"} / entry.path().lexically_relative(pack),
            std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}});
      }
    }
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    const auto location = [this](std::string_view root) {
      return dwe::assets::PackLocation{.fileSystem = &memory,
                                       .root = std::string{root},
                                       .origin = dwe::assets::PackOrigin::Engine};
    };
    auto mounted =
        dwe::assets::VirtualFileSystem::mount({location("packs/base"), location("packs/game")});
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*mounted));
    auto catalog = dwe::localization::Catalog::load(*files, "es");
    REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
    texts = std::make_unique<dwe::localization::Catalog>(std::move(*catalog));
    auto created = dwe::ui::Ui::create(dwe::ui::UiDesc{.files = files.get(),
                                                       .texts = texts.get(),
                                                       .width = 1280,
                                                       .height = 720,
                                                       .clock = {},
                                                       .scripts = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    ui = std::move(*created);
    auto menu = dwe::assets::AssetId::parse("ui/pause_menu/document");
    REQUIRE_MESSAGE(menu.has_value(), menu.error().message);
    auto loaded = ui->loadDocument("pause_menu", *menu);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
  }
};

} // namespace dwe::app::test
