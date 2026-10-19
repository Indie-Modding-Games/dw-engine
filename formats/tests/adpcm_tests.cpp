#include <dwe/formats/audio/adpcm.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "audio_fixtures.hpp"

using namespace dwe::formats::audio;
using dwe::formats::audio::test::adpcmBlock;
using dwe::formats::audio::test::adpcmSample;
using dwe::formats::audio::test::nibblesOf;

namespace {

std::vector<std::byte> blocksOf(std::initializer_list<std::array<std::byte, AdpcmBlockSize>> blocks) {
    std::vector<std::byte> bytes;
    for (const auto& block : blocks) {
        bytes.insert(bytes.end(), block.begin(), block.end());
    }
    return bytes;
}

} // namespace

TEST_CASE("ADPCM: residuals are sign-extended nibbles shifted right") {
    SUBCASE("shift 0 puts the nibble at the top of 16 bits") {
        std::array<std::uint8_t, AdpcmSamplesPerBlock> nibbles = nibblesOf(0);
        nibbles[0] = 7;
        nibbles[1] = 8;
        nibbles[2] = 0xF;
        const auto sample = decodeAdpcm(blocksOf({adpcmBlock(0, 0, AdpcmFlags::End, nibbles)}));
        REQUIRE(sample.pcm.size() == AdpcmSamplesPerBlock);
        CHECK(sample.pcm[0] == 28672);
        CHECK(sample.pcm[1] == -32768);
        CHECK(sample.pcm[2] == -4096);
        CHECK(sample.pcm[3] == 0);
    }
    SUBCASE("shift 12 leaves the nibble's own value; negative values shift arithmetically") {
        std::array<std::uint8_t, AdpcmSamplesPerBlock> nibbles = nibblesOf(1);
        nibbles[1] = 0xF;
        const auto sample = decodeAdpcm(blocksOf({adpcmBlock(0, 12, 0, nibbles)}));
        CHECK(sample.pcm[0] == 1);
        CHECK(sample.pcm[1] == -1);
        CHECK(sample.reservedBlocks == 0);
    }
    SUBCASE("a reserved shift decodes as 9") {
        for (const std::uint8_t shift : std::array<std::uint8_t, 3>{13, 14, 15}) {
            const auto sample = decodeAdpcm(blocksOf({adpcmBlock(0, shift, 0, nibblesOf(1))}));
            CHECK(sample.pcm[0] == 8);
            CHECK(sample.reservedBlocks == 1);
        }
    }
    SUBCASE("the low nibble of each byte comes first") {
        std::array<std::uint8_t, AdpcmSamplesPerBlock> nibbles = nibblesOf(0);
        nibbles[0] = 2;
        nibbles[1] = 5;
        const auto sample = decodeAdpcm(blocksOf({adpcmBlock(0, 12, 0, nibbles)}));
        CHECK(sample.pcm[0] == 2);
        CHECK(sample.pcm[1] == 5);
    }
}

TEST_CASE("ADPCM: each filter predicts from the last two samples") {
    // The first block leaves 4096 in both history samples; the second has zero residuals, so it shows the
    // prediction alone: (s1 * k0 + s2 * k1 + 32) >> 6.
    const auto first = adpcmBlock(0, 0, 0, nibblesOf(1));
    const auto decode = [&first](std::uint8_t filter) {
        return decodeAdpcm(blocksOf({first, adpcmBlock(filter, 0, 0, nibblesOf(0))})).pcm[AdpcmSamplesPerBlock];
    };
    CHECK(decode(0) == 0);
    CHECK(decode(1) == 3840);
    CHECK(decode(2) == 4032);
    CHECK(decode(3) == 2752);
    CHECK(decode(4) == 3968);
    // Reserved filters decode as filter 4.
    CHECK(decode(5) == 3968);
    CHECK(decode(7) == 3968);
    CHECK(decodeAdpcm(blocksOf({first, adpcmBlock(5, 0, 0, nibblesOf(0))})).reservedBlocks == 1);

    SUBCASE("filter 1 keeps decaying through the block") {
        const auto sample = decodeAdpcm(blocksOf({first, adpcmBlock(1, 0, 0, nibblesOf(0))}));
        CHECK(sample.pcm[AdpcmSamplesPerBlock + 1] == 3600);
    }
}

TEST_CASE("ADPCM: the result is clamped to 16 bits") {
    const auto high = decodeAdpcm(blocksOf({adpcmBlock(0, 0, 0, nibblesOf(7)), adpcmBlock(1, 0, 0, nibblesOf(7))}));
    CHECK(high.pcm.back() == 32767);
    const auto low = decodeAdpcm(blocksOf({adpcmBlock(0, 0, 0, nibblesOf(8)), adpcmBlock(1, 0, 0, nibblesOf(8))}));
    CHECK(low.pcm.back() == -32768);
}

TEST_CASE("ADPCM: flags end the sample and mark its loop") {
    SUBCASE("a one-shot sample stops at its first End block, which is included") {
        const std::array<std::uint8_t, 4> flags = {0, 0, AdpcmFlags::End, 0x07};
        const auto sample = decodeAdpcm(adpcmSample(flags));
        CHECK(sample.terminated);
        CHECK(sample.blocks == 3);
        CHECK(sample.pcm.size() == 3 * AdpcmSamplesPerBlock);
        CHECK_FALSE(sample.loop.has_value());
    }
    SUBCASE("a looped sample repeats from its LoopStart block to the end of its End block") {
        const std::array<std::uint8_t, 4> flags = {0, AdpcmFlags::LoopStart, 0, AdpcmFlags::End | AdpcmFlags::Repeat};
        const auto sample = decodeAdpcm(adpcmSample(flags));
        CHECK(sample.loop == SampleLoop{.start = 28, .end = 112});
        CHECK_FALSE(sample.loopStartImplied);
    }
    SUBCASE("the last LoopStart before the end wins, and LoopStart with Repeat counts") {
        const std::array<std::uint8_t, 3> flags = {AdpcmFlags::LoopStart, AdpcmFlags::LoopStart | AdpcmFlags::Repeat,
                                                   AdpcmFlags::End | AdpcmFlags::Repeat};
        const auto sample = decodeAdpcm(adpcmSample(flags));
        CHECK(sample.loop == SampleLoop{.start = 28, .end = 84});
    }
    SUBCASE("a repeat without a LoopStart block loops from the start") {
        const std::array<std::uint8_t, 2> flags = {0, AdpcmFlags::End | AdpcmFlags::Repeat};
        const auto sample = decodeAdpcm(adpcmSample(flags));
        CHECK(sample.loop == SampleLoop{.start = 0, .end = 56});
        CHECK(sample.loopStartImplied);
    }
    SUBCASE("data without End decodes whole blocks and counts the bytes left") {
        auto bytes = adpcmSample(std::array<std::uint8_t, 2>{0, 0});
        bytes.resize(bytes.size() + 5);
        const auto sample = decodeAdpcm(bytes);
        CHECK_FALSE(sample.terminated);
        CHECK(sample.blocks == 2);
        CHECK(sample.trailingBytes == 5);
    }
    SUBCASE("no data is an empty sample") {
        const auto sample = decodeAdpcm({});
        CHECK(sample.pcm.empty());
        CHECK_FALSE(sample.terminated);
    }
    SUBCASE("header bit 7 is reserved") {
        auto block = adpcmBlock(0, 12, AdpcmFlags::End, nibblesOf(0));
        block[0] |= std::byte{0x80};
        CHECK(decodeAdpcm(block).reservedBlocks == 1);
    }
}
