#include <dwe/signs/sign_overlay.hpp>

#include <doctest/doctest.h>

#include <string_view>

using namespace dwe::signs;
using dwe::assets::PaletteColor;

namespace {

SignOverlay parsed(std::string_view yaml) {
  auto overlay = parseSignOverlay(yaml);
  REQUIRE_MESSAGE(overlay.has_value(), overlay.error().message);
  return std::move(*overlay);
}

std::string problem(std::string_view yaml) {
  auto overlay = parseSignOverlay(yaml);
  REQUIRE_FALSE(overlay.has_value());
  return overlay.error().message;
}

} // namespace

TEST_CASE("a sign overlay reads every field of a sign") {
  const SignOverlay overlay = parsed(R"(
signs:
  - id: board
    target: {page: 1, bits: 4, clut: 20}
    region: [10, 20, 30, 12]
    corners: [[10, 20], [40, 21], [39, 32], [11, 31.5]]
    box: [60, 20]
    clear: {mode: fill, rect: [9, 19, 32, 14], color: "#102030"}
    text: signs.board
    font: noto_sans_regular
    size: 8.5
    letter_spacing: 0.5
    line_spacing: 0.9
    align: left
    valign: bottom
    rotation: -90
    skew: 12
    vertical: true
    fit: false
    color: "#ff800080"
    outline: {color: "#000000", width: 1.5}
    glow: {color: "#ff0000", radius: 3, strength: 0.5}
    colors: region
remove: [old]
)");
  REQUIRE(overlay.signs.size() == 1);
  const Sign& sign = overlay.signs.front();
  CHECK(sign.id == "board");
  CHECK(sign.target ==
        SignTarget{.kind = SignTarget::Kind::Page, .bitsPerPixel = 4, .page = 1, .clut = 20});
  CHECK(sign.region == PixelRect{.x = 10, .y = 20, .width = 30, .height = 12});
  REQUIRE(sign.corners.has_value());
  CHECK(sign.corners.value()[3] == PixelPoint{.x = 11.0F, .y = 31.5F});
  CHECK(sign.box == PixelPoint{.x = 60.0F, .y = 20.0F});
  CHECK(sign.clear.mode == SignClear::Mode::Fill);
  CHECK(sign.clear.rect == PixelRect{.x = 9, .y = 19, .width = 32, .height = 14});
  CHECK(sign.clear.color == PaletteColor{.red = 0x10, .green = 0x20, .blue = 0x30, .alpha = 255});
  CHECK(sign.text == "signs.board");
  CHECK(sign.font == "noto_sans_regular");
  CHECK(sign.size == doctest::Approx(8.5));
  CHECK(sign.letterSpacing == doctest::Approx(0.5));
  CHECK(sign.lineSpacing == doctest::Approx(0.9));
  CHECK(sign.align == HorizontalAlign::Left);
  CHECK(sign.valign == VerticalAlign::Bottom);
  CHECK(sign.rotation == doctest::Approx(-90));
  CHECK(sign.skew == doctest::Approx(12));
  CHECK(sign.vertical);
  CHECK_FALSE(sign.fit);
  CHECK(sign.color == PaletteColor{.red = 255, .green = 128, .blue = 0, .alpha = 128});
  REQUIRE(sign.outline.has_value());
  CHECK(sign.outline.value().width == doctest::Approx(1.5));
  REQUIRE(sign.glow.has_value());
  CHECK(sign.glow.value().radius == doctest::Approx(3));
  CHECK(sign.glow.value().strength == doctest::Approx(0.5));
  CHECK(sign.colors == SignColors::Region);
  CHECK(overlay.remove == std::vector<std::string>{"old"});
}

TEST_CASE("a sign overlay's defaults: the background, inpaint, centred white Noto Sans Bold") {
  const SignOverlay overlay = parsed(R"(
signs:
  - id: open
    target: background
    region: [0, 0, 8, 4]
    text: signs.open
)");
  REQUIRE(overlay.signs.size() == 1);
  const Sign& sign = overlay.signs.front();
  CHECK(sign.target.kind == SignTarget::Kind::Background);
  CHECK(sign.target.clut == -1);
  CHECK(sign.clear.mode == SignClear::Mode::Inpaint);
  CHECK_FALSE(sign.clear.rect.has_value());
  CHECK(sign.font == "noto_sans_bold");
  CHECK(sign.align == HorizontalAlign::Center);
  CHECK(sign.valign == VerticalAlign::Middle);
  CHECK(sign.fit);
  CHECK(sign.color == PaletteColor{.red = 255, .green = 255, .blue = 255, .alpha = 255});
  CHECK(sign.colors == SignColors::All);
}

TEST_CASE("a sign overlay reads a transparent clear for a sprite page") {
  const SignOverlay overlay = parsed(R"(
signs:
  - id: lit
    target: {page: 2, bits: 8, clut: 16}
    region: [0, 0, 96, 40]
    clear: transparent
    text: signs.lit
)");
  REQUIRE(overlay.signs.size() == 1);
  CHECK(overlay.signs.front().clear.mode == SignClear::Mode::Transparent);
}

TEST_CASE("a sign overlay refuses what it does not understand") {
  CHECK(problem("signs:\n  - {id: a, region: [0, 0, 1, 1], text: k, colour: red}\n")
            .contains("colour"));
  CHECK(problem("signs:\n  - {id: a, region: [0, 0, 1, 1], text: k, color: red}\n")
            .contains("#rrggbb"));
  CHECK(problem("signs:\n  - {id: a, region: [0, 0, 0, 1], text: k}\n").contains("width"));
  CHECK(problem("signs:\n  - {id: a, region: [0, 0, 1, 1], text: k, clear: smudge}\n")
            .contains("inpaint"));
  CHECK(problem("signs:\n  - {id: a, region: [0, 0, 1, 1], text: k, target: {page: 0, bits: 2}}\n")
            .contains("4 or 8"));
  CHECK(problem("signs:\n  - {id: a, region: [0, 0, 1, 1], text: k}\n"
                "  - {id: a, region: [0, 0, 1, 1], text: k}\n")
            .contains("two signs"));
}

TEST_CASE("a language's overlay replaces signs by id, adds its own and removes others") {
  SignOverlay common = parsed(R"(
signs:
  - {id: neon, region: [0, 0, 10, 4], text: signs.neon, size: 9}
  - {id: mat, region: [0, 8, 10, 4], text: signs.mat}
)");
  const SignOverlay spanish = parsed(R"(
signs:
  - {id: neon, region: [0, 0, 10, 4], text: signs.neon, size: 7}
  - {id: rules, region: [0, 16, 10, 4], text: signs.rules}
remove: [mat]
)");
  const SignOverlay merged = mergeSignOverlays(common, spanish);
  REQUIRE(merged.signs.size() == 2);
  CHECK(merged.signs[0].id == "neon");
  CHECK(merged.signs[0].size == doctest::Approx(7));
  CHECK(merged.signs[1].id == "rules");
  CHECK(merged.remove.empty());
}

TEST_CASE("sign colours are written #rrggbb or #rrggbbaa") {
  CHECK(parseSignColor("#00ff7f") ==
        PaletteColor{.red = 0, .green = 255, .blue = 127, .alpha = 255});
  CHECK(parseSignColor("#00ff7f40") ==
        PaletteColor{.red = 0, .green = 255, .blue = 127, .alpha = 0x40});
  CHECK_FALSE(parseSignColor("00ff7f").has_value());
  CHECK_FALSE(parseSignColor("#00ff7").has_value());
  CHECK_FALSE(parseSignColor("#00gg7f").has_value());
}
