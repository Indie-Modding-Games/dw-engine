// The engine's long names (docs/modulos/apps/game.md, «Nombres largos»): the documents that show a
// name of up to twelve letters keep it inside their boxes, and leave the original's texts as they
// are.

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace dwe::ui {
namespace {

std::vector<std::byte> whitePng(unsigned width, unsigned height) {
  const std::vector<unsigned char> pixels(std::size_t{width} * height * 4, 255);
  std::vector<unsigned char> png;
  REQUIRE(lodepng::encode(png, pixels, width, height) == 0);
  const auto* begin = reinterpret_cast<const std::byte*>(png.data());
  return {begin, begin + png.size()};
}

// The team pack from the source tree and a stand-in base pack, in memory.
struct Content {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;

  Content() {
    const std::filesystem::path game = DWE_GAME_PACK;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{game}) {
      if (!entry.is_regular_file()) {
        continue;
      }
      std::ifstream stream{entry.path(), std::ios::binary};
      const std::string bytes{std::istreambuf_iterator<char>{stream},
                              std::istreambuf_iterator<char>{}};
      memory.addFile(std::filesystem::path{"packs/game"} / entry.path().lexically_relative(game),
                     bytes);
    }
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    // Stand-ins for the drawings dwx extracts, which the original skin needs to be offered.
    for (const auto& [path, width, height] :
         {std::tuple{"texture/etcdat/system_w.png", 256U, 128U},
          std::tuple{"texture/etcdat/fi_info.png", 256U, 64U},
          std::tuple{"texture/etcdat/sboy.png", 256U, 256U},
          std::tuple{"texture/etcdat/bi_in.png", 256U, 64U},
          std::tuple{"texture/etcdat/feel_ef.png", 256U, 64U},
          std::tuple{"texture/etcdat/btlcomnd.png", 256U, 64U},
          std::tuple{"texture/etcna/title2.png", 320U, 240U},
          std::tuple{"texture/etcna/title256.png", 320U, 256U},
          std::tuple{"texture/stddat/time.png", 66U, 29U},
          std::tuple{"texture/stddat/stdtim/002.png", 256U, 120U}}) {
      memory.addFile(std::filesystem::path{"packs/base"} / path, whitePng(width, height));
    }
    const auto location = [this](std::string_view root) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = assets::PackOrigin::Engine};
    };
    auto mounted =
        assets::VirtualFileSystem::mount({location("packs/base"), location("packs/game")});
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
  }
};

struct Screen {
  Content content;
  localization::Catalog texts;
  std::unique_ptr<Ui> ui;
  input::InputState input{input::defaultActionMap()};

  explicit Screen(std::string_view document) : texts(catalog(*content.files)) {
    auto made = Ui::create({.files = content.files.get(),
                            .texts = &texts,
                            .width = 1280,
                            .height = 720,
                            .clock = {},
                            .scripts = {}});
    REQUIRE_MESSAGE(made.has_value(), made.error().message);
    ui = std::move(*made);
    auto id = assets::AssetId::parse(std::format("ui/{}/document", document));
    REQUIRE(id.has_value());
    REQUIRE(ui->loadDocument(document, *id));
  }

  static localization::Catalog catalog(const assets::VirtualFileSystem& files) {
    auto loaded = localization::Catalog::load(files, "en");
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
    return std::move(*loaded);
  }

  // A few frames: the documents lay out and fit_text measures what they wrote.
  void frames(int count = 5) {
    for (int frame = 0; frame < count; ++frame) {
      ui->tick(input, 1.0 / 60.0);
      input.endTick();
      ui->update();
    }
  }
};

constexpr std::string_view LongName = "WWWWWWWWWWWW";

// A speech box with the speaker's row and one row of text, as the game publishes it.
void publishSpeech(Ui& ui, std::string_view text, double revision) {
  ui.setSetting("story_textbox.open", true);
  ui.setSetting("story_textbox.kind", std::string{"speech"});
  ui.setSetting("story_textbox.opening", 1.0);
  ui.setSetting("story_textbox.row_count", 2.0);
  ui.setSetting("story_textbox.row.0.runs", 1.0);
  ui.setSetting("story_textbox.row.0.run.0.text", std::string{"Jijimon"});
  ui.setSetting("story_textbox.row.0.run.0.color", 7.0);
  ui.setSetting("story_textbox.row.1.runs", 1.0);
  ui.setSetting("story_textbox.row.1.run.0.text", std::string{text});
  ui.setSetting("story_textbox.row.1.run.0.color", 1.0);
  ui.setSetting("story_textbox.revision", revision);
}

} // namespace

TEST_CASE(
    "a speech row a long name makes too wide is squeezed into the box, a row that fits is not") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"story_textbox"};
    screen.ui->setSetting("ui_skin", std::string{skin});
    screen.ui->show("story_textbox");
    // Two long names on one row: wider than the box in either font.
    publishSpeech(*screen.ui, std::format("{} and {}, look!", LongName, LongName), 1.0);
    screen.frames();
    const std::string squeezed = screen.ui->styleProperty("story_textbox", "row-1", "transform");
    MESSAGE(squeezed);
    CHECK(squeezed.find("scale") != std::string::npos);
    CHECK(screen.ui->styleProperty("story_textbox", "row-0", "transform").find("scale") ==
          std::string::npos);

    // The same row with the original's six letters fits: nothing is squeezed.
    publishSpeech(*screen.ui, "WWWWWW, this isn't", 2.0);
    screen.frames();
    CHECK(screen.ui->styleProperty("story_textbox", "row-1", "transform").find("scale") ==
          std::string::npos);
  }
}

TEST_CASE("the naming box shows twelve letters inside the name's box in both skins") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"naming_box"};
    screen.ui->setSetting("ui_skin", std::string{skin});
    screen.ui->setSetting("naming_box.length", 12.0);
    for (int letter = 0; letter < 12; ++letter) {
      screen.ui->setSetting(std::format("naming_box.name.{}", letter), std::string{"W"});
    }
    screen.ui->setSetting("naming_box.revision", 1.0);
    screen.ui->show("naming_box");
    screen.frames();
    CHECK(screen.ui->elementText("naming_box", "letter-11") == "W");
    CHECK_FALSE(screen.ui->elementHasClass("naming_box", "letter-11", "hidden"));
    const auto box = screen.ui->elementRect("naming_box", "name-box");
    const auto last = screen.ui->elementRect("naming_box", "letter-11");
    REQUIRE(box.has_value());
    REQUIRE(last.has_value());
    CHECK(last->right <= box->right);

    // The original's six letters hide the rest.
    screen.ui->setSetting("naming_box.length", 6.0);
    screen.ui->setSetting("naming_box.revision", 2.0);
    screen.frames();
    CHECK(screen.ui->elementHasClass("naming_box", "letter-6", "hidden"));
  }
}

TEST_CASE("the original look's player page widens the name's box for a name past six letters") {
  Screen screen{"pause_menu"};
  screen.ui->setSetting("ui_skin", std::string{"original"});
  screen.ui->setSetting("player.name", std::string{LongName});
  screen.ui->show("pause_menu", true);
  screen.frames();
  CHECK(screen.ui->elementHasClass("pause_menu", "panel", "long-player-name"));
  screen.ui->setSetting("player.name", std::string{"WWWWWW"});
  screen.frames();
  CHECK_FALSE(screen.ui->elementHasClass("pause_menu", "panel", "long-player-name"));
}

} // namespace dwe::ui
