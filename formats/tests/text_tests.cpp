#include <dwe/core/yaml.hpp>
#include <dwe/formats/text/charset.hpp>
#include <dwe/formats/text/font.hpp>
#include <dwe/formats/text/game_text.hpp>
#include <dwe/formats/text/string_manifest.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using dwe::formats::text::appendUtf8;
using dwe::formats::text::asciiOf;
using dwe::formats::text::buildFontAtlas;
using dwe::formats::text::ControlParameter;
using dwe::formats::text::decodeByteString;
using dwe::formats::text::decodeScriptText;
using dwe::formats::text::encodeByteString;
using dwe::formats::text::encodeScriptText;
using dwe::formats::text::fallbackGlyph;
using dwe::formats::text::fontFromAtlas;
using dwe::formats::text::fontToYaml;
using dwe::formats::text::fullWidthOf;
using dwe::formats::text::readFont;
using dwe::formats::text::scriptControlTags;
using dwe::formats::text::SjisCode;
using dwe::formats::text::sjisToUnicode;
using dwe::formats::text::takeUtf8;
using dwe::formats::text::unicodeToSjis;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

std::vector<std::byte> bytesOf(std::initializer_list<unsigned> values) {
    std::vector<std::byte> bytes;
    for (const unsigned value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

void appendU16(std::vector<std::byte>& bytes, unsigned value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
}

// Decodes script text and checks that it encodes back to the same bytes.
std::string roundTripScript(const std::vector<std::byte>& bytes) {
    const auto decoded = decodeScriptText(bytes);
    REQUIRE_MESSAGE(decoded.has_value(), errorOf(decoded));
    CHECK(decoded->size == bytes.size());
    const auto encoded = encodeScriptText(decoded->text);
    REQUIRE_MESSAGE(encoded.has_value(), errorOf(encoded));
    CHECK(*encoded == bytes);
    return decoded->text;
}

} // namespace

TEST_CASE("the game charset maps its codes one to one") {
    std::size_t mapped = 0;
    for (unsigned lead = 0x81; lead <= 0x84; ++lead) {
        for (unsigned trail = 0x40; trail <= 0xFC; ++trail) {
            const auto code = static_cast<SjisCode>(lead << 8U | trail);
            const auto character = sjisToUnicode(code);
            if (!character) {
                continue;
            }
            ++mapped;
            CHECK(unicodeToSjis(*character) == code);
        }
    }
    CHECK(mapped == 522); // the 524 characters of cp932 in these rows, less the two the font replaces
    // The font draws these two as the closing quotes; the codes cp932 gives those are not mapped.
    CHECK(sjisToUnicode(0x8175) == U'’');
    CHECK(sjisToUnicode(0x8176) == U'”');
    CHECK_FALSE(sjisToUnicode(0x8166).has_value());
    CHECK_FALSE(sjisToUnicode(0x8168).has_value());
    CHECK(sjisToUnicode(0x8260) == U'Ａ');
    CHECK_FALSE(sjisToUnicode(0x889F).has_value()); // kanji are left to tags
    CHECK_FALSE(sjisToUnicode(0x4142).has_value());
    CHECK_FALSE(unicodeToSjis(U'漢').has_value());
}

TEST_CASE("every printable ASCII character has a full-width code and back") {
    for (char c = ' '; c < '\x7F'; ++c) {
        const auto code = fullWidthOf(c);
        CHECK_MESSAGE(code.has_value(), c);
        CHECK(asciiOf(code.value_or(0)) == c);
    }
    CHECK(fullWidthOf(' ') == 0x8140);
    CHECK(fullWidthOf('.') == 0x8142);
    CHECK(fullWidthOf('\'') == 0x8175);
    CHECK(fullWidthOf('"') == 0x8176);
    CHECK(fullWidthOf('A') == 0x8260);
    CHECK_FALSE(asciiOf(0x8144).has_value()); // the other full stop stays full-width
    CHECK_FALSE(fullWidthOf('\n').has_value());
}

TEST_CASE("UTF-8 is written and read back, and malformed sequences are refused") {
    std::string text;
    for (const char32_t character : {U'A', U'é', U'あ', U'\U0001F600'}) {
        appendUtf8(text, character);
    }
    CHECK(text.size() == 1 + 2 + 3 + 4);
    std::string_view rest = text;
    CHECK(takeUtf8(rest) == U'A');
    CHECK(takeUtf8(rest) == U'é');
    CHECK(takeUtf8(rest) == U'あ');
    CHECK(takeUtf8(rest) == U'\U0001F600');
    CHECK(rest.empty());

    for (const std::string_view bad :
         {std::string_view{"\xC0\x80"}, std::string_view{"\x80"}, std::string_view{"\xED\xA0\x80"},
          std::string_view{"\xF4\x90\x80\x80"}, std::string_view{"\xE3\x81"}}) {
        std::string_view input = bad;
        CHECK_FALSE(takeUtf8(input).has_value());
    }
}

TEST_CASE("a byte string decodes to tagged UTF-8 and encodes back") {
    const auto raw = bytesOf({'H', 'i', ' ', '{', '}', 0x01, 0x7F, 0x82, 0x60, 0x81, 0x66, 0x88, 0x9F, 0x81});
    const std::string text = decodeByteString(raw);
    CHECK(text == "Hi {{}}{byte:01}{byte:7F}Ａ{sjis:8166}{sjis:889F}{byte:81}");
    const auto encoded = encodeByteString(text);
    REQUIRE_MESSAGE(encoded.has_value(), errorOf(encoded));
    CHECK(*encoded == raw);
    CHECK(decodeByteString({}).empty());
}

TEST_CASE("byte string text that has no bytes is refused") {
    for (const std::string_view bad : {"{byte:00}", "{byte:81}x", "{byte:1}", "{sjis:4142}", "{sjis:8100}", "{sjis:81}",
                                       "{color:1}", "}", "{byte:01", "\xFF", "漢"}) {
        CHECK_FALSE_MESSAGE(encodeByteString(bad).has_value(), bad);
    }
}

TEST_CASE("every control code of script text has a tag that encodes back") {
    REQUIRE(scriptControlTags().size() == 26);
    for (const auto& tag : scriptControlTags()) {
        for (const unsigned parameter : {0x00U, 0x05U}) {
            const std::string text = roundTripScript(bytesOf({tag.code, parameter, 0x0D, 0x00, 0x00, 0x00}));
            const std::string name{tag.name};
            switch (tag.parameter) {
            case ControlParameter::Value:
                CHECK(text == std::format("{{{}:{}}}", name, parameter));
                break;
            case ControlParameter::Pstat:
                CHECK(text == std::format("{{{}:pstat={}}}", name, parameter));
                break;
            case ControlParameter::Raw:
                CHECK(text == std::format("{{{}:{:02X}}}", name, parameter));
                break;
            case ControlParameter::Ignored:
                CHECK(text == (parameter == 0 ? std::format("{{{}}}", name) : std::format("{{{}:raw=05}}", name)));
                break;
            }
        }
    }
}

TEST_CASE("script text keeps glyphs, new lines and odd cells") {
    std::vector<std::byte> bytes;
    for (const unsigned code : {0x8260U, 0x8140U, 0x8142U, 0x8144U, 0x816FU, 0x8170U, 0x8175U, 0x889FU, 0x4142U}) {
        bytes.push_back(static_cast<std::byte>(code >> 8U));
        bytes.push_back(static_cast<std::byte>(code & 0xFFU));
    }
    const auto tail =
        bytesOf({0x0D, 0x00, 0x0D, 0x07, 0x03, 0x01, 0x02, 0x03, 0x00, 0x09, 0x1D, 0x05, 0x0D, 0x00, 0x00, 0x00});
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    const std::string text = roundTripScript(bytes);
    CHECK(text == "A .．{{}}'{cell:889F}{cell:4142}\n{newline:raw=07}{raw03:010203}{cell:0009}{cell:1D05}");

    // Bytes after the final cells are not part of the run; final cells other than 0 are kept.
    const auto ended = decodeScriptText(bytesOf({0x82, 0x60, 0x0D, 0x01, 0x00, 0x02, 0xAA, 0xBB}));
    REQUIRE_MESSAGE(ended.has_value(), errorOf(ended));
    CHECK(ended->size == 6);
    CHECK(ended->text == "A{end:0102}");
    const auto reencoded = encodeScriptText(ended->text);
    REQUIRE_MESSAGE(reencoded.has_value(), errorOf(reencoded));
    CHECK(*reencoded == bytesOf({0x82, 0x60, 0x0D, 0x01, 0x00, 0x02}));
    CHECK(roundTripScript(bytesOf({0x0D, 0x00, 0x00, 0x00})).empty());
}

TEST_CASE("malformed script text is refused") {
    CHECK_FALSE(decodeScriptText(bytesOf({0x82, 0x60})).has_value());
    CHECK_FALSE(decodeScriptText(bytesOf({0x82, 0x60, 0x03, 0x01})).has_value());
    CHECK_FALSE(decodeScriptText(bytesOf({0x0D, 0x00, 0x00})).has_value());
    CHECK_FALSE(decodeScriptText(bytesOf({0x82})).has_value());
    for (const std::string_view bad : {"\n{cell:0041}",
                                       "{end:0000}A",
                                       "{end:00}",
                                       "{cell:0D00}",
                                       "{cell:0300}",
                                       "{cell:41}",
                                       "{color:256}",
                                       "{color:x}",
                                       "{item:5}",
                                       "{item:pstat=}",
                                       "{tamer:raw=1}",
                                       "{tamer:5}",
                                       "{raw02:G1}",
                                       "{raw03:0102}",
                                       "{newline:raw=}",
                                       "{unknown}",
                                       "{sjis:8260}",
                                       "漢",
                                       "{",
                                       "}"}) {
        CHECK_FALSE_MESSAGE(encodeScriptText(bad).has_value(), bad);
    }
}

TEST_CASE("a font of two glyphs goes to an atlas and back") {
    std::vector<std::byte> codes;
    appendU16(codes, 0x8260);
    appendU16(codes, 0x817E);
    std::vector<std::byte> glyphs;
    for (std::size_t row = 0; row < dwe::formats::text::GlyphRows; ++row) {
        appendU16(glyphs, row == 0 ? 0x7FFFU : 0xFFFFU); // column 0 of the top row is ink
    }
    appendU16(glyphs, 10);
    for (std::size_t row = 0; row < dwe::formats::text::GlyphRows; ++row) {
        appendU16(glyphs, row == 10 ? 0xFFEFU : 0xFFFFU); // column 11 of the bottom row is ink
    }
    appendU16(glyphs, 12);
    std::vector<std::byte> clut;
    for (unsigned i = 0; i < dwe::formats::text::FontClutSize; ++i) {
        appendU16(clut, i * 0x0421U);
    }

    const auto font = readFont(codes, glyphs, clut);
    REQUIRE_MESSAGE(font.has_value(), errorOf(font));
    REQUIRE(font->glyphs.size() == 2);
    CHECK(font->glyphs[0].code == 0x8260);
    CHECK(font->glyphs[0].width == 10);
    CHECK(font->glyphs[0].ink(0, 0));
    CHECK_FALSE(font->glyphs[0].ink(1, 0));
    CHECK(font->glyphs[1].ink(11, 10));
    CHECK(font->clut[2] == 0x0842);
    CHECK(fallbackGlyph(*font) == 1);

    const auto atlas = buildFontAtlas(*font, 2);
    CHECK(atlas.width == 24);
    CHECK(atlas.height == 11);
    REQUIRE(atlas.indices.size() == 24U * 11U);
    CHECK(atlas.indices[0] == 1);
    CHECK(atlas.indices[1] == 0);
    CHECK(atlas.indices[(10 * 24) + 23] == 1);

    const auto yaml = dwe::core::emitYaml(fontToYaml(*font, atlas));
    REQUIRE_MESSAGE(yaml.has_value(), errorOf(yaml));
    const auto parsed = dwe::core::parseYaml(*yaml);
    REQUIRE_MESSAGE(parsed.has_value(), errorOf(parsed));
    const auto back = fontFromAtlas(atlas, *parsed);
    REQUIRE_MESSAGE(back.has_value(), errorOf(back));
    CHECK(*back == *font);
}

TEST_CASE("a font whose tables do not fit is refused") {
    std::vector<std::byte> codes;
    appendU16(codes, 0x8260);
    appendU16(codes, 0x8260);
    std::vector<std::byte> glyphs;
    for (int glyph = 0; glyph < 2; ++glyph) {
        for (std::size_t row = 0; row <= dwe::formats::text::GlyphRows; ++row) {
            appendU16(glyphs, 0xFFFF);
        }
    }
    const std::vector<std::byte> clut(dwe::formats::text::FontClutSize * 2);
    CHECK_FALSE(readFont(codes, glyphs, clut).has_value()); // a code repeats

    codes[2] = std::byte{0x7E};
    codes[3] = std::byte{0x81};
    CHECK(readFont(codes, glyphs, clut).has_value());
    CHECK_FALSE(readFont(std::span{codes}.first(2), glyphs, clut).has_value());
    CHECK_FALSE(readFont(codes, glyphs, std::span{clut}.first(30)).has_value());
    glyphs[0] = std::byte{0xF0}; // bit 3 of the first row is clear
    CHECK_FALSE(readFont(codes, glyphs, clut).has_value());
}

TEST_CASE("the US string manifest names unique blocks with sane layouts") {
    const auto manifest = dwe::formats::text::findStringManifest("us");
    if (!manifest) {
        FAIL("the US has no string manifest");
        return;
    }
    CHECK(manifest->language == "en");
    CHECK_FALSE(dwe::formats::text::findStringManifest("jp").has_value());
    for (std::size_t i = 0; i < manifest->blocks.size(); ++i) {
        const auto& block = manifest->blocks[i];
        CHECK(block.count > 0);
        if (block.storage == dwe::formats::text::StringStorage::Named) {
            CHECK(block.addresses.size() == block.count);
        }
        if (block.storage == dwe::formats::text::StringStorage::Records) {
            CHECK(block.length <= block.stride);
        }
        for (std::size_t j = 0; j < i; ++j) {
            CHECK_FALSE((manifest->blocks[j].source == block.source && manifest->blocks[j].name == block.name));
        }
    }
}
