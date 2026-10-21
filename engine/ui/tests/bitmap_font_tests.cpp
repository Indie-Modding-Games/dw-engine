#include <dwe/ui/bitmap_font.hpp>
#include <dwe/ui/text_font.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bitmap_font_fixture.hpp"

namespace dwe::ui {
namespace {

BitmapFont fixtureFont() {
    auto font = BitmapFont::parse(fixture::Meta, fixture::atlas(), fixture::Baseline);
    REQUIRE_MESSAGE(font.has_value(), font.error().message);
    return std::move(*font);
}

struct Run {
    std::string text;
    bool bitmap = false;

    friend bool operator==(const Run&, const Run&) = default;
    friend std::ostream& operator<<(std::ostream& stream, const Run& run) {
        return stream << (run.bitmap ? "bitmap '" : "fallback '") << run.text << "'";
    }
};

std::vector<Run> runsOf(const BitmapFont& font, std::string_view text) {
    std::vector<Run> runs;
    for (const TextRun& run : font.splitRuns(text)) {
        runs.push_back({.text = std::string{run.text}, .bitmap = run.bitmap});
    }
    return runs;
}

std::string replaced(std::string_view text, std::string_view from, std::string_view to) {
    std::string result{text};
    result.replace(result.find(from), from.size(), to);
    return result;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("the glyphs answer to their character and to their ASCII equivalent") {
    const BitmapFont font = fixtureFont();
    CHECK(font.cellWidth() == fixture::CellWidth);
    CHECK(font.cellHeight() == fixture::CellHeight);
    CHECK(font.baseline() == fixture::Baseline);
    // Ａ, A, Ｂ, B and ○, which has no ASCII equivalent.
    CHECK(font.characterCount() == 5);

    const BitmapGlyph* fullWidth = font.glyph(U'Ａ');
    const BitmapGlyph* ascii = font.glyph(U'A');
    REQUIRE(fullWidth != nullptr);
    REQUIRE(ascii != nullptr);
    CHECK(fullWidth->x == 0);
    CHECK(ascii->x == 0);
    CHECK(ascii->advance == 3);

    const BitmapGlyph* letterB = font.glyph(U'B');
    REQUIRE(letterB != nullptr);
    CHECK(letterB->x == fixture::CellWidth);
    CHECK(letterB->y == 0);
    CHECK(letterB->advance == 4);

    const BitmapGlyph* circle = font.glyph(U'○');
    REQUIRE(circle != nullptr);
    CHECK(circle->x == 2 * fixture::CellWidth);
    CHECK(font.inkTop(*circle) == 4);
}

TEST_CASE("characters the font lacks are missing, not drawn as the original's fallback glyph") {
    const BitmapFont font = fixtureFont();
    for (char32_t missing : {U'C', U'ñ', U'á', U'¿', U'¡', U' '}) {
        CAPTURE(static_cast<std::uint32_t>(missing));
        CHECK(font.glyph(missing) == nullptr);
    }
}

TEST_CASE("text splits into runs the bitmap font draws and runs that fall back") {
    const BitmapFont font = fixtureFont();
    CHECK(runsOf(font, "") == std::vector<Run>{});
    CHECK(runsOf(font, "AB") == std::vector<Run>{{.text = "AB", .bitmap = true}});
    CHECK(runsOf(font, "ñ") == std::vector<Run>{{.text = "ñ", .bitmap = false}});
    CHECK(runsOf(font, "A¿ñB○") == std::vector<Run>{{.text = "A", .bitmap = true},
                                                    {.text = "¿ñ", .bitmap = false},
                                                    {.text = "B○", .bitmap = true}});
    CHECK(runsOf(font, "¡AÁ") == std::vector<Run>{{.text = "¡", .bitmap = false},
                                                  {.text = "A", .bitmap = true},
                                                  {.text = "Á", .bitmap = false}});
    // A byte that is not UTF-8 is not a glyph: it goes to the fallback, which shows what it shows for such bytes.
    CHECK(runsOf(font, "A\xFF"
                       "B") == std::vector<Run>{{.text = "A", .bitmap = true},
                                                {.text = "\xFF", .bitmap = false},
                                                {.text = "B", .bitmap = true}});
}

TEST_CASE("the ink atlas is white ink over transparency, each pixel repeated by the scale") {
    const BitmapFont font = fixtureFont();
    const assets::Image ink = font.inkAtlas(3);
    CHECK(ink.width == fixture::CellWidth * fixture::Cells * 3);
    CHECK(ink.height == fixture::CellHeight * 3);
    REQUIRE(ink.pixels.size() == std::size_t{ink.width} * ink.height * 4);
    const auto pixel = [&](std::size_t x, std::size_t y) { return ink.pixels[(((y * ink.width) + x) * 4) + 3]; };
    // Glyph 0 has ink in column 0 from row 2: atlas rows 6 to 8 are row 2 three times over.
    CHECK(pixel(0, 5) == std::byte{0});
    CHECK(pixel(0, 6) == std::byte{0xFF});
    CHECK(pixel(2, 8) == std::byte{0xFF});
    CHECK(pixel(3, 8) == std::byte{0});
    CHECK(pixel(0, std::size_t{fixture::Baseline} * 3) == std::byte{0});
    CHECK(font.inkAtlas(0).width == fixture::CellWidth * fixture::Cells);
}

TEST_CASE("a bitmap font that does not fit its atlas or its baseline is refused") {
    CHECK_FALSE(BitmapFont::parse(replaced(fixture::Meta, "x: 12", "x: 13"), fixture::atlas(), fixture::Baseline));
    CHECK_FALSE(BitmapFont::parse(replaced(fixture::Meta, "ascii: \"A\"", "ascii: \"AB\""), fixture::atlas(),
                                  fixture::Baseline));
    CHECK_FALSE(BitmapFont::parse(replaced(fixture::Meta, "cell_height: 11", "cell_height: 0"), fixture::atlas(),
                                  fixture::Baseline));
    CHECK_FALSE(BitmapFont::parse(fixture::Meta, fixture::atlas(), 0));
    CHECK_FALSE(BitmapFont::parse(fixture::Meta, fixture::atlas(), fixture::CellHeight + 1));
    assets::Image truncated = fixture::atlas();
    truncated.pixels.pop_back();
    CHECK_FALSE(BitmapFont::parse(fixture::Meta, truncated, fixture::Baseline));
    CHECK_FALSE(BitmapFont::parse("glyphs: []", fixture::atlas(), fixture::Baseline));
}

TEST_CASE("the text font setting's names") {
    CHECK(textFontName(TextFont::Modern) == "modern");
    CHECK(textFontName(TextFont::Original) == "original");
    CHECK(textFontNamed("modern") == TextFont::Modern);
    CHECK(textFontNamed("original") == TextFont::Original);
    CHECK_FALSE(textFontNamed("Original").has_value());
    CHECK_FALSE(textFontNamed("").has_value());
}

TEST_CASE("the game pack's original look reads the extracted font and defaults to the modern one") {
    const std::string yaml = readFile(std::filesystem::path{DWE_GAME_PACK} / "ui/fonts/original_look.yaml");
    auto look = parseOriginalLook(yaml);
    REQUIRE_MESSAGE(look.has_value(), look.error().message);
    CHECK(look->family == "original-look");
    CHECK(look->defaultFont == TextFont::Modern);
    CHECK(look->modernFamily == "Noto Sans");
    CHECK(look->atlas == "font/main");
    CHECK(look->meta == "font/main_meta");
    CHECK(look->baseline == 10);
    CHECK(look->fallbackSize > 0.0F);

    CHECK_FALSE(parseOriginalLook(replaced(yaml, "default: modern", "default: retro")));
    CHECK_FALSE(parseOriginalLook(replaced(yaml, "fallback_size: ", "fallback_size: -")));
    CHECK_FALSE(parseOriginalLook(replaced(yaml, "family:", "familia:")));
    // dwx cook keeps only the images under font/ as PNG, and the bitmap font reads their exact pixels.
    const auto elsewhere = parseOriginalLook(replaced(yaml, "atlas: font/main", "atlas: texture/font"));
    REQUIRE_FALSE(elsewhere.has_value());
    CHECK(elsewhere.error().message.contains("it must live under font/"));
    CHECK(parseOriginalLook(replaced(yaml, "atlas: font/main", "atlas: font/large")).has_value());
}

} // namespace dwe::ui
