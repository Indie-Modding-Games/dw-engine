#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "coop_hud.hpp"

namespace dwe::app {
namespace {

// The game pack from the source tree in memory, its English texts and the co-op HUD's document.
struct CoopHudUi {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;
  std::unique_ptr<localization::Catalog> texts;
  std::unique_ptr<ui::Ui> ui;

  CoopHudUi() {
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
    auto document = assets::AssetId::parse("ui/coop_hud/document");
    REQUIRE_MESSAGE(document.has_value(), document.error().message);
    auto loaded = ui->loadDocument(CoopHudDocument, *document);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
  }

  [[nodiscard]] std::string text(std::string_view element) const {
    return ui->elementText(CoopHudDocument, element);
  }
  [[nodiscard]] bool shown(std::string_view element) const {
    return ui->isVisible(CoopHudDocument) && ui->elementShown(CoopHudDocument, element);
  }
};

const CoopPeerLabel Ana{.name = "Ana", .key = {}, .number = 0};

} // namespace

TEST_CASE("the co-op tag names the guest by its name on the host, and the host by role") {
  CHECK(coopPeerLabel(true, "Ana") == Ana);
  // A guest that sent no name is the room's second member: "Guest 1".
  CHECK(coopPeerLabel(true, "") ==
        CoopPeerLabel{.name = {}, .key = std::string{GuestPlayerText}, .number = 1});
  // The host's name never reaches the guest: whatever the link says, the guest calls it "Host".
  CHECK(coopPeerLabel(false, "") ==
        CoopPeerLabel{.name = {}, .key = std::string{HostPlayerText}, .number = 0});
  CHECK(coopPeerLabel(false, "Ana").key == HostPlayerText);
  CHECK(coopPeerLabel(false, "Ana").name.empty());
}

TEST_CASE("the co-op tag shows only with a linked player whose head is in the running field") {
  const screens::WindowFraction middle{.x = 0.5, .y = 0.25};
  const CoopTagView shown = coopTagViewOf(Ana, middle, true);
  CHECK(shown.visible);
  CHECK(shown.x == doctest::Approx(50.0));
  CHECK(shown.y == doctest::Approx(25.0));
  CHECK(shown.peer == Ana);
  // No one linked, no projection, a stopped field or a head off the window: hidden, and empty, so
  // a hidden tag publishes nothing new while it stays hidden.
  CHECK(coopTagViewOf(std::nullopt, middle, true) == CoopTagView{});
  CHECK(coopTagViewOf(Ana, std::nullopt, true) == CoopTagView{});
  CHECK(coopTagViewOf(Ana, middle, false) == CoopTagView{});
  CHECK(coopTagViewOf(Ana, screens::WindowFraction{.x = -0.1, .y = 0.5}, true) == CoopTagView{});
  CHECK(coopTagViewOf(Ana, screens::WindowFraction{.x = 0.5, .y = 1.2}, true) == CoopTagView{});
}

TEST_CASE("losing the co-op link in play warns the host and sends the guest back to the title") {
  CHECK(coopLinkLoss(true, true) == CoopLinkLoss::PeerLeft);
  CHECK(coopLinkLoss(false, true) == CoopLinkLoss::HostLeft);
  // Out of play (the lobby, the title) the rooms screens say it themselves.
  CHECK(coopLinkLoss(true, false) == CoopLinkLoss::Quiet);
  CHECK(coopLinkLoss(false, false) == CoopLinkLoss::Quiet);
}

TEST_CASE("the co-op HUD draws the tag over the other player and hides with nothing to show") {
  CoopHudUi hud;
  CoopHud coop{*hud.ui};
  coop.showTag(coopTagViewOf(Ana, screens::WindowFraction{.x = 0.25, .y = 0.5}, true));
  coop.advance(0.016);
  hud.ui->update();
  REQUIRE(hud.ui->isVisible(CoopHudDocument));
  CHECK(hud.shown("tag"));
  CHECK(hud.text("tag-name") == "Ana");
  CHECK(hud.ui->styleProperty(CoopHudDocument, "tag", "left") == "25%");
  CHECK(hud.ui->styleProperty(CoopHudDocument, "tag", "top") == "50%");
  CHECK_FALSE(hud.shown("notice"));
  // The host seen from the guest: by its role, in the player's language.
  coop.showTag(
      coopTagViewOf(coopPeerLabel(false, ""), screens::WindowFraction{.x = 0.5, .y = 0.5}, true));
  hud.ui->update();
  CHECK(hud.text("tag-name") == "Host");
  // A guest with no name, by number.
  coop.showTag(
      coopTagViewOf(coopPeerLabel(true, ""), screens::WindowFraction{.x = 0.5, .y = 0.5}, true));
  hud.ui->update();
  CHECK(hud.text("tag-name") == "Guest 1");
  // Off the window the tag goes, and the document with it.
  coop.showTag(coopTagViewOf(Ana, std::nullopt, true));
  coop.advance(0.016);
  hud.ui->update();
  CHECK_FALSE(hud.ui->isVisible(CoopHudDocument));
}

TEST_CASE("a co-op notice names the player who left and goes after a few seconds") {
  CoopHudUi hud;
  CoopHud coop{*hud.ui};
  coop.notice(CoopDisconnectedText, Ana);
  hud.ui->update();
  REQUIRE(coop.noticeShown());
  CHECK(hud.shown("notice"));
  CHECK(hud.text("notice-text") == "Ana disconnected.");
  CHECK_FALSE(hud.shown("tag"));
  // A guest named by number, and a name with a pattern character, as they are.
  coop.notice(CoopDisconnectedText, coopPeerLabel(true, ""));
  hud.ui->update();
  CHECK(hud.text("notice-text") == "Guest 1 disconnected.");
  coop.notice(CoopDisconnectedText, coopPeerLabel(true, "100%"));
  hud.ui->update();
  CHECK(hud.text("notice-text") == "100% disconnected.");
  // The guest back at the title.
  coop.notice(CoopHostLeftText);
  hud.ui->update();
  CHECK(hud.text("notice-text") == "The host closed the room. You are back at the menu.");
  coop.advance(CoopHud::NoticeSeconds - 0.5);
  hud.ui->update();
  CHECK(hud.shown("notice"));
  coop.advance(1.0);
  hud.ui->update();
  CHECK_FALSE(coop.noticeShown());
  CHECK_FALSE(hud.ui->isVisible(CoopHudDocument));
}

} // namespace dwe::app
