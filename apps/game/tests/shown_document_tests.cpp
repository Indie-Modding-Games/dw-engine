#include <dwe/assets/asset_id.hpp>
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
#include <vector>

#include "save_machine.hpp"
#include "shown_document.hpp"

namespace dwe::app {
namespace {

// The game pack from the source tree over an empty stand-in base pack, in memory, and its UI.
struct GameUi {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;
  std::unique_ptr<localization::Catalog> texts;
  std::unique_ptr<ui::Ui> ui;

  GameUi() {
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
    const std::vector<assets::PackLocation> locations{
        {.fileSystem = &memory, .root = "packs/base", .origin = assets::PackOrigin::Engine},
        {.fileSystem = &memory, .root = "packs/game", .origin = assets::PackOrigin::Engine}};
    auto mounted = assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
    auto catalog = localization::Catalog::load(*files, "en");
    REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
    texts = std::make_unique<localization::Catalog>(std::move(*catalog));
    auto created = ui::Ui::create(ui::UiDesc{.files = files.get(),
                                             .texts = texts.get(),
                                             .width = 1280,
                                             .height = 720,
                                             .clock = {},
                                             .scripts = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    ui = std::move(*created);
    auto document = assets::AssetId::parse("ui/save_machine/document");
    REQUIRE_MESSAGE(document.has_value(), document.error().message);
    auto loaded = ui->loadDocument(SaveMachineDocument, *document);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
  }
};

} // namespace

TEST_CASE("a screen's document goes with the screen, so a game ended over it leaves no box") {
  GameUi game;
  {
    ShownDocument document{*game.ui, SaveMachineDocument};
    document.show(true);
    REQUIRE(game.ui->isVisible(SaveMachineDocument));
    CHECK(document.shown());
  }
  CHECK_FALSE(game.ui->isVisible(SaveMachineDocument));
}

TEST_CASE("a document hidden by its screen stays hidden when the screen goes") {
  GameUi game;
  ShownDocument other{*game.ui, SaveMachineDocument};
  {
    ShownDocument document{*game.ui, SaveMachineDocument};
    document.show(true);
    document.hide();
    CHECK_FALSE(document.shown());
    // Another owner shows it after the first hid it: the first one going must not hide it.
    other.show(true);
  }
  CHECK(game.ui->isVisible(SaveMachineDocument));
}

} // namespace dwe::app
