// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one. Nothing read
// from the disc is printed but counts.

#include <dwe/core/sha1.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/boot.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/known_releases.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/scn_file.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/script_yaml.hpp>
#include <dwe/formats/text/charset.hpp>
#include <dwe/formats/text/font.hpp>
#include <dwe/formats/text/game_text.hpp>
#include <dwe/formats/text/string_manifest.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace dwe::formats;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

constexpr std::uint8_t SetSelection = 0x10;

struct ScriptCounts {
    std::size_t scripts = 0;
    std::size_t instructions = 0;
    std::size_t texts = 0;
    std::size_t unknowns = 0;
    std::size_t unreachable = 0;
    std::size_t textMismatches = 0;
    std::size_t codeBytes = 0;
    std::set<std::uint8_t> opcodes;
};

// Disassembles a script and checks that each of its texts encodes back to the bytes it came from.
void countScript(std::span<const std::byte> bytes, ScriptCounts& counts) {
    const auto disassembly = script::disassembleScript(bytes);
    REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
    ++counts.scripts;
    counts.instructions += disassembly->instructions.size();
    counts.unknowns += disassembly->unknowns.size();
    counts.unreachable += disassembly->unreachable;
    counts.codeBytes += disassembly->codeEnd - disassembly->header.codeStart;
    for (const auto& instruction : disassembly->instructions) {
        counts.opcodes.insert(instruction.opcode);
        if (!instruction.text) {
            continue;
        }
        ++counts.texts;
        // SHOW_TEXTBOX: opcode and pad. SET_SELECTION: opcode, count, the options and the SHOW_TEXTBOX word.
        const std::size_t start =
            instruction.offset + 2 + (instruction.opcode == SetSelection ? (instruction.targets.size() * 2) + 2 : 0);
        const auto encoded = text::encodeScriptText(*instruction.text);
        if (!encoded || start + encoded->size() > bytes.size() ||
            !std::ranges::equal(*encoded, bytes.subspan(start, encoded->size()))) {
            ++counts.textMismatches;
        }
    }
}

struct DiscFiles {
    disc::DiscImage image;
    disc::IsoVolume volume;

    [[nodiscard]] std::vector<std::byte> read(std::string_view path) {
        const auto found = std::ranges::find(volume.files, path, &disc::IsoFile::path);
        REQUIRE(found != volume.files.end());
        auto bytes = disc::readIsoFile(image, *found);
        REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
        return std::move(*bytes);
    }
};

std::optional<DiscFiles> openDisc() {
    const std::optional<std::filesystem::path> mountedPath = dwe::test_support::userDisc();
    if (!mountedPath) {
        MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
        return std::nullopt;
    }
    const std::filesystem::path& path = *mountedPath;
    auto image = disc::DiscImage::open(path);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));
    auto volume = disc::readIsoVolume(*image);
    REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));
    return DiscFiles{.image = std::move(*image), .volume = std::move(*volume)};
}

} // namespace

TEST_CASE("the user's disc: every event script decodes and its texts encode back") {
    auto files = openDisc();
    if (!files) {
        return;
    }
    const auto mapHead = files->read("SCN/MAPHEAD.SCN");
    const auto pack = files->read("SCN/DG.SCN");
    const auto slots = script::readScriptTable(pack);
    REQUIRE_MESSAGE(slots.has_value(), errorOf(slots));
    REQUIRE(slots->size() > 1);

    ScriptCounts counts;
    countScript(mapHead, counts);
    const auto copy = std::span{pack}.subspan((*slots)[0].offset, (*slots)[0].size);
    CHECK(copy.size() >= mapHead.size());
    CHECK(std::ranges::equal(copy.first(std::min(copy.size(), mapHead.size())), mapHead));
    for (std::size_t id = 1; id < slots->size(); ++id) {
        countScript(std::span{pack}.subspan((*slots)[id].offset, (*slots)[id].size), counts);
    }
    MESSAGE("scripts ", counts.scripts, ", instructions ", counts.instructions, ", code bytes ", counts.codeBytes,
            ", opcodes used ", counts.opcodes.size(), ", texts ", counts.texts, ", unreachable ", counts.unreachable,
            ", unknowns ", counts.unknowns);
    CHECK(counts.scripts == slots->size());
    CHECK(counts.unknowns == 0);
    CHECK(counts.textMismatches == 0);
}

TEST_CASE("the user's disc: every event script's listing YAML text reads back as its disassembly's listing") {
    auto files = openDisc();
    if (!files) {
        return;
    }
    const auto pack = files->read("SCN/DG.SCN");
    const auto slots = script::readScriptTable(pack);
    REQUIRE_MESSAGE(slots.has_value(), errorOf(slots));
    std::size_t mismatches = 0;
    std::size_t texts = 0;
    for (const auto& slot : *slots) {
        const auto disassembly = script::disassembleScript(std::span{pack}.subspan(slot.offset, slot.size));
        REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
        const auto text = dwe::core::emitYaml(script::scriptToYaml(*disassembly));
        REQUIRE_MESSAGE(text.has_value(), errorOf(text));
        const auto parsed = dwe::core::parseYaml(*text);
        REQUIRE_MESSAGE(parsed.has_value(), errorOf(parsed));
        const auto read = script::readScriptListing(*parsed);
        REQUIRE_MESSAGE(read.has_value(), errorOf(read));
        if (*read != script::listingOf(*disassembly)) {
            ++mismatches;
        }
        texts += static_cast<std::size_t>(std::ranges::count_if(
            read->instructions, [](const auto& instruction) { return instruction.text.has_value(); }));
    }
    MESSAGE("scripts ", slots->size(), ", texts ", texts, ", listings that differ ", mismatches);
    CHECK(texts > 0);
    CHECK(mismatches == 0);
}

TEST_CASE("the user's disc: the executable's strings and font read and encode back") {
    auto files = openDisc();
    if (!files) {
        return;
    }
    const auto executable = disc::findBootExecutable(files->image, files->volume);
    REQUIRE_MESSAGE(executable.has_value(), errorOf(executable));
    const auto bytes = files->read(executable->path);
    const auto release = disc::findKnownRelease(dwe::core::sha1(bytes));
    if (!release) {
        FAIL("the executable is not a known release");
        return;
    }
    const auto manifest = text::findStringManifest(release->version);
    if (!manifest) {
        MESSAGE("the strings of version ", release->version, " are not located: skipped");
        return;
    }
    const auto location = text::findFontLocation(release->version);
    if (!location) {
        MESSAGE("the font of version ", release->version, " is not located: skipped");
        return;
    }
    const auto image = exe::MemoryImage::fromExecutable(bytes);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));

    const auto codes = image->bytesAt(location->charToGlyphTable, std::size_t{location->glyphCount} * 2);
    const auto glyphs = image->bytesAt(location->glyphData, std::size_t{location->glyphCount} * 24);
    const auto clut = image->bytesAt(location->clut, text::FontClutSize * 2);
    REQUIRE((codes && glyphs && clut));
    const auto font = text::readFont(*codes, *glyphs, *clut);
    REQUIRE_MESSAGE(font.has_value(), errorOf(font));
    const auto atlas = text::buildFontAtlas(*font, 16);
    const auto back = text::fontFromAtlas(atlas, text::fontToYaml(*font, atlas));
    REQUIRE_MESSAGE(back.has_value(), errorOf(back));
    CHECK(*back == *font);
    // Every glyph code has a character, so the font's map is all text.
    CHECK(std::ranges::all_of(font->glyphs,
                              [](const text::Glyph& glyph) { return text::sjisToUnicode(glyph.code).has_value(); }));

    std::size_t strings = 0;
    std::size_t mismatches = 0;
    const auto check = [&](const text::StringBlock& block, const exe::MemoryImage& source) {
        const auto read = text::readStringBlock(block, source, *image);
        REQUIRE_MESSAGE(read.has_value(), errorOf(read));
        for (const auto& raw : *read) {
            if (!raw) {
                continue;
            }
            ++strings;
            const auto rawBytes = std::as_bytes(std::span{*raw});
            const auto encoded = text::encodeByteString(text::decodeByteString(rawBytes));
            if (!encoded || !std::ranges::equal(*encoded, rawBytes)) {
                ++mismatches;
            }
        }
    };
    for (const auto& block : manifest->blocks) {
        if (block.source.empty()) {
            check(block, *image);
        }
    }
    for (const auto& overlay : manifest->overlays) {
        const auto overlayBytes = files->read(overlay.file);
        const auto overlayImage = exe::MemoryImage::fromOverlay(overlayBytes, overlay.loadAddress);
        for (const auto& block : manifest->blocks) {
            if (block.source == overlay.file) {
                check(block, overlayImage);
            }
        }
    }
    MESSAGE("glyphs ", font->glyphs.size(), ", string blocks ", manifest->blocks.size(), ", strings ", strings);
    CHECK(mismatches == 0);
}
