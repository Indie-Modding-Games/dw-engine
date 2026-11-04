#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/damage_numbers.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "common.hpp"
#include "field_numbers.hpp"

namespace dwe::app {
namespace {

// The field rules' frame (transition.frame_seconds): the original's field runs at 20 frames a
// second.
constexpr float FieldFrame = 0.05F;

// The game pack from the source tree with its field numbers document, and its UI at 1280x720.
struct NumbersUi {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;
  std::unique_ptr<localization::Catalog> texts;
  std::unique_ptr<ui::Ui> ui;

  NumbersUi() {
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
                                             .scripts = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    ui = std::move(*created);
    auto document = assets::AssetId::parse("ui/field_numbers/document");
    REQUIRE_MESSAGE(document.has_value(), document.error().message);
    auto loaded = ui->loadDocument(FieldNumbersDocument, *document);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
  }

  [[nodiscard]] std::string numbers() const {
    const ui::UiValue value = ui->setting(screens::FieldNumbersSetting);
    const std::string* text = std::get_if<std::string>(&value);
    return text != nullptr ? *text : std::string{};
  }
};

// One field frame as the game runs it: three ticks of 1/60 s.
void frame(FieldNumbers& numbers) {
  for (int tick = 0; tick < 3; ++tick) {
    numbers.tick(TickSeconds);
  }
}

} // namespace

TEST_CASE("deal_damage's number shows over the partner's feet at once, 8 pixels up, then bounces") {
  NumbersUi game;
  FieldNumbers numbers{*game.ui, FieldFrame};
  CHECK_FALSE(game.ui->isVisible(FieldNumbersDocument));
  // Elecmon's shock: 20% of 800 (script_interp.c: deal_damage, addEntityText(partner, 0, 0, 160,
  // 0)).
  const std::array damage{std::int32_t{160}};
  numbers.add(damage, screens::WindowFraction{.x = 0.4F, .y = 0.5F});
  CHECK(game.ui->isVisible(FieldNumbersDocument));
  CHECK(game.numbers() == "40.0 50.0 0 -8 160 0 0 ");
  // entity_text.c: ENTITY_TEXT_Y_OFFSETS: 7 pixels up on its second frame, not before.
  numbers.tick(TickSeconds);
  numbers.tick(TickSeconds);
  CHECK(game.numbers() == "40.0 50.0 0 -8 160 0 0 ");
  numbers.tick(TickSeconds);
  CHECK(game.numbers() == "40.0 50.0 0 -15 160 0 0 ");
  // The document draws the number's digits.
  CHECK(game.ui->elementRect(FieldNumbersDocument, "dn-0").has_value());
  CHECK(game.ui->elementRect(FieldNumbersDocument, "dd-0-2").has_value());
}

TEST_CASE(
    "a field number stays 31 field frames, the original's 93 vblanks, and the document goes") {
  NumbersUi game;
  FieldNumbers numbers{*game.ui, FieldFrame};
  const std::array damage{std::int32_t{160}};
  numbers.add(damage, screens::WindowFraction{.x = 0.4F, .y = 0.5F});
  // The frame it was added drew its first; 30 more draw the rest.
  for (int drawn = 1; drawn < 31; ++drawn) {
    frame(numbers);
    CHECK(game.ui->isVisible(FieldNumbersDocument));
  }
  CHECK_FALSE(game.numbers().empty());
  frame(numbers);
  CHECK(game.numbers().empty());
  CHECK_FALSE(game.ui->isVisible(FieldNumbersDocument));
  CHECK_FALSE(numbers.active());
}

TEST_CASE("three shocks in a row queue their numbers 4 pixels apart; clear takes them away") {
  NumbersUi game;
  FieldNumbers numbers{*game.ui, FieldFrame};
  const std::array damage{std::int32_t{160}, std::int32_t{160}};
  numbers.add(damage, screens::WindowFraction{.x = 0.4F, .y = 0.5F});
  // The second waits until the first has shown 11 frames (renderEntityText).
  CHECK(game.numbers() == "40.0 50.0 0 -8 160 0 0 ");
  for (int drawn = 1; drawn < 11; ++drawn) {
    frame(numbers);
  }
  CHECK(game.numbers().ends_with("40.0 50.0 4 -4 160 0 0 "));
  numbers.clear();
  CHECK(game.numbers().empty());
  CHECK_FALSE(game.ui->isVisible(FieldNumbersDocument));
}

TEST_CASE("a number off the camera is kept in its queue but not drawn") {
  NumbersUi game;
  FieldNumbers numbers{*game.ui, FieldFrame};
  const std::array damage{std::int32_t{160}};
  numbers.add(damage, std::nullopt);
  CHECK(numbers.active());
  CHECK(game.numbers().empty());
  CHECK_FALSE(game.ui->isVisible(FieldNumbersDocument));
}

} // namespace dwe::app
