#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/ui/skin.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace dwe::ui {
namespace {

constexpr double Tick = 1.0 / 60.0;

// The screens the game loads, each by the name its skin styles it under.
constexpr std::array<std::string_view, 6> Screens{"hud",    "pause_menu",   "main_menu",
                                                  "battle", "partner_menu", "save_slots"};

// The other screens that take the pause menu's panel and entries in both looks (skin.yaml).
constexpr std::array<std::string_view, 11> PanelScreens{
    "destination_choice", "dialog",       "new_game_rules", "sleep_menu", "gym_stats", "shop_menu",
    "naming_box",         "fishing_bait", "fishing_notice", "gym_slots",  "egg_box"};

assets::AssetId idOf(std::string_view text) {
  auto id = assets::AssetId::parse(text);
  REQUIRE(id.has_value());
  return *id;
}

// The game pack and the test skin's mod pack from the source tree, a stand-in for the base pack, a
// pack for the tests' own files and an empty mod pack, all in memory. Base, game and tests are the
// engine's own packs (PackOrigin::Engine), whose documents run their Lua; test_contrast and mod are
// mods, whose documents do not.
struct Content {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;

  Content() {
    copyPack(DWE_GAME_PACK, "packs/game");
    copyPack(DWE_SKIN_TEST_PACK, "packs/test_contrast");
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    // Above the game pack, so the tests' files override its own.
    memory.addFile("packs/tests/pack.yaml",
                   "id: tests\nversion: 0.1.0\nlayer: base\npriority: 2\n");
    memory.addFile("packs/mod/pack.yaml", "id: mod\nversion: 0.1.0\nlayer: mods\npriority: 1\n");
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

  void add(std::string_view path, std::string_view text) {
    memory.addFile(std::filesystem::path{"packs/tests"} / path, text);
  }

  void addToMod(std::string_view path, std::string_view text) {
    memory.addFile(std::filesystem::path{"packs/mod"} / path, text);
  }

  assets::VirtualFileSystem& mount() {
    const auto location = [this](std::string_view root, assets::PackOrigin origin) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = origin};
    };
    const std::vector<assets::PackLocation> locations{
        location("packs/base", assets::PackOrigin::Engine),
        location("packs/game", assets::PackOrigin::Engine),
        location("packs/test_contrast", assets::PackOrigin::Mod),
        location("packs/tests", assets::PackOrigin::Engine),
        location("packs/mod", assets::PackOrigin::Mod)};
    auto mounted = assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
    return *files;
  }
};

localization::Catalog catalogIn(const assets::VirtualFileSystem& files, std::string_view language) {
  auto catalog = localization::Catalog::load(files, language);
  REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
  return std::move(*catalog);
}

// A clock the tests move by hand, for hot reload.
struct FakeClock {
  double now = 0.0;
};

std::unique_ptr<Ui> createUi(const assets::VirtualFileSystem& files,
                             const localization::Catalog& texts,
                             FakeClock* hotReloadClock = nullptr) {
  UiDesc desc{
      .files = &files, .texts = &texts, .width = 1280, .height = 720, .clock = {}, .scripts = {}};
  if (hotReloadClock != nullptr) {
    desc.hotReload = true;
    desc.clock = [hotReloadClock] { return hotReloadClock->now; };
  }
  auto ui = Ui::create(desc);
  REQUIRE_MESSAGE(ui.has_value(), ui.error().message);
  return std::move(*ui);
}

// An opaque white RGBA PNG, for sprite sheets.
std::vector<std::byte> whitePng(unsigned width, unsigned height) {
  const std::vector<unsigned char> pixels(std::size_t{width} * height * 4, 255);
  std::vector<unsigned char> png;
  REQUIRE(lodepng::encode(png, pixels, width, height) == 0);
  const auto* begin = reinterpret_cast<const std::byte*>(png.data());
  return {begin, begin + png.size()};
}

// White stand-ins, of the disc's sizes, for the images dwx extracts that the original skin draws
// its sprites from: the skin is offered only when the packs have them.
void addOriginalArt(Content& content) {
  struct Stand {
    std::string_view path;
    unsigned width;
    unsigned height;
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
        Stand{.path = "map/trai00/texture4/001.png", .width = 256, .height = 256}}) {
    content.memory.addFile(std::filesystem::path{"packs/base"} / image.path,
                           whitePng(image.width, image.height));
  }
}

void loadScreens(Ui& ui) {
  for (const std::string_view screen : Screens) {
    REQUIRE(ui.loadDocument(screen, idOf(std::format("ui/{}/document", screen))));
  }
}

void frame(Ui& ui, input::InputState& input) {
  ui.tick(input, Tick);
  input.endTick();
  ui.update();
}

void press(Ui& ui, input::InputState& input, platform::KeyCode key) {
  input.handle(platform::KeyChanged{.key = key, .down = true, .repeat = false});
  frame(ui, input);
  input.handle(platform::KeyChanged{.key = key, .down = false, .repeat = false});
  frame(ui, input);
}

// Clicks an element with the mouse; whether the UI took the press and the release.
bool clickElement(Ui& ui, std::string_view document, std::string_view element) {
  const auto found = ui.elementCenter(document, element);
  REQUIRE_MESSAGE(found.has_value(), element);
  const Ui::Point center = found.value_or(Ui::Point{});
  static_cast<void>(ui.handleEvent(platform::MouseMoved{.x = center.x, .y = center.y}));
  const bool down = ui.handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = true});
  const bool up = ui.handleEvent(
      platform::MouseButtonChanged{.button = platform::MouseButton::Left, .down = false});
  ui.update();
  return down && up;
}

// The colour of every screen's text, from its body.
std::vector<std::string> textColours(const Ui& ui) {
  std::vector<std::string> colours;
  colours.reserve(Screens.size());
  for (const std::string_view screen : Screens) {
    colours.push_back(ui.styleProperty(screen, "", "color"));
  }
  return colours;
}

std::vector<std::string> skinIds(const Ui& ui) {
  std::vector<std::string> ids;
  ids.reserve(ui.skins().skins().size());
  for (const Skin& skin : ui.skins().skins()) {
    ids.push_back(skin.id);
  }
  return ids;
}

// A skin of the tests' pack that lays the test screen out with its own document, keeping its ids
// and script.
constexpr std::string_view FramedSkin =
    "id: framed\nname: ui_skin.framed\nextends: modern\n"
    "screens:\n  test:\n    document: ui/skins/framed/test/document\n";
constexpr std::string_view TestDocument = "<rml><head><script src='logic.lua'/></head>"
                                          "<body><p id='label'>own</p></body></rml>";
constexpr std::string_view FramedDocument =
    "<rml><head><script src='/ui/test/logic.lua'/></head>"
    "<body><div><p id='label'>framed</p></div></body></rml>";
constexpr std::string_view TestLogic = "function greet() ui.send('greeted') end\n";

// The warnings logged while one lives. A log sink cannot be removed, so one sink, added once, feeds
// the capture alive.
class WarningCapture {
public:
  WarningCapture() {
    static const bool added = [] {
      core::addLogSink([](core::LogLevel level, std::string_view message) {
        if (current != nullptr && level == core::LogLevel::Warning) {
          current->warnings_.emplace_back(message);
        }
      });
      return true;
    }();
    static_cast<void>(added);
    current = this;
  }
  ~WarningCapture() { current = nullptr; }
  WarningCapture(const WarningCapture&) = delete;
  WarningCapture& operator=(const WarningCapture&) = delete;
  WarningCapture(WarningCapture&&) = delete;
  WarningCapture& operator=(WarningCapture&&) = delete;

  [[nodiscard]] bool has(std::string_view text) const {
    return std::ranges::any_of(warnings_,
                               [text](const std::string& line) { return line.contains(text); });
  }

  [[nodiscard]] const std::vector<std::string>& all() const { return warnings_; }

private:
  static inline WarningCapture* current = nullptr;
  std::vector<std::string> warnings_;
};

// Every skin file the game pack and the test mods ship: their style sheets and replacement
// documents.
std::vector<std::filesystem::path> shippedSkinFiles() {
  std::vector<std::filesystem::path> found;
  const std::filesystem::path testMods =
      std::filesystem::path{DWE_SKIN_TEST_PACK}.parent_path().parent_path();
  for (const std::filesystem::path& root : {std::filesystem::path{DWE_GAME_PACK}, testMods}) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
      const std::string path = assets::pathText(entry.path());
      const std::string extension = assets::pathText(entry.path().extension());
      if (entry.is_regular_file() && path.contains("/ui/skins/") &&
          (extension == ".rcss" || extension == ".rml")) {
        found.push_back(entry.path());
      }
    }
  }
  return found;
}

} // namespace

TEST_CASE(
    "without the disc's art the UI starts with the fallback, modern, and a mod pack adds a skin") {
  Content content;
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "es");
  const WarningCapture warnings;
  auto ui = createUi(files, texts);
  CHECK(skinIds(*ui) == std::vector<std::string>{"modern", "test_contrast"});
  CHECK(warnings.has("the skin original is left out"));
  CHECK(ui->skins().defaultSkin() == "modern");
  CHECK(ui->skin() == "modern");
  const Skin* added = ui->skins().find("test_contrast");
  REQUIRE(added != nullptr);
  CHECK(texts.text(added->nameKey) == "Contraste de prueba");
  CHECK(texts.text(ui->skins().find("modern")->nameKey) == "Moderno");
}

TEST_CASE(
    "with the disc's art the UI starts with the original skin, and every screen loads under it cleanly") {
  Content content;
  addOriginalArt(content);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "es");
  const WarningCapture warnings;
  auto ui = createUi(files, texts);
  CHECK(skinIds(*ui) == std::vector<std::string>{"modern", "original", "test_contrast"});
  CHECK(ui->skins().defaultSkin() == "original");
  CHECK(ui->skin() == "original");
  CHECK(texts.text(ui->skins().find("original")->nameKey) == "Original");
  loadScreens(*ui);
  // The screens that share the pause menu's panel, whose panel the skin marks as its own
  // (skin-original), so the modern look's rules stay off it.
  for (const std::string_view screen : PanelScreens) {
    REQUIRE(ui->loadDocument(screen, idOf(std::format("ui/{}/document", screen))));
  }
  CHECK(ui->checkSkinDocuments().empty());
  CHECK(ui->elementHasClass("pause_menu", "panel", "skin-original"));
  CHECK(ui->elementHasClass("dialog", "panel", "skin-original"));
  // The skin marks the field bars to fill in whole pips, as the original.
  CHECK(ui->elementHasClass("hud", "field-meter-0-bar", "pips"));
  // Nothing in the skin is refused (RmlUi's own complaints come as warnings too). The stand-in
  // packs have no bitmap font, which is warned about whatever the skin.
  for (const std::string& line : warnings.all()) {
    CHECK_MESSAGE(line.contains("font/main"), line);
  }
  // A switch away takes the classes off, and back puts them on again.
  REQUIRE(ui->setSkin("modern"));
  CHECK_FALSE(ui->elementHasClass("hud", "field-meter-0-bar", "pips"));
  CHECK_FALSE(ui->elementHasClass("pause_menu", "panel", "skin-original"));
  REQUIRE(ui->setSkin("original"));
  CHECK(ui->elementHasClass("hud", "field-meter-0-bar", "pips"));
}

namespace {

// The happiness face the original skin draws now: the sprite named in the icon's decorator.
std::string happinessFace(const Ui& ui) {
  return ui.styleProperty("hud", "field-meter-0-label", "decorator");
}

void showFieldStatus(Ui& ui, double happiness, double area) {
  ui.setSetting("hud.shown", true);
  ui.setSetting("hud.meter.0.value", happiness);
  ui.setSetting("hud.meter.0.min", -100.0);
  ui.setSetting("hud.meter.0.max", 100.0);
  ui.setSetting("hud.partner.area", area);
  const UiValue current = ui.setting("hud.revision");
  const double* count = std::get_if<double>(&current);
  ui.setSetting("hud.revision", (count == nullptr ? 0.0 : *count) + 1.0);
  ui.update();
}

} // namespace

TEST_CASE(
    "in an area the partner likes or dislikes, the original skin's face alternates every 0.25 s as the original's") {
  // partner_impl.c: renderStatusBars: while PLAYTIME_FRAMES % 10 < 5, 5 of the original's 20 frames
  // a second, the face is the one 11 pixels to the left (liked) or to the right (disliked); then
  // the usual one for 5 frames.
  constexpr double FlickerHalf = 5.0 / 20.0;
  constexpr double Inside = 0.1;
  Content content;
  addOriginalArt(content);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  FakeClock clock;
  UiDesc desc{
      .files = &files, .texts = &texts, .width = 1280, .height = 720, .clock = {}, .scripts = {}};
  desc.clock = [&clock] { return clock.now; };
  auto created = Ui::create(desc);
  REQUIRE_MESSAGE(created.has_value(), created.error().message);
  Ui& ui = **created;
  REQUIRE(ui.skin() == "original");
  REQUIRE(ui.loadDocument("hud", idOf("ui/hud/document")));
  ui.show("hud");
  ui.update();

  // The face after running the UI frame by frame, as the game does, up to `seconds`.
  const auto faceAt = [&](double seconds) {
    while (clock.now < seconds) {
      clock.now = std::min(seconds, clock.now + Tick);
      ui.update();
    }
    return happinessFace(ui);
  };
  struct Case {
    double happiness;
    double area;
    std::string_view shown;
    std::string_view usual;
  };
  for (const Case& area :
       {Case{.happiness = 10.0, .area = 1.0, .shown = "face-happy-liked", .usual = "face-happy"},
        Case{.happiness = -10.0, .area = 1.0, .shown = "face-sad-liked", .usual = "face-sad"},
        Case{.happiness = 10.0, .area = 2.0, .shown = "face-happy-disliked", .usual = "face-happy"},
        Case{.happiness = -10.0, .area = 2.0, .shown = "face-sad-disliked", .usual = "face-sad"}}) {
    CAPTURE(area.shown);
    const double start = clock.now + 1.0;
    static_cast<void>(faceAt(start));
    showFieldStatus(ui, area.happiness, area.area);
    // The animation starts on the frame after the script sets the class, a frame after `start`.
    CHECK(faceAt(start + Inside).contains(area.shown));
    const std::string usual = faceAt(start + FlickerHalf + Inside);
    CHECK(usual.contains(area.usual));
    CHECK_FALSE(usual.contains(area.shown));
    CHECK(faceAt(start + (2 * FlickerHalf) + Inside).contains(area.shown));
    CHECK_FALSE(faceAt(start + (3 * FlickerHalf) + Inside).contains(area.shown));
    // The script marks the status with the area; the skin swaps the face on that class.
    CHECK(ui.elementHasClass("hud", "field-status",
                             area.area == 1.0 ? "area-liked" : "area-disliked"));
    CHECK_FALSE(ui.elementHasClass("hud", "field-status",
                                   area.area == 1.0 ? "area-disliked" : "area-liked"));
  }

  // In an area it neither likes nor dislikes, the face holds still.
  const double start = clock.now + 1.0;
  static_cast<void>(faceAt(start));
  showFieldStatus(ui, 10.0, 0.0);
  CHECK_FALSE(ui.elementHasClass("hud", "field-status", "area-liked"));
  CHECK_FALSE(ui.elementHasClass("hud", "field-status", "area-disliked"));
  for (const double later : {Inside, FlickerHalf + Inside, (2 * FlickerHalf) + Inside}) {
    const std::string face = faceAt(start + later);
    CHECK(face.contains("face-happy"));
    CHECK_FALSE(face.contains("liked"));
  }
}

TEST_CASE(
    "the original skin's award and item notices are the original's box, rows and animated confirm button") {
  // tamer.c: the player's award and item pickup states open the box at (-130, 42) from the
  // screen's centre, 262 x 59 (x 30, y 162 of the 320 x 240 screen: 3 dp a pixel, the 4:3 screen
  // from x 160 of 1280); renderAwardSomethingTextbox and renderItemPickupTextbox copy the text
  // area's 12-pixel rows to y 45 from the centre (165) on, and script_textbox.c: renderUIBox draws
  // the confirm button, 16 x 12, at (239, 43) from the box's corner, one of three drawings for 4 of
  // the field's 20 frames a second.
  constexpr double Frame = 1.0 / 20.0;
  Content content;
  addOriginalArt(content);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  FakeClock clock;
  UiDesc desc{
      .files = &files, .texts = &texts, .width = 1280, .height = 720, .clock = {}, .scripts = {}};
  desc.clock = [&clock] { return clock.now; };
  auto created = Ui::create(desc);
  REQUIRE_MESSAGE(created.has_value(), created.error().message);
  Ui& ui = **created;
  REQUIRE(ui.skin() == "original");
  ui.setSetting("award_notice.name", std::string{"Agumon"});
  ui.setSetting("award_notice.lines", std::string{"a,b,c"});
  ui.setSetting("field_item_notice.item", std::string{"Meat"});

  struct Notice {
    std::string_view document;
    std::vector<std::string_view> rows;
  };
  const auto pixel = [](float dp) { return dp / 3.0F; };
  for (const Notice& notice :
       {Notice{.document = "award_notice", .rows = {"line-1", "line-2", "line-3"}},
        Notice{.document = "field_item_notice", .rows = {"item", "text"}}}) {
    CAPTURE(notice.document);
    REQUIRE(ui.loadDocument(notice.document, idOf(std::format("ui/{}/document", notice.document))));
    ui.show(notice.document);
    ui.update();
    const auto box = ui.elementRect(notice.document, "box");
    REQUIRE(box.has_value());
    CHECK(pixel(box->left - 160.0F) == doctest::Approx(30.0));
    CHECK(pixel(box->top) == doctest::Approx(162.0));
    CHECK(pixel(box->right - box->left) == doctest::Approx(262.0));
    CHECK(pixel(box->bottom - box->top) == doctest::Approx(59.0));
    // The box's look is the speech box's: the original's border, not the modern grey line.
    CHECK(ui.styleProperty(notice.document, "box", "decorator").contains("original-box"));
    // UI_BOX_COLORS 0, black, at half (createAnimatedUIBox color 0, feature 2).
    CHECK(ui.styleProperty(notice.document, "box", "background-color") == "#00000080");
    for (std::size_t row = 0; row < notice.rows.size(); ++row) {
      CAPTURE(row);
      const auto line = ui.elementRect(notice.document, notice.rows[row]);
      REQUIRE(line.has_value());
      CHECK(pixel(line->top) == doctest::Approx(165.0 + (12.0 * static_cast<double>(row))));
    }
    const auto icon = ui.elementRect(notice.document, "confirm-icon");
    REQUIRE(icon.has_value());
    CHECK(pixel(icon->left - box->left) == doctest::Approx(239.0));
    CHECK(pixel(icon->top - box->top) == doctest::Approx(43.0));
    CHECK(pixel(icon->right - icon->left) == doctest::Approx(16.0));
    CHECK(pixel(icon->bottom - icon->top) == doctest::Approx(12.0));

    // The drawing after running the UI frame by frame, as the game does, up to `seconds`.
    const auto drawingAt = [&](double seconds) {
      while (clock.now < seconds) {
        clock.now = std::min(seconds, clock.now + Tick);
        ui.update();
      }
      return ui.styleProperty(notice.document, "confirm-icon", "decorator");
    };
    // The animation starts on the frame after the document shows.
    const double start = clock.now + Tick;
    for (int turn = 0; turn < 2; ++turn) {
      for (int drawing = 0; drawing < 3; ++drawing) {
        CAPTURE(turn);
        CAPTURE(drawing);
        const double at = start + Frame + (((turn * 3) + drawing) * 4 * Frame);
        CHECK(drawingAt(at).contains(std::format("confirm-icon-{}", drawing)));
      }
    }
    ui.hide(notice.document);
  }

  // The modern skin keeps its own box, without the button.
  REQUIRE(ui.setSkin("modern"));
  for (const std::string_view document : {"award_notice", "field_item_notice"}) {
    ui.show(document);
    ui.update();
    CHECK(ui.drawnRectsOf(document, "confirm-icon").empty());
  }
}

TEST_CASE("a live switch restyles every document in place and keeps its state") {
  Content content;
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  loadScreens(*ui);
  ui->setSetting("language", std::string{"en"});
  input::InputState input{input::defaultActionMap()};
  ui->show("hud");
  ui->show("main_menu", true);
  frame(*ui, input);
  press(*ui, input, input::keys::Down);
  REQUIRE(ui->focusedElementId("main_menu") == "language");
  const std::vector<std::string> modern = textColours(*ui);
  CHECK_FALSE(modern.front().empty());

  REQUIRE(ui->setSkin("test_contrast"));
  frame(*ui, input);
  CHECK(ui->skin() == "test_contrast");
  const std::vector<std::string> contrast = textColours(*ui);
  for (std::size_t screen = 0; screen < Screens.size(); ++screen) {
    CAPTURE(Screens.at(screen));
    CHECK(contrast.at(screen) != modern.at(screen));
    CHECK(contrast.at(screen) == contrast.front());
  }
  // The documents stayed: the menu is still open on the same entry, and the language entry still
  // works.
  CHECK(ui->isVisible("main_menu"));
  CHECK(ui->focusedElementId("main_menu") == "language");
  CHECK(ui->elementText("main_menu", "language-value") == "English");

  REQUIRE(ui->setSkin("modern"));
  frame(*ui, input);
  CHECK(textColours(*ui) == modern);

  CHECK_FALSE(ui->setSkin("missing"));
  CHECK(ui->skin() == "modern");
}

TEST_CASE("a skin that lays a screen out with its own document keeps the screen's ids and script") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  content.add("ui/skins/framed/skin.yaml", FramedSkin);
  content.add("ui/skins/framed/test/document.rml", FramedDocument);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  ui->show("test");
  CHECK(ui->elementText("test", "label") == "own");

  REQUIRE(ui->setSkin("framed"));
  CHECK(ui->isVisible("test"));
  CHECK(ui->elementText("test", "label") == "framed");
  REQUIRE(ui->callScript("test", "greet"));
  CHECK(ui->takeCommands().size() == 1);

  REQUIRE(ui->setSkin("modern"));
  CHECK(ui->elementText("test", "label") == "own");
}

TEST_CASE(
    "a skin that cannot be read, lacks an asset or extends a skin that is not offered is left out") {
  Content content;
  content.add("ui/skins/renamed/skin.yaml", "id: other\nname: ui_skin.renamed\n");
  content.add("ui/skins/no_style/skin.yaml",
              "id: no_style\nname: ui_skin.no_style\ntheme: [ui/skins/no_style/x]\n");
  content.add("ui/skins/orphan/skin.yaml", "id: orphan\nname: ui_skin.orphan\nextends: nowhere\n");
  content.add("ui/skins/loop_a/skin.yaml", "id: loop_a\nname: ui_skin.loop_a\nextends: loop_b\n");
  content.add("ui/skins/loop_b/skin.yaml", "id: loop_b\nname: ui_skin.loop_b\nextends: loop_a\n");
  content.add("ui/skins/broken/skin.yaml", "id: broken\nname: [\n");
  content.add("ui/skins/framed/skin.yaml", FramedSkin);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  const WarningCapture warnings;
  auto ui = createUi(files, texts);
  // framed names a document the packs lack.
  CHECK(skinIds(*ui) == std::vector<std::string>{"modern", "test_contrast"});
  CHECK(warnings.has("the skin orphan is left out: it extends 'nowhere', which is not offered"));
  CHECK(warnings.has("the skin loop_a is left out: it extends"));
  CHECK(warnings.has("the skin loop_b is left out: it extends"));
  CHECK(warnings.has("which comes back to it through extends"));
}

TEST_CASE(
    "without a skin the packs name, the UI starts with none and the documents keep their own style") {
  Content content;
  content.add("ui/ui_skin.yaml", "default: nowhere\n");
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  CHECK(ui->skin().empty());
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  ui->update();
  const std::string own = ui->styleProperty("test", "", "color");
  REQUIRE(ui->setSkin("modern"));
  ui->update();
  CHECK(ui->styleProperty("test", "", "color") != own);
}

TEST_CASE("a default the packs do not offer gives way to the fallback ui/ui_skin.yaml names") {
  Content content;
  content.add("ui/ui_skin.yaml", "default: nowhere\nfallback: modern\n");
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  CHECK(ui->skins().defaultSkin() == "modern");
  CHECK(ui->skin() == "modern");
}

// A skin of the tests' pack that marks elements of the shared HUD document, one of them an id the
// HUD lacks.
constexpr std::string_view ClassySkin = "id: classy\nname: ui_skin.classy\nextends: modern\n"
                                        "document_classes:\n"
                                        "  hud:\n"
                                        "    field-meter-0-bar: [pips, wide]\n"
                                        "    field-pool-1-bar: [pips]\n"
                                        "    no-such-element: [pips]\n";

TEST_CASE("a skin's document classes go on when a document loads and come off on a switch") {
  Content content;
  content.add("ui/skins/classy/skin.yaml", ClassySkin);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  REQUIRE(ui->setSkin("classy"));
  const WarningCapture warnings;
  loadScreens(*ui);
  CHECK(ui->elementHasClass("hud", "field-meter-0-bar", "pips"));
  CHECK(ui->elementHasClass("hud", "field-meter-0-bar", "wide"));
  CHECK(ui->elementHasClass("hud", "field-pool-1-bar", "pips"));
  CHECK_FALSE(ui->elementHasClass("hud", "field-meter-1-bar", "pips"));
  CHECK(warnings.has("#no-such-element"));

  REQUIRE(ui->setSkin("modern"));
  CHECK_FALSE(ui->elementHasClass("hud", "field-meter-0-bar", "pips"));
  CHECK_FALSE(ui->elementHasClass("pause_menu", "panel", "skin-original"));
  CHECK_FALSE(ui->elementHasClass("hud", "field-meter-0-bar", "wide"));
  CHECK_FALSE(ui->elementHasClass("hud", "field-pool-1-bar", "pips"));
  // A class of the document's own stays: only the classes a skin names come off.
  CHECK(ui->elementHasClass("hud", "minimap", "size-medium"));

  REQUIRE(ui->setSkin("classy"));
  CHECK(ui->elementHasClass("hud", "field-meter-0-bar", "pips"));
}

// Happiness 30 of -100 to 100 in two tones: the upper tone fills 30 % of its half, or 28 % in whole
// pips of 4 %.
void setHappiness(Ui& ui) {
  ui.setSetting("hud.meter.0.value", 30.0);
  ui.setSetting("hud.meter.0.min", -100.0);
  ui.setSetting("hud.meter.0.max", 100.0);
  ui.setSetting("hud.meter.0.two_tone", true);
}

TEST_CASE(
    "a skin's classes are on before the HUD first draws and before it redraws on the switch's ui_skin") {
  SUBCASE("a document loaded under the skin draws with them when it shows") {
    Content content;
    content.add("ui/skins/classy/skin.yaml", ClassySkin);
    const assets::VirtualFileSystem& files = content.mount();
    const localization::Catalog texts = catalogIn(files, "en");
    auto ui = createUi(files, texts);
    REQUIRE(ui->setSkin("classy"));
    loadScreens(*ui);
    setHappiness(*ui);
    ui->show("hud");
    ui->update();
    CHECK(ui->styleProperty("hud", "field-meter-0-high", "width") == "28%");
  }
  SUBCASE("a live switch has them on when the game publishes ui_skin, which redraws the HUD") {
    Content content;
    content.add("ui/skins/classy/skin.yaml", ClassySkin);
    const assets::VirtualFileSystem& files = content.mount();
    const localization::Catalog texts = catalogIn(files, "en");
    auto ui = createUi(files, texts);
    loadScreens(*ui);
    setHappiness(*ui);
    ui->show("hud");
    ui->update();
    CHECK(ui->styleProperty("hud", "field-meter-0-high", "width") == "30%");
    // What the game does on a change of skin (apps/game UiSkinChanger): switch, then publish the
    // setting.
    REQUIRE(ui->setSkin("classy"));
    ui->setSetting(std::string{UiSkinSetting}, std::string{"classy"});
    ui->update();
    CHECK(ui->styleProperty("hud", "field-meter-0-high", "width") == "28%");
    REQUIRE(ui->setSkin("modern"));
    ui->setSetting(std::string{UiSkinSetting}, std::string{"modern"});
    ui->update();
    CHECK(ui->styleProperty("hud", "field-meter-0-high", "width") == "30%");
  }
}

TEST_CASE("a skin's classes for a document the game does not load are a warning") {
  Content content;
  content.add("ui/skins/classy/skin.yaml",
              "id: classy\nname: ui_skin.classy\nextends: modern\n"
              "document_classes:\n  huds:\n    field-status: [pips]\n");
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  loadScreens(*ui);
  REQUIRE(ui->setSkin("classy"));
  const WarningCapture warnings;
  CHECK(ui->checkSkinDocuments() == std::vector<std::string>{"huds"});
  CHECK(warnings.has("document huds"));
}

TEST_CASE(
    "the menus' skin entry sends the next skin the packs offer and shows the one the game reports") {
  Content content;
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "es");
  auto ui = createUi(files, texts);
  loadScreens(*ui);
  ui->setSetting("language", std::string{"es"});
  ui->setSetting(std::string{UiSkinSetting}, std::string{"modern"});
  input::InputState input{input::defaultActionMap()};
  ui->show("main_menu", true);
  frame(*ui, input);
  CHECK(ui->elementText("main_menu", "ui-skin-value") == "Moderno");
  while (ui->focusedElementId("main_menu") != "ui-skin") {
    REQUIRE(ui->focusedElementId("main_menu") != "quit");
    press(*ui, input, input::keys::Down);
  }
  press(*ui, input, input::keys::Right);
  auto commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].name == UiSkinSetting);
  CHECK(commands[0].value == UiValue{std::string{"test_contrast"}});
  // With two skins, left wraps around to the other one too.
  press(*ui, input, input::keys::Left);
  commands = ui->takeCommands();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].value == UiValue{std::string{"test_contrast"}});

  // The menu shows what the game reports back.
  ui->setSetting(std::string{UiSkinSetting}, std::string{"test_contrast"});
  frame(*ui, input);
  CHECK(ui->elementText("main_menu", "ui-skin-value") == "Contraste de prueba");

  // The pause menu's options page has the same entry.
  ui->hide("main_menu");
  ui->show("pause_menu", true);
  frame(*ui, input);
  CHECK(ui->elementText("pause_menu", "ui-skin-value") == "Contraste de prueba");
  // The mod's texts add to the game pack's: the modern skin keeps its name.
  ui->setSetting(std::string{UiSkinSetting}, std::string{"modern"});
  frame(*ui, input);
  CHECK(ui->elementText("pause_menu", "ui-skin-value") == "Moderno");
}

TEST_CASE("sprite sheets fold along extends: the nearest skin's sheet and sprite win") {
  Content content;
  content.memory.addFile("packs/tests/ui/test/sheet.png", whitePng(32, 32));
  content.add("ui/skins/sprites_base/skin.yaml",
              "id: sprites_base\nname: ui_skin.modern\nsprite_sheets:\n"
              "- {name: frames, image: ui/test/sheet, "
              "sprites: {corner: [0, 0, 8, 8], edge: [8, 0, 8, 8]}}\n"
              "- {name: icons, image: ui/test/sheet, sprites: {star: [0, 8, 8, 8]}}\n");
  content.add("ui/skins/sprites_near/skin.yaml",
              "id: sprites_near\nname: ui_skin.modern\nextends: sprites_base\n"
              "sprite_sheets:\n"
              "- {name: frames, image: ui/test/sheet, sprites: {corner: [16, 0, 8, 8]}}\n"
              "- {name: extra, image: ui/test/sheet, sprites: {star: [16, 8, 8, 8]}}\n");
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  const WarningCapture warnings;
  auto ui = createUi(files, texts);
  CHECK(warnings.has(
      "the skin sprites_near names the sprite star in the sprite sheets extra and icons; the one in "
      "extra is used"));
  const Skin* near = ui->skins().find("sprites_near");
  REQUIRE(near != nullptr);
  const std::vector<SkinSpriteSheet> sheets = ui->skins().spriteSheetsOf(*near);
  // frames is the near skin's own (corner moved, edge gone with the base's sheet); star is extra's,
  // so icons, left with no sprites, is dropped.
  REQUIRE(sheets.size() == 2);
  CHECK(sheets[0].name == "frames");
  REQUIRE(sheets[0].sprites.size() == 1);
  CHECK(sheets[0].sprites[0].x == 16);
  CHECK(sheets[1].name == "extra");
  REQUIRE(sheets[1].sprites.size() == 1);
  CHECK(sheets[1].sprites[0].x == 16);

  const Skin* base = ui->skins().find("sprites_base");
  REQUIRE(base != nullptr);
  CHECK(ui->skins().spriteSheetsOf(*base).size() == 2);
  REQUIRE(ui->setSkin("sprites_near"));
}

TEST_CASE("a switch whose document cannot load goes back to the skin the UI had and fails") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  content.add("ui/skins/framed/skin.yaml", FramedSkin);
  content.add("ui/skins/framed/test/document.rml", FramedDocument);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  loadScreens(*ui);
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  ui->show("test");
  ui->update();
  const std::vector<std::string> modern = textColours(*ui);

  // The replacement document is gone after the packs were read.
  content.memory.removeFile("packs/tests/ui/skins/framed/test/document.rml");
  const auto switched = ui->setSkin("framed");
  REQUIRE_FALSE(switched.has_value());
  CHECK(switched.error().message.contains("modern"));
  CHECK(ui->skin() == "modern");
  CHECK(ui->isVisible("test"));
  CHECK(ui->elementText("test", "label") == "own");
  ui->update();
  CHECK(textColours(*ui) == modern);
  REQUIRE(ui->setSkin("test_contrast"));
}

TEST_CASE("hot reload picks up an edited style sheet of the current skin") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  content.add(
      "ui/skins/edited/skin.yaml",
      "id: edited\nname: ui_skin.modern\nextends: modern\ntheme: [ui/skins/edited/theme]\n");
  content.add("ui/skins/edited/theme.rcss", "body { color: #102030; }\n");
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  FakeClock clock;
  auto ui = createUi(files, texts, &clock);
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  ui->show("test");
  REQUIRE(ui->setSkin("edited"));
  ui->update();
  const std::string before = ui->styleProperty("test", "", "color");

  content.add("ui/skins/edited/theme.rcss", "body { color: #405060; }\n");
  clock.now = 1.0;
  ui->reloadChanged();
  ui->update();
  const std::string after = ui->styleProperty("test", "", "color");
  CHECK(after != before);
  CHECK(ui->skin() == "edited");
  CHECK(ui->isVisible("test"));
}

TEST_CASE(
    "a failed switch with two documents to replace keeps both on their own document and look, shown once") {
  Content content;
  constexpr std::string_view ShowingLogic = "function on_show(document) ui.send('shown') end\n";
  for (const std::string_view screen : {"first", "second"}) {
    content.add(std::format("ui/{}/document.rml", screen), TestDocument);
    content.add(std::format("ui/{}/logic.lua", screen), ShowingLogic);
    content.add(std::format("ui/skins/framed/{}/document.rml", screen),
                std::format("<rml><head><script src='/ui/{}/logic.lua'/></head>"
                            "<body><div><p id='label'>framed</p></div></body></rml>",
                            screen));
  }
  content.add("ui/skins/framed/skin.yaml",
              "id: framed\nname: ui_skin.framed\nextends: modern\nscreens:\n"
              "  first:\n    document: ui/skins/framed/first/document\n"
              "  second:\n    document: ui/skins/framed/second/document\n");
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  for (const std::string_view screen : {"first", "second"}) {
    REQUIRE(ui->loadDocument(screen, idOf(std::format("ui/{}/document", screen))));
    ui->show(screen);
  }
  ui->update();
  const std::string modern = ui->styleProperty("first", "", "color");
  static_cast<void>(ui->takeCommands());

  // The first replacement loads; the second is gone after the packs were read.
  content.memory.removeFile("packs/tests/ui/skins/framed/second/document.rml");
  const auto switched = ui->setSkin("framed");
  REQUIRE_FALSE(switched.has_value());
  CHECK(switched.error().message.contains("second"));
  CHECK(ui->skin() == "modern");
  ui->update();
  for (const std::string_view screen : {"first", "second"}) {
    CAPTURE(screen);
    CHECK(ui->isVisible(screen));
    CHECK(ui->elementText(screen, "label") == "own");
    CHECK(ui->styleProperty(screen, "", "color") == modern);
  }
  // Nothing was swapped, so nothing was shown again.
  CHECK(ui->takeCommands().empty());
}

TEST_CASE("a skin does not change navigation: no shipped skin sets a nav property") {
  const std::vector<std::filesystem::path> skinFiles = shippedSkinFiles();
  REQUIRE_FALSE(skinFiles.empty());
  for (const std::filesystem::path& file : skinFiles) {
    CAPTURE(assets::pathText(file));
    std::ifstream stream{file, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    CHECK_FALSE(text.contains("nav-"));
  }
}

TEST_CASE("no shipped skin paints a box's fill over its original-box border") {
  // RmlUi paints the first decorator of a list on top, as CSS does with backgrounds: a gradient
  // listed before original-box hides the border's lines and corner sprites.
  const std::vector<std::filesystem::path> skinFiles = shippedSkinFiles();
  REQUIRE_FALSE(skinFiles.empty());
  int bordered = 0;
  for (const std::filesystem::path& file : skinFiles) {
    std::ifstream stream{file, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    for (std::size_t at = text.find("decorator:"); at != std::string::npos;
         at = text.find("decorator:", at + 1)) {
      const std::string list = text.substr(at, text.find(';', at) - at);
      const std::size_t border = list.find("original-box");
      if (border == std::string::npos) {
        continue;
      }
      ++bordered;
      CAPTURE(assets::pathText(file));
      CAPTURE(list);
      CHECK(list.find("gradient(") > border);
    }
  }
  CHECK(bordered > 0);
}

// A replacement document with the game's script, a script of its own, an inline script and an event
// attribute.
constexpr std::string_view OwnLuaDocument =
    "<rml><head><script src='/ui/test/logic.lua'/><script src='own.lua'/>"
    "<script>ui.send('inline')</script></head><body>"
    "<p id='label'>replaced</p>"
    "<button id='first' autofocus='autofocus' "
    "onclick=\"ui.send('pressed')\">One</button></body></rml>";

TEST_CASE("a mod's skin lays a screen out with the game's script, but none of its own Lua runs") {
  Content content;
  content.add("ui/test/document.rml", TestDocument);
  content.add("ui/test/logic.lua", TestLogic);
  content.addToMod("ui/skins/modded/skin.yaml",
                   "id: modded\nname: ui_skin.modern\nextends: modern\nscreens:\n"
                   "  test:\n    document: ui/skins/modded/test/document\n");
  content.addToMod("ui/skins/modded/test/document.rml", OwnLuaDocument);
  content.addToMod("ui/skins/modded/test/own.lua", "function greet() ui.send('hijacked') end\n");
  // The same skin in an engine pack, to show the same document's Lua does run there.
  content.add("ui/skins/trusted/skin.yaml",
              "id: trusted\nname: ui_skin.modern\nextends: modern\nscreens:\n"
              "  test:\n    document: ui/skins/trusted/test/document\n");
  content.add("ui/skins/trusted/test/document.rml", OwnLuaDocument);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  REQUIRE(ui->loadDocument("test", idOf("ui/test/document")));
  ui->show("test", true);
  REQUIRE(ui->setSkin("modded"));
  CHECK(ui->elementText("test", "label") == "replaced");
  // The inline script did not run, and the game's greet is the one that answers, not the mod's
  // own.lua.
  CHECK(ui->takeCommands().empty());
  REQUIRE(ui->callScript("test", "greet"));
  const auto greeted = ui->takeCommands();
  REQUIRE(greeted.size() == 1);
  CHECK(greeted[0].name == "greeted");
  // Neither does its onclick, though the click reaches the button.
  input::InputState input{input::defaultActionMap()};
  frame(*ui, input);
  CHECK(clickElement(*ui, "test", "first"));
  CHECK(ui->takeCommands().empty());

  REQUIRE(ui->setSkin("trusted"));
  frame(*ui, input);
  CHECK(clickElement(*ui, "test", "first"));
  std::vector<std::string> names;
  for (const UiCommand& command : ui->takeCommands()) {
    names.push_back(command.name);
  }
  CHECK(names == std::vector<std::string>{"inline", "pressed"});
}

TEST_CASE(
    "the original skin's Tech page: no Final range, the help page on the extra action, out on cancel") {
  // game_menu.c: tickDigimonMenu and renderDigimonMovesView (MENU_STATE 6 to 9).
  Content content;
  addOriginalArt(content);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  REQUIRE(ui->skin() == "original");
  REQUIRE(ui->loadDocument("partner_menu", idOf("ui/partner_menu/document")));
  // The game's mirror of the skin for the scripts (apps/game/src/ui_skin_setting.cpp).
  ui->setSetting("ui_skin", std::string{"original"});
  for (const int slot : {0, 3}) {
    const std::string base = std::format("partner.technique.{}.", slot);
    ui->setSetting(base + "empty", false);
    ui->setSetting(base + "name", std::string{"Spit Fire"});
    ui->setSetting(base + "range", 3.0);
  }
  ui->setSetting("partner.technique.choice.0.available", true);
  ui->setSetting("partner.technique.choice.0.known", true);
  ui->setSetting("partner.technique.choice.0.equipped", true);
  ui->setSetting("partner.technique.choice.0.slot", 0.0);
  ui->setSetting("partner.technique.can_finish", true);
  input::InputState input{input::defaultActionMap()};
  ui->show("partner_menu", true);
  const auto settle = [&] {
    for (int tick = 0; tick < 60; ++tick) {
      frame(*ui, input);
    }
  };
  settle();

  // The status page names SPEED and BRAINS with the small-caps pictures beside their icons.
  CHECK(
      ui->styleProperty("partner_menu", "stat-2-label", "decorator").contains("status-speed-name"));
  CHECK(ui->styleProperty("partner_menu", "stat-3-label", "decorator")
            .contains("status-brains-name"));

  press(*ui, input, input::keys::Right);
  settle();
  // The Techset row names its range; the Final row never does.
  CHECK_FALSE(ui->elementText("partner_menu", "tech-range-0").empty());
  CHECK(ui->elementText("partner_menu", "tech-range-3").empty());

  press(*ui, input, input::keys::Return);
  settle();
  REQUIRE(ui->elementHasClass("partner_menu", "scrim", "tech-editing"));
  static_cast<void>(ui->takeCues());

  // The game counts the extra action's presses in a setting; one opens the help page, with a sound.
  ui->setSetting("partner_menu.extra_presses", 1.0);
  ui->update();
  CHECK(ui->elementHasClass("partner_menu", "scrim", "tech-help"));
  const std::vector<UiCue> cues = ui->takeCues();
  CHECK(std::ranges::find(cues, UiCue::Confirm) != cues.end());
  CHECK_FALSE(ui->elementText("partner_menu", "tech-help-cancel-key").empty());

  // On the help page only cancel works, back to the change view.
  press(*ui, input, input::keys::Down);
  CHECK(ui->elementHasClass("partner_menu", "scrim", "tech-help"));
  press(*ui, input, input::keys::Escape);
  CHECK_FALSE(ui->elementHasClass("partner_menu", "scrim", "tech-help"));
  CHECK(ui->elementHasClass("partner_menu", "scrim", "tech-editing"));

  // Out of the change view the extra action does nothing.
  press(*ui, input, input::keys::Escape);
  settle();
  REQUIRE_FALSE(ui->elementHasClass("partner_menu", "scrim", "tech-editing"));
  ui->setSetting("partner_menu.extra_presses", 2.0);
  ui->update();
  CHECK_FALSE(ui->elementHasClass("partner_menu", "scrim", "tech-help"));
}

// The portraits of the player and the partner reach past their clips over the tabs; a click on a
// tab must land on it.
TEST_CASE("the original skin's player and partner pages switch tabs on a click") {
  Content content;
  addOriginalArt(content);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "es");
  auto ui = createUi(files, texts);
  REQUIRE(ui->skin() == "original");
  loadScreens(*ui);
  ui->setSetting("ui_skin", std::string{"original"});
  input::InputState input{input::defaultActionMap()};
  const auto settle = [&] {
    for (int tick = 0; tick < 60; ++tick) {
      frame(*ui, input);
    }
  };

  // The pause menu's PLAYER page: each of its five tabs, clicked.
  ui->show("pause_menu", true);
  settle();
  CHECK(clickElement(*ui, "pause_menu", "open-player"));
  settle();
  REQUIRE(ui->elementHasClass("pause_menu", "player-tab-player", "selected"));
  for (const std::string_view tab : {"chart", "medals", "card", "city", "player"}) {
    CAPTURE(tab);
    const std::string id = std::format("player-tab-{}", tab);
    static_cast<void>(clickElement(*ui, "pause_menu", id));
    settle();
    CHECK(ui->elementHasClass("pause_menu", id, "selected"));
  }
  ui->hide("pause_menu");

  // The partner's page: Status and Tech.
  ui->show("partner_menu", true);
  settle();
  REQUIRE(ui->elementHasClass("partner_menu", "tab-status", "selected"));
  static_cast<void>(clickElement(*ui, "partner_menu", "tab-tech"));
  settle();
  CHECK(ui->elementHasClass("partner_menu", "tab-tech", "selected"));
  static_cast<void>(clickElement(*ui, "partner_menu", "tab-status"));
  settle();
  CHECK(ui->elementHasClass("partner_menu", "tab-status", "selected"));
}

// Publishes a small chart as the game does (apps/game/src/player_chart.cpp), its first count
// cells: two Fresh cells, two Ultimate cells and the two the canonical stages call Mega (Phoenixmon
// and HerculesKabuterimon), then four more Fresh cells, two of them past the Fresh box's grid of
// four (one below it, one on a taken place) as a ruleset's could be.
void publishSmallChart(Ui& ui, std::size_t count) {
  struct Cell {
    std::string_view id;
    double x;
    double y;
  };
  constexpr std::array<Cell, 10> Cells{
      Cell{.id = "fr1", .x = 30, .y = 43},   Cell{.id = "fr2", .x = 30, .y = 81},
      Cell{.id = "ul1", .x = 250, .y = 43},  Cell{.id = "houo", .x = 250, .y = 62},
      Cell{.id = "hkab", .x = 274, .y = 43}, Cell{.id = "ul2", .x = 274, .y = 62},
      Cell{.id = "fr3", .x = 30, .y = 119},  Cell{.id = "fr4", .x = 30, .y = 157},
      Cell{.id = "fr5", .x = 30, .y = 195},  Cell{.id = "fr6", .x = 30, .y = 43}};
  for (std::size_t i = 0; i < Cells.size(); ++i) {
    const std::string base = std::format("player.chart.{}.", i);
    ui.setSetting(base + "x", Cells.at(i).x);
    ui.setSetting(base + "y", Cells.at(i).y);
    ui.setSetting(base + "u", 0.0);
    ui.setSetting(base + "v", 0.0);
    ui.setSetting(base + "index", static_cast<double>(i));
    ui.setSetting(base + "id", std::string{Cells.at(i).id});
    ui.setSetting(base + "level", 5.0);
  }
  ui.setSetting("player.chart.count", static_cast<double>(count));
}

// The chart's pages and Mega box (docs/modulos/apps/game.md, «Etapas canónicas»): the original's
// one page while every box's cells fit its grid; the Mega box (the canonical stages or the Mega
// step's Megas) as a sixth box on the same page; and the cells past a box's grid on a next page
// with the same boxes, reached with Down past a column's last cell, Down on the tab, the wheel or
// the arrow, and left with Up, the cursor in its column.
TEST_CASE("the original skin's chart draws the Mega box and pages past a full box") {
  Content content;
  addOriginalArt(content);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  REQUIRE(ui->skin() == "original");
  loadScreens(*ui);
  ui->setSetting("ui_skin", std::string{"original"});
  ui->setSetting("canonical_stages", false);
  ui->setSetting("player.chart.mega", std::string{});
  publishSmallChart(*ui, 6);
  input::InputState input{input::defaultActionMap()};
  const auto settle = [&] {
    for (int tick = 0; tick < 30; ++tick) {
      frame(*ui, input);
    }
  };
  const auto focused = [&] { return ui->focusedElementId("pause_menu"); };
  const auto pagesTop = [&] { return ui->styleProperty("pause_menu", "chart-pages", "top"); };
  const auto left = [&](int cell) {
    return ui->styleProperty("pause_menu", std::format("chart-cell-{}", cell), "left");
  };
  const auto top = [&](int cell) {
    return ui->styleProperty("pause_menu", std::format("chart-cell-{}", cell), "top");
  };
  const auto chartHas = [&](std::string_view name) {
    return ui->elementHasClass("pause_menu", "player-chart", name);
  };
  const auto offPage = [&](std::string_view element) {
    return ui->elementHasClass("pause_menu", element, "off-page");
  };

  ui->show("pause_menu", true);
  settle();
  REQUIRE(clickElement(*ui, "pause_menu", "open-player"));
  settle();
  press(*ui, input, input::keys::Right);
  settle();
  REQUIRE(ui->elementHasClass("pause_menu", "player-tab-chart", "selected"));

  // Every cell fits its box: the original's one page, five boxes, cells where the game puts them.
  CHECK_FALSE(chartHas("chart-paged"));
  CHECK_FALSE(chartHas("chart-mega"));
  CHECK_FALSE(ui->elementHasClass("pause_menu", "panel", "chart-wide"));
  CHECK(left(0) == "54dp");
  CHECK(top(0) == "30dp");
  CHECK(left(3) == "714dp");
  CHECK(left(5) == "786dp");
  press(*ui, input, input::keys::Return);
  CHECK(focused() == "chart-cell-0");
  press(*ui, input, input::keys::Down);
  CHECK(focused() == "chart-cell-1");
  press(*ui, input, input::keys::Down);
  settle();
  CHECK(focused() == "chart-cell-1");
  CHECK(pagesTop() == "0dp");

  // The canonical stages: Phoenixmon and HerculesKabuterimon in the Mega box, a sixth box on the
  // same page; the Champion and Ultimate columns closer so the six fit.
  ui->setSetting("canonical_stages", true);
  settle();
  CHECK(chartHas("chart-mega"));
  CHECK_FALSE(chartHas("chart-paged"));
  CHECK(ui->elementHasClass("pause_menu", "panel", "chart-wide"));
  CHECK(left(3) == "855dp");
  CHECK(top(3) == "30dp");
  CHECK(left(4) == "855dp");
  CHECK(top(4) == "87dp");
  CHECK(left(2) == "687dp");
  CHECK(left(5) == "753dp");
  CHECK(top(5) == "87dp");
  CHECK(left(0) == "54dp");
  CHECK_FALSE(offPage("chart-cell-3"));
  // Right from the Fresh column goes through the Ultimate box to the Mega box.
  press(*ui, input, input::keys::Right);
  CHECK(focused() == "chart-cell-2");
  press(*ui, input, input::keys::Right);
  CHECK(focused() == "chart-cell-5");
  press(*ui, input, input::keys::Right);
  CHECK(focused() == "chart-cell-4");
  press(*ui, input, input::keys::Up);
  CHECK(focused() == "chart-cell-3");
  press(*ui, input, input::keys::Escape);

  // The Mega step's Megas (player.chart.mega) draw the Mega box without the canonical stages.
  ui->setSetting("canonical_stages", false);
  ui->setSetting("player.chart.mega", std::string{"ul2"});
  settle();
  CHECK(chartHas("chart-mega"));
  CHECK(left(5) == "855dp");
  CHECK(top(5) == "30dp");
  CHECK(left(3) == "687dp");
  CHECK(top(3) == "87dp");
  ui->setSetting("player.chart.mega", std::string{});

  // Six Fresh cells: four on page 1 where the game puts them, the two past the grid on page 2, in
  // the Fresh box's first places; the arrows and the page number under the chart.
  publishSmallChart(*ui, 10);
  settle();
  CHECK(chartHas("chart-paged"));
  CHECK_FALSE(chartHas("chart-mega"));
  CHECK(top(7) == "372dp");
  CHECK(left(8) == "54dp");
  CHECK(top(8) == "594dp");
  CHECK(top(9) == "708dp");
  CHECK(offPage("chart-cell-8"));
  CHECK(offPage("chart-less"));
  CHECK_FALSE(offPage("chart-more"));
  CHECK(ui->elementText("pause_menu", "chart-page") == "1/2");
  press(*ui, input, input::keys::Return);
  CHECK(focused() == "chart-cell-3");
  press(*ui, input, input::keys::Left);
  press(*ui, input, input::keys::Left);
  CHECK(focused() == "chart-cell-0");
  press(*ui, input, input::keys::Down);
  press(*ui, input, input::keys::Down);
  press(*ui, input, input::keys::Down);
  CHECK(focused() == "chart-cell-7");
  // Down past the column's last cell slides to page 2, onto the column's top cell there; the keys
  // wait for the slide, and page 1's cells are hidden once it ends.
  press(*ui, input, input::keys::Down);
  CHECK(focused() == "chart-cell-8");
  CHECK(pagesTop() != "-564dp");
  CHECK_FALSE(offPage("chart-cell-0"));
  CHECK_FALSE(offPage("chart-stage-other-0"));
  settle();
  CHECK(pagesTop() == "-564dp");
  CHECK(offPage("chart-cell-0"));
  CHECK_FALSE(offPage("chart-cell-8"));
  CHECK(offPage("chart-stage-other-0"));
  CHECK(ui->styleProperty("pause_menu", "chart-stage-0", "top") == "585dp");
  CHECK_FALSE(offPage("chart-less"));
  CHECK(offPage("chart-more"));
  CHECK(ui->elementText("pause_menu", "chart-page") == "2/2");
  press(*ui, input, input::keys::Down);
  CHECK(focused() == "chart-cell-9");
  press(*ui, input, input::keys::Down);
  settle();
  CHECK(focused() == "chart-cell-9");
  CHECK(pagesTop() == "-564dp");
  // Up past the column's first cell goes back to page 1, on the cell it left.
  press(*ui, input, input::keys::Up);
  press(*ui, input, input::keys::Up);
  settle();
  CHECK(pagesTop() == "0dp");
  CHECK(focused() == "chart-cell-7");
  // From a column with nothing on page 2 the cursor goes to the nearest column there, and back.
  press(*ui, input, input::keys::Right);
  CHECK(focused() == "chart-cell-3");
  press(*ui, input, input::keys::Down);
  settle();
  CHECK(pagesTop() == "-564dp");
  CHECK(focused() == "chart-cell-8");
  press(*ui, input, input::keys::Up);
  settle();
  CHECK(focused() == "chart-cell-3");

  // Out of the cells, Down and Up on the tab turn the page too.
  press(*ui, input, input::keys::Escape);
  CHECK(focused() == "player-tab-chart");
  press(*ui, input, input::keys::Down);
  settle();
  CHECK(pagesTop() == "-564dp");
  CHECK(focused() == "player-tab-chart");
  press(*ui, input, input::keys::Up);
  settle();
  CHECK(pagesTop() == "0dp");

  // The wheel over the chart turns it down and back up, and so do the arrows.
  const auto center = ui->elementCenter("pause_menu", "chart-stage-3");
  REQUIRE(center.has_value());
  const Ui::Point point = center.value_or(Ui::Point{});
  static_cast<void>(ui->handleEvent(platform::MouseMoved{.x = point.x, .y = point.y}));
  ui->update();
  static_cast<void>(ui->handleEvent(platform::MouseWheelMoved{.x = 0, .y = -1}));
  settle();
  CHECK(pagesTop() == "-564dp");
  static_cast<void>(ui->handleEvent(platform::MouseWheelMoved{.x = 0, .y = 1}));
  settle();
  CHECK(pagesTop() == "0dp");
  REQUIRE(clickElement(*ui, "pause_menu", "chart-more"));
  settle();
  CHECK(pagesTop() == "-564dp");
  REQUIRE(clickElement(*ui, "pause_menu", "chart-less"));
  settle();
  CHECK(pagesTop() == "0dp");

  // The Mega box and a page 2 at once: six boxes on both pages.
  ui->setSetting("canonical_stages", true);
  settle();
  CHECK(chartHas("chart-mega"));
  CHECK(chartHas("chart-paged"));
  CHECK(left(3) == "855dp");
  CHECK(top(8) == "594dp");

  // Back to the original's cells: one page, five boxes.
  ui->setSetting("canonical_stages", false);
  publishSmallChart(*ui, 6);
  settle();
  CHECK_FALSE(chartHas("chart-paged"));
  CHECK_FALSE(chartHas("chart-mega"));
  CHECK_FALSE(offPage("chart-cell-3"));
  CHECK(left(3) == "714dp");
  CHECK(pagesTop() == "0dp");
}

TEST_CASE("the modern skin's partner tabs switch the page on left and right, as the original's") {
  Content content;
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  REQUIRE(ui->skin() == "modern");
  REQUIRE(ui->loadDocument("partner_menu", idOf("ui/partner_menu/document")));
  ui->setSetting("ui_skin", std::string{"modern"});
  input::InputState input{input::defaultActionMap()};
  ui->show("partner_menu", true);
  frame(*ui, input);
  REQUIRE(ui->focusedElementId("partner_menu") == "tab-status");

  // Right through every page, the focus staying on the tabs, then on to Back.
  for (const std::string_view page : {"tech", "items", "care"}) {
    CAPTURE(page);
    press(*ui, input, input::keys::Right);
    const std::string tab = std::format("tab-{}", page);
    CHECK(ui->focusedElementId("partner_menu") == tab);
    CHECK(ui->elementHasClass("partner_menu", tab, "selected"));
    CHECK_FALSE(ui->elementHasClass("partner_menu", std::format("page-{}", page), "hidden"));
  }
  press(*ui, input, input::keys::Right);
  CHECK(ui->focusedElementId("partner_menu") == "close");
  CHECK(ui->elementHasClass("partner_menu", "tab-care", "selected"));
  // Left from Back opens the last page again, and on back to the first.
  press(*ui, input, input::keys::Left);
  CHECK(ui->elementHasClass("partner_menu", "tab-care", "selected"));
  for (int step = 0; step < 3; ++step) {
    press(*ui, input, input::keys::Left);
  }
  CHECK(ui->focusedElementId("partner_menu") == "tab-status");
  CHECK(ui->elementHasClass("partner_menu", "tab-status", "selected"));
  CHECK(ui->elementHasClass("partner_menu", "page-tech", "hidden"));
}

TEST_CASE("the modern skin's partner age says day for one and days for any other count") {
  Content content;
  const assets::VirtualFileSystem& files = content.mount();
  for (const auto& [language, one, two] :
       {std::tuple{"en", "1 day", "2 days"}, std::tuple{"es", "1 día", "2 días"}}) {
    CAPTURE(language);
    const localization::Catalog texts = catalogIn(files, language);
    auto ui = createUi(files, texts);
    REQUIRE(ui->loadDocument("partner_menu", idOf("ui/partner_menu/document")));
    ui->setSetting("ui_skin", std::string{"modern"});
    ui->show("partner_menu", true);
    double revision = 0.0;
    const auto age = [&](double days) {
      ui->setSetting("partner.age", days);
      revision += 1.0;
      ui->setSetting("partner.revision", revision);
      ui->update();
      return ui->elementText("partner_menu", "status-age");
    };
    CHECK(age(1.0) == one);
    CHECK(age(2.0) == two);
  }
}

TEST_CASE("the original skin's enemy HP bar fill is as wide as the HP left") {
  Content content;
  addOriginalArt(content);
  const assets::VirtualFileSystem& files = content.mount();
  const localization::Catalog texts = catalogIn(files, "en");
  auto ui = createUi(files, texts);
  REQUIRE(ui->loadDocument("battle", idOf("ui/battle/document")));
  ui->setSetting("ui_skin", std::string{"original"});
  ui->show("battle", true);
  double revision = 0.0;
  // One bar as apps/screens publishes it: enemy 1, its anchor, the corner, the fill's width in the
  // original's pixels, its red and green tint (BTL_renderEnemyHPBars) and the HP.
  const auto publish = [&](int fill, int red, int green, int hp) {
    ui->setSetting("battle.hp_bars",
                   std::format("1 50.0 40.0 20 -26 {} {} {} {} ", fill, red, green, hp));
    revision += 1.0;
    ui->setSetting("battle.hp_bars.revision", revision);
    ui->update();
  };
  const auto dp = [&](std::string_view id, std::string_view property) {
    return std::stod(ui->styleProperty("battle", id, property));
  };
  // Where the gradients the fill is drawn with end, in dp (3 a pixel): the fill's box does not
  // clip them, so they must stop at the HP left themselves.
  const auto drawnEnd = [&] {
    double end = 0.0;
    for (int segment = 0; segment < 3; ++segment) {
      const std::string id = std::format("hb-1-s{}", segment);
      if (!ui->elementHasClass("battle", id, "hidden")) {
        end = std::max(end, dp(id, "left") + dp(id, "width"));
      }
    }
    return end;
  };

  publish(50, 128, 255, 400);
  CHECK(drawnEnd() == doctest::Approx(150.0));
  // 250 HP of 400: 31 pixels, yellower.
  publish(31, 196, 255, 250);
  CHECK(drawnEnd() == doctest::Approx(93.0));
  CHECK(dp("hb-1-fill", "width") == doctest::Approx(93.0));
  // 87 of 400: 10 pixels, orange; the green channel's saturation lies past them.
  publish(10, 255, 192, 87);
  CHECK(drawnEnd() == doctest::Approx(30.0));
  // Nothing left: no gradient shows.
  publish(0, 255, 128, 0);
  for (int segment = 0; segment < 3; ++segment) {
    CHECK(ui->elementHasClass("battle", std::format("hb-1-s{}", segment), "hidden"));
  }
}

} // namespace dwe::ui
