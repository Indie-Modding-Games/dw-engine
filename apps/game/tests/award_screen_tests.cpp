#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "award_screen.hpp"
#include "session_fixture.hpp"

namespace dwe::app {
namespace {

// The game pack from the source tree with the award notice loaded, the original's sickness line in
// a made-up base pack, and their UI at 1280x720.
struct NoticeUi {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;
  std::unique_ptr<localization::Catalog> texts;
  std::unique_ptr<ui::Ui> ui;

  NoticeUi() {
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
    memory.addFile("packs/base/text/en/main.yaml", "partner_impl_is_sick.0000: \"is Sick!\"\n");
    const auto location = [this](std::string_view root) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = assets::PackOrigin::Engine};
    };
    const std::array locations{location("packs/base"), location("packs/game")};
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
                                             .scripts = {},
                                             .cursor = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    ui = std::move(*created);
    auto document = assets::AssetId::parse("ui/award_notice/document");
    REQUIRE_MESSAGE(document.has_value(), document.error().message);
    auto loaded = ui->loadDocument(AwardNoticeDocument, *document);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
  }

  [[nodiscard]] std::string text(std::string_view element) const {
    return ui->elementText(AwardNoticeDocument, element);
  }
};

} // namespace

TEST_CASE("the partner falling sick opens the award box with its name and the sickness line") {
  // partner_impl.c: tickSicknessMechanics: tamerSetState(0x14), the name in TEXT_COLORS 10 and
  // "is Sick!" after it in 1, in the box of tamerTickAwardSomething, until confirmed.
  const session::testing::TestPacks packs;
  game::save::MemorySaveStorage storage;
  const auto session = session::testing::newGame(packs, &storage);
  NoticeUi notice;
  AwardScreen screen{nullptr, *session, *notice.ui, nullptr};

  screen.tick();
  CHECK_FALSE(screen.active());

  screen.noteSickness("Agumon");
  CHECK_FALSE(screen.active());
  screen.tick();
  REQUIRE(screen.active());
  notice.ui->update();
  CHECK(notice.ui->isVisible(AwardNoticeDocument));
  CHECK(notice.text("name") == "Agumon ");
  CHECK(notice.text("text-1") == "is Sick!");
  CHECK(notice.ui->elementHasClass(AwardNoticeDocument, "name", "color-10"));
  CHECK(notice.ui->elementHasClass(AwardNoticeDocument, "line-1", "color-1"));
  CHECK(notice.ui->elementHasClass(AwardNoticeDocument, "line-2", "hidden"));

  CHECK_FALSE(screen.handle({.name = "other", .value = {}}));
  CHECK(screen.handle({.name = "award_notice_ok", .value = {}}));
  CHECK_FALSE(screen.active());
  notice.ui->update();
  CHECK_FALSE(notice.ui->isVisible(AwardNoticeDocument));
  // Shown once: the next tick opens nothing.
  screen.tick();
  CHECK_FALSE(screen.active());
}

} // namespace dwe::app
