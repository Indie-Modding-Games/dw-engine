#include <dwe/ui/skin.hpp>
#include <dwe/ui/sprite_sheet_rcss.hpp>

#include <doctest/doctest.h>

#include <format>
#include <string>
#include <string_view>

namespace dwe::ui {
namespace {

constexpr std::string_view FullSkin = R"yaml(
id: framed
name: ui_skin.framed
extends: modern
theme: [ui/skins/framed/theme]
screens:
  main_menu:
    styles: [ui/skins/framed/main_menu/style]
    document: ui/skins/framed/main_menu/document
  hud:
    styles: []
sprite_sheets:
  - name: frames
    image: texture/stddat/time
    palette: 3
    original_size: [66, 29]
    dp_per_pixel: 3
    sprites:
      corner-top: [0, 0, 8, 8]
      edge: [8, 0, 4, 8]
)yaml";

SkinSpriteSheet sheetOf(std::string_view yaml) {
  auto skin = parseSkin(yaml);
  REQUIRE_MESSAGE(skin.has_value(), skin.error().message);
  REQUIRE(skin->spriteSheets.size() == 1);
  return skin->spriteSheets.front();
}

std::string errorOf(std::string_view yaml) {
  auto skin = parseSkin(yaml);
  REQUIRE_FALSE(skin.has_value());
  return skin.error().message;
}

} // namespace

TEST_CASE("a skin.yaml names the look of the screens") {
  auto skin = parseSkin(FullSkin);
  REQUIRE_MESSAGE(skin.has_value(), skin.error().message);
  CHECK(skin->id == "framed");
  CHECK(skin->nameKey == "ui_skin.framed");
  CHECK(skin->extends == std::optional<std::string>{"modern"});
  REQUIRE(skin->theme.size() == 1);
  CHECK(skin->theme[0].str() == "ui/skins/framed/theme");
  REQUIRE(skin->screens.size() == 2);
  const SkinScreen& menu = skin->screens.at("main_menu");
  REQUIRE(menu.styles.size() == 1);
  CHECK(menu.document.has_value());
  CHECK(skin->screens.at("hud").styles.empty());
  const SkinSpriteSheet& sheet = skin->spriteSheets.at(0);
  CHECK(sheet.image.palette == std::optional<std::size_t>{3});
  CHECK(sheet.originalWidth == std::optional<std::uint32_t>{66});
  CHECK(sheet.dpPerPixel == doctest::Approx(3.0F));
  REQUIRE(sheet.sprites.size() == 2);
  CHECK(sheet.sprites[0].name == "corner-top");
  CHECK(sheet.sprites[1].width == 4);

  const auto bare = parseSkin("id: plain\nname: ui_skin.plain\n");
  REQUIRE(bare.has_value());
  CHECK(bare->screens.empty());
  CHECK_FALSE(bare->extends.has_value());
}

TEST_CASE("a skin.yaml with a misspelt key, a bad name or a bad sprite is refused") {
  CHECK(errorOf("id: plain\nname: x\nthemes: []\n").contains("themes"));
  CHECK(errorOf("id: Plain\nname: x\n").contains("lowercase"));
  CHECK(errorOf("id: plain\nname: x\nscreens:\n  Main: {}\n").contains("lowercase"));
  CHECK(errorOf("id: plain\nname: x\nscreens:\n  hud: {style: []}\n").contains("style"));
  CHECK(errorOf("id: plain\nname: x\ntheme: [UI/Theme]\n").length() > 0);
  CHECK(
      errorOf("id: p\nname: x\nsprite_sheets:\n- {name: s, image: a, sprites: {b: [0, 0, 0, 1]}}\n")
          .contains("at least one pixel"));
  CHECK(errorOf("id: p\nname: x\nsprite_sheets:\n- {name: s, image: a, sprites: {b: [0, 0, 1]}}\n")
            .length() > 0);
  CHECK(errorOf(
            "id: p\nname: x\nsprite_sheets:\n- {name: 1s, image: a, sprites: {b: [0, 0, 1, 1]}}\n")
            .contains("starting with a letter"));
  CHECK(
      errorOf(
          "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a, dp_per_pixel: 0, sprites: {b: [0, 0, 1, 1]}}\n")
          .contains("dp_per_pixel"));
}

TEST_CASE("a skin.yaml marks elements of shared documents with document_classes") {
  const auto skin = parseSkin(
      "id: p\nname: x\ndocument_classes:\n  hud:\n    field-meter-0-bar: [pips, pips, wide]\n");
  REQUIRE_MESSAGE(skin.has_value(), skin.error().message);
  REQUIRE(skin->documentClasses.contains("hud"));
  CHECK(skin->documentClasses.at("hud").at("field-meter-0-bar") ==
        std::vector<std::string>{"pips", "wide"});
  CHECK_FALSE(parseSkin("id: p\nname: x\ndocument_classes:\n  Hud:\n    a: [b]\n").has_value());
  CHECK_FALSE(parseSkin("id: p\nname: x\ndocument_classes:\n  hud:\n    a b: [c]\n").has_value());
  CHECK_FALSE(parseSkin("id: p\nname: x\ndocument_classes:\n  hud:\n    a: []\n").has_value());
  CHECK_FALSE(parseSkin("id: p\nname: x\ndocument_classes:\n  hud:\n    a: [1b]\n").has_value());
  CHECK_FALSE(parseSkin("id: p\nname: x\ndocument_classes: [hud]\n").has_value());
}

TEST_CASE(
    "a sprite sheet may let its picture be smoothed, which its RCSS asks only when smoothing is on") {
  SkinSpriteSheet sheet = sheetOf(
      "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a/b, palette: 0, upscale: smooth, sprites: {c: [0, 0, 1, 1]}}\n");
  CHECK(sheet.smooth);
  // The skin reads the flag; the image is smoothed only once the styler sets it on the reference.
  CHECK_FALSE(sheet.image.smooth);
  sheet.image.smooth = true;
  const auto rcss = spriteSheetRcss(sheet, 1, 1);
  REQUIRE(rcss.has_value());
  CHECK(rcss->contains("src: /a/b?palette=0&upscale=smooth;"));

  CHECK_FALSE(
      sheetOf(
          "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a/b, sprites: {c: [0, 0, 1, 1]}}\n")
          .smooth);
  CHECK(
      errorOf(
          "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a, upscale: sharp, sprites: {b: [0, 0, 1, 1]}}\n")
          .contains("upscale must be smooth"));
}

TEST_CASE("a sprite sheet may mirror its picture's sides, the original size counting them") {
  const SkinSpriteSheet sheet = sheetOf(
      "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a/b, palette: 0, mirror_sides: [3, 1], original_size: [6, 2], sprites: {c: [0, 0, 6, 2]}}\n");
  CHECK(sheet.image.mirrorLeft == 3);
  CHECK(sheet.image.mirrorRight == 1);
  const auto rcss = spriteSheetRcss(sheet, 6, 2);
  REQUIRE(rcss.has_value());
  CHECK(rcss->contains("src: /a/b?palette=0&mirror_left=3&mirror_right=1;"));
  CHECK(
      errorOf(
          "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a, mirror_sides: [1], sprites: {b: [0, 0, 1, 1]}}\n")
          .contains("mirror_sides: [left, right]"));
  CHECK(
      errorOf(
          "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a, mirror_sides: [2000, 0], sprites: {b: [0, 0, 1, 1]}}\n")
          .contains("at most 1024 columns"));
}

TEST_CASE("ui/ui_skin.yaml names the skin the game starts with") {
  const auto choice = parseSkinChoice("default: modern\n");
  REQUIRE(choice.has_value());
  CHECK(choice->preferred == "modern");
  CHECK_FALSE(choice->fallback.has_value());
  const auto withFallback = parseSkinChoice("default: original\nfallback: modern\n");
  REQUIRE(withFallback.has_value());
  CHECK(withFallback->preferred == "original");
  CHECK(withFallback->fallback == "modern");
  CHECK_FALSE(parseSkinChoice("default: modern\nfallback: Modern\n").has_value());
  CHECK_FALSE(parseSkinChoice("default: modern\nother: 1\n").has_value());
  CHECK_FALSE(parseSkinChoice("default: Modern\n").has_value());
}

TEST_CASE("a sprite sheet keeps its sprites' size on screen over an upscaled image") {
  const SkinSpriteSheet sheet = sheetOf(FullSkin);
  // The original image: one image pixel per original pixel, three dp each.
  auto original = spriteSheetRcss(sheet, 66, 29);
  REQUIRE_MESSAGE(original.has_value(), original.error().message);
  CHECK(original->contains("@spritesheet frames {"));
  CHECK(original->contains("src: /texture/stddat/time?palette=3;"));
  CHECK(original->contains("resolution: 0.33333334x;"));
  CHECK(original->contains("corner-top: 0px 0px 8px 8px;"));
  CHECK(original->contains("edge: 8px 0px 4px 8px;"));

  // A copy four times larger: four times the pixels, and the same size in dp.
  auto upscaled = spriteSheetRcss(sheet, 264, 116);
  REQUIRE(upscaled.has_value());
  CHECK(upscaled->contains("resolution: 1.3333334x;"));
  CHECK(upscaled->contains("edge: 32px 0px 16px 32px;"));

  // Not a whole multiple, or not the same one across and down.
  CHECK_FALSE(spriteSheetRcss(sheet, 100, 29).has_value());
  CHECK_FALSE(spriteSheetRcss(sheet, 132, 87).has_value());
  CHECK_FALSE(spriteSheetRcss(sheet, 33, 29).has_value());
}

TEST_CASE("a sprite outside its image is refused") {
  const SkinSpriteSheet sheet = sheetOf(
      "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a, sprites: {b: [60, 0, 8, 8]}}\n");
  CHECK(spriteSheetRcss(sheet, 68, 8).has_value());
  CHECK_FALSE(spriteSheetRcss(sheet, 66, 8).has_value());
}

TEST_CASE("a sprite whose edge passes 32 bits is outside its image, not wrapped around into it") {
  const SkinSpriteSheet sheet = sheetOf(
      "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a, sprites: {b: [4294967280, 0, 32, 1]}}\n");
  CHECK_FALSE(spriteSheetRcss(sheet, 64, 8).has_value());
  const SkinSpriteSheet tall = sheetOf(
      "id: p\nname: x\nsprite_sheets:\n- {name: s, image: a, sprites: {b: [0, 4294967295, 1, 1]}}\n");
  CHECK_FALSE(spriteSheetRcss(tall, 64, 8).has_value());
}

TEST_CASE("dp_per_pixel must be a finite number from MinDpPerPixel to MaxDpPerPixel") {
  const auto withScale = [](std::string_view scale) {
    return std::format("id: p\nname: x\nsprite_sheets:\n- {{name: s, image: a, dp_per_pixel: {}, "
                       "sprites: {{b: [0, 0, 1, 1]}}}}\n",
                       scale);
  };
  // The YAML reader already refuses what is not a finite number.
  for (const std::string_view bad : {"inf", "nan"}) {
    CAPTURE(bad);
    CHECK_FALSE(parseSkin(withScale(bad)).has_value());
  }
  for (const std::string_view bad : {"0", "-1", "1e-40", "0.0624", "16.5", "1e9"}) {
    CAPTURE(bad);
    const auto skin = parseSkin(withScale(bad));
    REQUIRE_FALSE(skin.has_value());
    CHECK(skin.error().message.contains("dp_per_pixel must be a number from 0.0625 to 16"));
  }
  for (const std::string_view good : {"0.0625", "1", "3", "16"}) {
    CAPTURE(good);
    CHECK(parseSkin(withScale(good)).has_value());
  }
}

} // namespace dwe::ui
