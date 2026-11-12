#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/ui/skin.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common.hpp"
#include "ui_skin_setting.hpp"
#include "user_settings.hpp"

namespace dwe::app {
namespace {

// Whether the stand-in base pack has the images dwx extracts from the disc, which the original skin
// needs.
enum class DiscArt : std::uint8_t { Missing, Extracted };

// The game pack and the test skin's pack from the source tree over a stand-in base pack, in memory.
struct Content {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;
  std::unique_ptr<localization::Catalog> texts;
  std::unique_ptr<ui::Ui> ui;

  explicit Content(DiscArt art = DiscArt::Missing) {
    copyPack(DWE_GAME_PACK, "packs/game");
    copyPack(DWE_SKIN_TEST_PACK, "packs/test_contrast");
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    if (art == DiscArt::Extracted) {
      addOriginalArt();
    }
    // The engine's own packs and the test skin's mod pack, as the game mounts them.
    const auto location = [this](std::string_view root, assets::PackOrigin origin) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = origin};
    };
    const std::vector<assets::PackLocation> locations{
        location("packs/base", assets::PackOrigin::Engine),
        location("packs/game", assets::PackOrigin::Engine),
        location("packs/test_contrast", assets::PackOrigin::Mod)};
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
  }

  // Blank stand-ins, of the disc's sizes, for the images dwx extracts that the original skin draws
  // from.
  void addOriginalArt() {
    struct Stand {
      std::string_view path;
      std::uint32_t width;
      std::uint32_t height;
    };
    for (const Stand& image :
         {Stand{.path = "texture/etcdat/system_w.png", .width = 256, .height = 128},
          Stand{.path = "texture/etcdat/fi_info.png", .width = 256, .height = 64},
          Stand{.path = "texture/etcdat/sboy.png", .width = 256, .height = 256},
          Stand{.path = "texture/etcdat/bi_in.png", .width = 256, .height = 64},
          Stand{.path = "texture/etcdat/feel_ef.png", .width = 256, .height = 64},
          Stand{.path = "texture/etcdat/btlcomnd.png", .width = 256, .height = 64},
          Stand{.path = "texture/etcna/title2.png", .width = 320, .height = 240},
          Stand{.path = "texture/etcna/title256.png", .width = 320, .height = 256},
          Stand{.path = "texture/stddat/time.png", .width = 66, .height = 29},
          Stand{.path = "texture/stddat/stdtim/002.png", .width = 256, .height = 120},
          Stand{.path = "texture/stddat/stdtim/003.png", .width = 256, .height = 256},
          Stand{.path = "texture/stddat/taisen1.png", .width = 256, .height = 256},
          Stand{.path = "texture/stddat/taisen2.png", .width = 128, .height = 144},
          Stand{.path = "texture/stddat/16taisen.png", .width = 256, .height = 256},
          Stand{.path = "map/trai00/texture4/001.png", .width = 256, .height = 256}}) {
      const assets::Image blank{
          .width = image.width,
          .height = image.height,
          .pixels = std::vector<std::byte>(std::size_t{image.width} * image.height * 4)};
      auto png = assets::encodePng(blank);
      REQUIRE_MESSAGE(png.has_value(), png.error().message);
      memory.addFile(std::filesystem::path{"packs/base"} / image.path, *png);
    }
  }

  void copyPack(const std::filesystem::path& folder, const std::filesystem::path& into) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator{folder}) {
      if (!entry.is_regular_file()) {
        continue;
      }
      std::ifstream stream{entry.path(), std::ios::binary};
      const std::string bytes{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
      memory.addFile(into / entry.path().lexically_relative(folder), bytes);
    }
  }

  [[nodiscard]] ui::UiValue published() const { return ui->setting(ui::UiSkinSetting); }

  // Loads one of the game pack's documents, hidden, as the game does at start.
  void load(std::string_view name) const {
    auto asset = assets::AssetId::parse("ui/" + std::string{name} + "/document");
    REQUIRE_MESSAGE(asset.has_value(), asset.error().message);
    auto loaded = ui->loadDocument(name, *asset);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
  }

  // The skin picker's row of a skin: the packs' skins in their order, from 1.
  [[nodiscard]] std::string rowOf(std::string_view skin) const {
    const auto& skins = ui->skins().skins();
    const auto found = std::ranges::find(skins, skin, &ui::Skin::id);
    REQUIRE(found != skins.end());
    return std::to_string(std::distance(skins.begin(), found) + 1);
  }
};

} // namespace

TEST_CASE("the start skin is --ui-skin, else the saved one, else the packs' default") {
  const std::optional<std::string> none;
  CHECK(startUiSkin(std::string{"modern"}, std::string{"test_contrast"}) ==
        std::optional<std::string>{"modern"});
  CHECK(startUiSkin(none, std::string{"test_contrast"}) ==
        std::optional<std::string>{"test_contrast"});
  CHECK_FALSE(startUiSkin(none, none).has_value());

  // Only one UI may exist at a time: each run is a scope of its own.
  {
    Content content;
    applyStartUiSkin(*content.ui, startUiSkin(none, std::string{"test_contrast"}));
    CHECK(content.ui->skin() == "test_contrast");
    CHECK(content.published() == ui::UiValue{std::string{"test_contrast"}});
  }
  // Nothing chosen keeps the packs' default: the original look, or the modern one when the disc's
  // art is missing; a skin the packs do not offer keeps it too, with a warning.
  {
    Content defaults{DiscArt::Extracted};
    applyStartUiSkin(*defaults.ui, startUiSkin(none, none));
    CHECK(defaults.ui->skin() == "original");
    CHECK(defaults.published() == ui::UiValue{std::string{"original"}});
  }
  {
    Content withoutArt;
    applyStartUiSkin(*withoutArt.ui, startUiSkin(none, none));
    CHECK(withoutArt.ui->skin() == "modern");
    CHECK(withoutArt.published() == ui::UiValue{std::string{"modern"}});
  }
  {
    Content unknown;
    applyStartUiSkin(*unknown.ui,
                     startUiSkin(std::string{"missing"}, std::string{"test_contrast"}));
    CHECK(unknown.ui->skin() == "modern");
    CHECK(unknown.published() == ui::UiValue{std::string{"modern"}});
  }
}

TEST_CASE(
    "a skin the player saved stays theirs when the packs' default changes; an unset one takes the default") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "ui_skin.yaml";
  // Before the default became the original look, a player chose the modern one and it was saved.
  saveUserSettings(file, UserSettings{.language = std::nullopt,
                                      .textFont = std::nullopt,
                                      .uiSkin = "modern",
                                      .uiAddon = std::nullopt,
                                      .minimap = std::nullopt});
  {
    Content content{DiscArt::Extracted};
    applyStartUiSkin(*content.ui, startUiSkin(std::nullopt, loadUserSettings(file).uiSkin));
    CHECK(content.ui->skin() == "modern");
  }
  // A player who never chose one gets the new default.
  saveUserSettings(file, UserSettings{.language = "es",
                                      .textFont = std::nullopt,
                                      .uiSkin = std::nullopt,
                                      .uiAddon = std::nullopt,
                                      .minimap = std::nullopt});
  {
    Content content{DiscArt::Extracted};
    const UserSettings unset = loadUserSettings(file);
    CHECK_FALSE(unset.uiSkin.has_value());
    applyStartUiSkin(*content.ui, startUiSkin(std::nullopt, unset.uiSkin));
    CHECK(content.ui->skin() == "original");
  }
}

TEST_CASE(
    "a change of skin switches the UI, publishes it and saves it, and the next start reads it back") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "ui_skin.yaml";
  {
    Content content;
    UserSettings settings = loadUserSettings(file);
    applyStartUiSkin(*content.ui, startUiSkin(std::nullopt, settings.uiSkin));
    UiSkinChanger changer{*content.ui, settings, file};
    changer.change("test_contrast");
    CHECK(content.ui->skin() == "test_contrast");
    CHECK(content.published() == ui::UiValue{std::string{"test_contrast"}});
    CHECK(settings.uiSkin == std::optional<std::string>{"test_contrast"});
    // An id the packs do not offer changes nothing, saved or shown.
    changer.change("missing");
    CHECK(content.ui->skin() == "test_contrast");
  }
  {
    Content next;
    const UserSettings saved = loadUserSettings(file);
    CHECK(saved.uiSkin == std::optional<std::string>{"test_contrast"});
    applyStartUiSkin(*next.ui, startUiSkin(std::nullopt, saved.uiSkin));
    CHECK(next.ui->skin() == "test_contrast");
  }
}

TEST_CASE("a saved legacy skin, which the new skin replaced, starts as the original") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  saveUserSettings(file, UserSettings{.language = "es",
                                      .textFont = std::nullopt,
                                      .uiSkin = "legacy",
                                      .uiAddon = std::nullopt,
                                      .minimap = std::nullopt});
  const UserSettings saved = loadUserSettings(file);
  CHECK(startUiSkin(std::nullopt, saved.uiSkin) == std::optional<std::string>{"original"});
  // --ui-skin is the player's own choice of the run, taken as it is.
  CHECK(startUiSkin(std::string{"legacy"}, saved.uiSkin) == std::optional<std::string>{"legacy"});
  Content content{DiscArt::Extracted};
  CHECK(content.ui->skins().find("legacy") == nullptr);
  applyStartUiSkin(*content.ui, startUiSkin(std::nullopt, saved.uiSkin));
  CHECK(content.ui->skin() == "original");
  CHECK(content.published() == ui::UiValue{std::string{"original"}});
  // It was a choice: the first run's picker does not ask again.
  CHECK_FALSE(asksUiSkin(std::nullopt, saved.uiSkin, content.ui->skins().skins().size()));
}

TEST_CASE("the title asks the skin only when no command line or saved choice names one") {
  CHECK(asksUiSkin(std::nullopt, std::nullopt, 2));
  CHECK_FALSE(asksUiSkin(std::nullopt, std::string{"modern"}, 2));
  CHECK_FALSE(asksUiSkin(std::string{"modern"}, std::nullopt, 2));
  // Nothing to choose from.
  CHECK_FALSE(asksUiSkin(std::nullopt, std::nullopt, 1));
}

TEST_CASE("a first run shows the skin picker, then the language box; the saved skin skips it") {
  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  {
    Content content{DiscArt::Extracted};
    UserSettings settings = loadUserSettings(file);
    applyStartUiSkin(*content.ui, startUiSkin(std::nullopt, settings.uiSkin));
    UiSkinChanger changer{*content.ui, settings, file};
    content.load(MainMenuDocument);
    content.load(SkinPickerDocument);
    content.ui->setSetting("title.language_first", true);
    // As TitleScreen::showBoot and showMenu: the picker while it asks, else the main menu at
    // "Press Start", whose language box comes first.
    SkinPickerStep picker{
        asksUiSkin(std::nullopt, settings.uiSkin, content.ui->skins().skins().size())};
    const auto showBoot = [&] {
      if (!picker.show(*content.ui)) {
        content.ui->setSetting("title.boot", true);
        content.ui->show(MainMenuDocument, true);
      }
    };
    showBoot();
    content.ui->update();
    CHECK(content.ui->isVisible(SkinPickerDocument));
    CHECK_FALSE(content.ui->isVisible(MainMenuDocument));
    // The cursor starts on the skin shown, the packs' default.
    CHECK(content.ui->focusedElementId(SkinPickerDocument) == "skin-" + content.rowOf("original"));

    // Moving the cursor puts a skin on at once, without saving it.
    REQUIRE(content.ui->callScript(SkinPickerDocument, "skin_focus",
                                   std::vector<std::string>{content.rowOf("modern")}));
    for (const ui::UiCommand& command : content.ui->takeCommands()) {
      REQUIRE(command.name == UiSkinPreviewCommand);
      CHECK(changer.preview(std::get<std::string>(command.value)));
    }
    content.ui->update();
    CHECK(content.ui->skin() == "modern");
    CHECK(content.published() == ui::UiValue{std::string{"modern"}});
    CHECK(content.ui->isVisible(SkinPickerDocument));
    CHECK_FALSE(loadUserSettings(file).uiSkin.has_value());

    // Confirming one saves it, and the title goes on to the language box. Cancel does nothing.
    REQUIRE(content.ui->callScript(SkinPickerDocument, "on_action",
                                   std::vector<std::string>{"", "cancel"}));
    CHECK(content.ui->takeCommands().empty());
    CHECK(content.ui->isVisible(SkinPickerDocument));
    REQUIRE(content.ui->callScript(SkinPickerDocument, "choose",
                                   std::vector<std::string>{content.rowOf("original")}));
    const std::vector<ui::UiCommand> commands = content.ui->takeCommands();
    REQUIRE(commands.size() == 2);
    CHECK(commands[0].name == ui::UiSkinSetting);
    changer.change(std::get<std::string>(commands[0].value));
    REQUIRE(picker.handle(commands[1], *content.ui));
    showBoot();
    content.ui->update();
    CHECK_FALSE(content.ui->isVisible(SkinPickerDocument));
    CHECK(content.ui->isVisible(MainMenuDocument));
    CHECK(content.ui->elementShown(MainMenuDocument, "language-box"));
    CHECK(loadUserSettings(file).uiSkin == std::optional<std::string>{"original"});

    // Asked once: back at the title later, the language box comes first again.
    CHECK_FALSE(picker.show(*content.ui));
    CHECK_FALSE(content.ui->isVisible(SkinPickerDocument));
  }
  {
    // The next start has the saved skin: no picker.
    Content next{DiscArt::Extracted};
    const UserSettings saved = loadUserSettings(file);
    CHECK_FALSE(asksUiSkin(std::nullopt, saved.uiSkin, next.ui->skins().skins().size()));
  }
}

} // namespace dwe::app
