#pragma once

// The team's game pack from the source tree, with white stand-ins for the drawings dwx extracts,
// and one of its documents shown at 1280x720: what the tests of the game's menus share.

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/platform/events.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace dwe::ui::fixture {

inline std::vector<std::byte> whitePng(unsigned width, unsigned height) {
  const std::vector<unsigned char> pixels(std::size_t{width} * height * 4, 255);
  std::vector<unsigned char> png;
  REQUIRE(lodepng::encode(png, pixels, width, height) == 0);
  const auto* begin = reinterpret_cast<const std::byte*>(png.data());
  return {begin, begin + png.size()};
}

// An indexed stand-in with four palettes of 16 colours and its metadata (an image dwx extracts
// with its palettes, docs/architecture/ui.md), which a skin's sprite sheet with a palette needs.
inline std::vector<std::byte> indexedPng(unsigned width, unsigned height) {
  lodepng::State state;
  state.info_raw.colortype = LCT_PALETTE;
  state.info_raw.bitdepth = 8;
  state.info_png.color.colortype = LCT_PALETTE;
  state.info_png.color.bitdepth = 8;
  state.encoder.auto_convert = 0;
  for (unsigned colour = 0; colour < 16; ++colour) {
    const auto grey = static_cast<unsigned char>(colour * 16);
    lodepng_palette_add(&state.info_png.color, grey, grey, grey, 255);
    lodepng_palette_add(&state.info_raw, grey, grey, grey, 255);
  }
  const std::vector<unsigned char> pixels(std::size_t{width} * height, 1);
  std::vector<unsigned char> png;
  REQUIRE(lodepng::encode(png, pixels, width, height, state) == 0);
  const auto* begin = reinterpret_cast<const std::byte*>(png.data());
  return {begin, begin + png.size()};
}

inline std::string indexedMeta(unsigned width, unsigned height) {
  std::string meta =
      std::format("mode: indexed4\nwidth: {}\nheight: {}\npng: indexed\npng_palette: 0\n"
                  "palettes:\n",
                  width, height);
  for (int palette = 0; palette < 4; ++palette) {
    meta +=
        "- \"0000 ffff ffff ffff ffff ffff ffff ffff ffff ffff ffff ffff ffff ffff ffff ffff\"\n";
  }
  return meta;
}

// The team pack from the source tree and a stand-in base pack, in memory.
struct Content {
  assets::MemoryFileSystem memory;
  std::unique_ptr<assets::VirtualFileSystem> files;

  // `edit` may change the pack's files before it is mounted.
  explicit Content(const std::function<void(assets::MemoryFileSystem&)>& edit = {}) {
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
          std::tuple{"texture/stddat/stdtim/002.png", 256U, 120U},
          std::tuple{"texture/stddat/stdtim/003.png", 256U, 256U},
          std::tuple{"texture/stddat/taisen1.png", 256U, 256U},
          std::tuple{"texture/stddat/taisen2.png", 128U, 144U},
          std::tuple{"texture/stddat/16taisen.png", 256U, 256U}}) {
      memory.addFile(std::filesystem::path{"packs/base"} / path, whitePng(width, height));
    }
    memory.addFile("packs/base/map/trai00/texture4/001.png", indexedPng(256, 256));
    memory.addFile("packs/base/map/trai00/texture4/001_meta.yaml", indexedMeta(256, 256));
    const auto location = [this](std::string_view root) {
      return assets::PackLocation{
          .fileSystem = &memory, .root = std::string{root}, .origin = assets::PackOrigin::Engine};
    };
    if (edit) {
      edit(memory);
    }
    const std::array locations{location("packs/base"), location("packs/game")};
    auto mounted = assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
  }
};

struct Screen {
  Content content;
  localization::Catalog texts;
  std::unique_ptr<Ui> ui;
  input::InputState input{input::defaultActionMap()};

  explicit Screen(std::string_view document, std::string_view language = "en",
                  const std::function<void(assets::MemoryFileSystem&)>& edit = {})
      : content(edit), texts(catalog(*content.files, language)) {
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

  static localization::Catalog catalog(const assets::VirtualFileSystem& files,
                                       std::string_view language) {
    auto loaded = localization::Catalog::load(files, language);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
    return std::move(*loaded);
  }

  // Switches the UI to a skin, as the game does, and tells the documents' logic.
  void skin(std::string_view id) {
    REQUIRE_MESSAGE(ui->setSkin(id), id);
    ui->setSetting("ui_skin", std::string{id});
  }

  // A key pressed for a frame and let go for another.
  void press(platform::KeyCode key) {
    input.handle(platform::KeyChanged{.key = key, .down = true, .repeat = false});
    frames(1);
    input.handle(platform::KeyChanged{.key = key, .down = false, .repeat = false});
    frames(1);
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

} // namespace dwe::ui::fixture
