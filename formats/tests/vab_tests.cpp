#include <dwe/formats/audio/vab.hpp>
#include <dwe/formats/audio/vhb.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "audio_fixtures.hpp"

using namespace dwe::formats::audio;
using dwe::formats::audio::test::buildBankVhb;
using dwe::formats::audio::test::buildFontVhb;
using dwe::formats::audio::test::buildVab;
using dwe::formats::audio::test::Bytes;
using dwe::formats::audio::test::ProgramSpec;
using dwe::formats::audio::test::ToneSpec;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// Two programs in slots 0 and 3 (a gap, as two music banks have), three samples.
std::vector<ProgramSpec> twoPrograms() {
    return {ProgramSpec{.slot = 0, .tones = {ToneSpec{.min = 0, .max = 59, .vag = 1}, ToneSpec{.min = 60, .vag = 2}}},
            ProgramSpec{.slot = 3, .tones = {ToneSpec{.center = 48, .shift = 32, .vag = 3}}, .mvol = 100, .mpan = 32}};
}

const std::vector<std::size_t> threeSamples = {32, 48, 16};

void corrupt(std::vector<std::byte>& bytes, std::size_t offset, std::uint8_t value) {
    bytes[offset] = std::byte{value};
}

} // namespace

TEST_CASE("VAB: the header, programs, tones and sample sizes are read") {
    const auto vh = buildVab(twoPrograms(), threeSamples, 3);
    const auto vab = readVab(vh);
    REQUIRE_MESSAGE(vab.has_value(), errorOf(vab));
    CHECK(vab->header.version == 7);
    CHECK(vab->header.ps == 2);
    CHECK(vab->header.ts == 3);
    CHECK(vab->header.vs == 3);
    CHECK(vab->size() == 0x20 + 2048 + (2 * 512) + 512);
    CHECK(vab->size() == vh.size());
    CHECK(vab->vbSize() == 96);
    REQUIRE(vab->toneBlocks.size() == 2);
    CHECK(vab->toneBlocks[1].program == 3);
    CHECK(vab->programs[3].mvol == 100);
    CHECK(vab->programs[3].mpan == 32);
    CHECK(vab->tonesOf(0).size() == 2);
    CHECK(vab->tonesOf(1).empty());
    const auto tones = vab->tonesOf(3);
    REQUIRE(tones.size() == 1);
    CHECK(tones[0].center == 48);
    CHECK(tones[0].shift == 32);
    CHECK(tones[0].prog == 3);
    CHECK(tones[0].vag == 3);
    CHECK(tones[0].adsr1 == 0x80FF);
    CHECK(tones[0].adsr2 == 0x5FC0);
    CHECK(vab->vagSizes[2] == 6);
}

TEST_CASE("VAB: samples are located one after another in the VB") {
    SUBCASE("in order, skipping zero sizes") {
        const auto vab = readVab(buildVab(twoPrograms(), {32, 48, 16, 0, 16}, 5));
        REQUIRE(vab.has_value());
        const auto vags = locateVags(*vab, vab->vbSize());
        REQUIRE_MESSAGE(vags.has_value(), errorOf(vags));
        REQUIRE(vags->size() == 4);
        CHECK((*vags)[1].offset == 32);
        CHECK((*vags)[3].number == 5);
        CHECK((*vags)[3].offset == 96);
        CHECK_FALSE((*vags)[3].beyondCount);
    }
    SUBCASE("a sample beyond vs is returned and marked while it fits") {
        auto vh = buildVab(twoPrograms(), {32, 48, 16, 16}, 3);
        const auto vab = readVab(vh);
        REQUIRE(vab.has_value());
        const auto vags = locateVags(*vab, 112);
        REQUIRE(vags.has_value());
        REQUIRE(vags->size() == 4);
        CHECK((*vags)[3].beyondCount);
        CHECK(locateVags(*vab, 96)->size() == 3);
    }
    SUBCASE("a counted sample that does not fit is an error") {
        const auto vab = readVab(buildVab(twoPrograms(), threeSamples, 3));
        REQUIRE(vab.has_value());
        CHECK_FALSE(locateVags(*vab, 64).has_value());
    }
}

TEST_CASE("VAB: malformed headers are errors") {
    const auto good = buildVab(twoPrograms(), threeSamples, 3);
    SUBCASE("magic") {
        auto bytes = good;
        corrupt(bytes, 0, 'X');
        CHECK_FALSE(readVab(bytes).has_value());
    }
    SUBCASE("too short for its programs") {
        CHECK_FALSE(readVab(std::span{good}.first(good.size() - 1)).has_value());
        CHECK_FALSE(readVab(std::span{good}.first(8)).has_value());
    }
    SUBCASE("ps does not match the programs with tones") {
        auto bytes = good;
        corrupt(bytes, 0x12, 3);
        CHECK_FALSE(readVab(bytes).has_value());
    }
    SUBCASE("a tone that names another program") {
        auto bytes = good;
        corrupt(bytes, 0x20 + 2048 + 0x14, 5);
        CHECK_FALSE(readVab(bytes).has_value());
    }
    SUBCASE("a tone that plays a sample outside vs") {
        auto bytes = good;
        corrupt(bytes, 0x20 + 2048 + 0x16, 9);
        CHECK_FALSE(readVab(bytes).has_value());
    }
    SUBCASE("a program with more than 16 tones") {
        auto bytes = good;
        corrupt(bytes, 0x20, 17);
        CHECK_FALSE(readVab(bytes).has_value());
    }
    SUBCASE("fsize smaller than the VH") {
        auto bytes = good;
        corrupt(bytes, 0x0C, 0);
        corrupt(bytes, 0x0D, 0);
        CHECK_FALSE(readVab(bytes).has_value());
    }
}

TEST_CASE("VHB: sound banks and music fonts") {
    const auto vh = buildVab(twoPrograms(), threeSamples, 3);
    const std::vector<std::byte> vb(96, std::byte{0});
    SUBCASE("a sound bank has a 12-byte header") {
        const auto bytes = buildBankVhb(vh, vb);
        const auto vhb = readVhb(bytes);
        REQUIRE_MESSAGE(vhb.has_value(), errorOf(vhb));
        CHECK_FALSE(vhb->musicFont);
        CHECK(vhb->vhOffset == 12);
        CHECK(vhb->vb.size() == 96);
        CHECK(vhb->end == bytes.size());
        CHECK(vhb->seqBank.empty());
    }
    SUBCASE("a music font has a 16-byte header and its SEQs after the VB") {
        const std::vector<std::byte> seqs(20, std::byte{1});
        const auto vhb = readVhb(buildFontVhb(vh, vb, seqs));
        REQUIRE_MESSAGE(vhb.has_value(), errorOf(vhb));
        CHECK(vhb->musicFont);
        CHECK(vhb->seqBank.size() == 20);
    }
    SUBCASE("padding after the data is allowed") {
        auto bytes = buildBankVhb(vh, vb);
        bytes.resize(bytes.size() + 100);
        CHECK(readVhb(bytes).has_value());
    }
    SUBCASE("broken headers") {
        auto bytes = buildBankVhb(vh, vb);
        auto other = bytes;
        corrupt(other, 0, 8);
        CHECK_FALSE(readVhb(other).has_value());
        other = bytes;
        corrupt(other, 4, static_cast<std::uint8_t>(std::to_integer<int>(bytes[4]) + 4));
        CHECK_FALSE(readVhb(other).has_value());
        other = bytes;
        corrupt(other, 8, static_cast<std::uint8_t>(std::to_integer<int>(bytes[8]) - 16));
        CHECK_FALSE(readVhb(other).has_value());
        CHECK_FALSE(readVhb(std::span{bytes}.first(bytes.size() - 1)).has_value());
        CHECK_FALSE(readVhb(std::span{bytes}.first(10)).has_value());
        auto font = buildFontVhb(vh, vb, std::vector<std::byte>(4));
        corrupt(font, 12, 0xFF);
        CHECK_FALSE(readVhb(font).has_value());
    }
}

TEST_CASE("sound files: the known files and their entries") {
    CHECK(soundFiles().size() == 7);
    const SoundFile* vball = findSoundFile("SOUND/VHB/VBALL.VHB");
    REQUIRE(vball != nullptr);
    CHECK(vball->layout == SoundFileLayout::FixedStride);
    const auto entries = fixedStrideEntries(*vball);
    REQUIRE(entries.size() == 71);
    CHECK(entries[2] == SectorRange{.first = 14, .count = 7});
    CHECK(findSoundFile("SOUND/VHB/NONE.VHB") == nullptr);

    Bytes file;
    file.fill(3 * SoundSectorSize);
    const std::vector<SectorRange> ranges = {{.first = 0, .count = 1}, {.first = 1, .count = 2}};
    const auto parts = splitSoundFile(file.bytes(), ranges);
    REQUIRE(parts.has_value());
    CHECK((*parts)[1].size() == 2 * SoundSectorSize);
    const std::vector<SectorRange> outside = {{.first = 2, .count = 2}};
    CHECK_FALSE(splitSoundFile(file.bytes(), outside).has_value());
}
