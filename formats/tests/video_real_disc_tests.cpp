// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one. It decodes the
// first frames and audio sectors of each movie and checks that they look like pictures and sound; nothing it decodes
// is written anywhere.

#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/video/mdec.hpp>
#include <dwe/formats/video/str.hpp>
#include <dwe/formats/video/xa_adpcm.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace dwe::formats::disc;
using namespace dwe::formats::video;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

constexpr std::size_t FramesToDecode = 12;

} // namespace

TEST_CASE("the user's disc: the first frames and audio of each movie decode") {
    const std::optional<std::filesystem::path> mountedDisc = dwe::test_support::userDisc();
    if (!mountedDisc) {
        MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
        return;
    }
    const std::filesystem::path& disc = *mountedDisc;
    auto image = DiscImage::open(disc);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));
    const auto volume = readIsoVolume(*image);
    REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));

    std::size_t movies = 0;
    for (const auto& file : volume->files) {
        if (!file.path.starts_with("MOVIE/") || !file.path.ends_with(".STR")) {
            continue;
        }
        ++movies;
        StrFrameAssembler assembler;
        XaDecoder audio;
        std::vector<std::int16_t> samples;
        std::size_t frames = 0;
        std::size_t audioSectors = 0;
        for (std::uint32_t i = 0; i < sectorSpan(file) && frames < FramesToDecode; ++i) {
            const auto data = image->readSector(file.lba + i);
            REQUIRE_MESSAGE(data.has_value(), errorOf(data));
            const auto sector = readStrSector(*data);
            REQUIRE_MESSAGE(sector.has_value(), file.path, " sector ", i, ": ", errorOf(sector));
            if (const auto* xa = std::get_if<StrAudioSector>(&*sector)) {
                CHECK(xa->coding == XaCoding{.channels = 2, .sampleRate = 37800, .bitsPerSample = 4});
                const auto mismatches = audio.decodeSector(xa->data, xa->coding, samples);
                REQUIRE_MESSAGE(mismatches.has_value(), errorOf(mismatches));
                CHECK(*mismatches == 0);
                ++audioSectors;
                continue;
            }
            const auto* video = std::get_if<StrVideoSector>(&*sector);
            REQUIRE(video != nullptr);
            const auto frame = assembler.add(*video);
            REQUIRE_MESSAGE(frame.has_value(), file.path, " sector ", i, ": ", errorOf(frame));
            if (!frame->has_value()) {
                continue;
            }
            const StrFrame& assembled = **frame;
            CHECK(assembled.number == frames + 1);
            CHECK(assembled.width == 320);
            CHECK(assembled.height == 176);
            const auto decoded = decodeFrame(assembled.bitstream, assembled.width, assembled.height);
            REQUIRE_MESSAGE(decoded.has_value(), file.path, " frame ", assembled.number, ": ", errorOf(decoded));
            CHECK(decoded->y.size() == std::size_t{320} * 176);
            CHECK(decoded->cb.size() == std::size_t{160} * 88);
            ++frames;
        }
        CHECK(frames == FramesToDecode);
        CHECK(audioSectors > 0);
        CHECK(samples.size() == audioSectors * 4032);
        MESSAGE(file.path, ": ", frames, " frames and ", audioSectors, " audio sectors decoded");
    }
    CHECK(movies == 4);
}

TEST_CASE("the user's disc: a frame in the middle of each movie is a picture") {
    const std::optional<std::filesystem::path> mountedDisc = dwe::test_support::userDisc();
    if (!mountedDisc) {
        MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
        return;
    }
    const std::filesystem::path& disc = *mountedDisc;
    auto image = DiscImage::open(disc);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));
    const auto volume = readIsoVolume(*image);
    REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));

    for (const auto& file : volume->files) {
        if (!file.path.starts_with("MOVIE/") || !file.path.ends_with(".STR")) {
            continue;
        }
        // Start a few sectors before the middle, so the first frames are partial and skipped.
        StrFrameAssembler assembler;
        std::size_t pictures = 0;
        for (std::uint32_t i = sectorSpan(file) / 2; i < sectorSpan(file) && pictures < 3; ++i) {
            const auto data = image->readSector(file.lba + i);
            REQUIRE(data.has_value());
            const auto sector = readStrSector(*data);
            REQUIRE(sector.has_value());
            const auto* video = std::get_if<StrVideoSector>(&*sector);
            if (video == nullptr) {
                continue;
            }
            const auto frame = assembler.add(*video);
            if (!frame || !frame->has_value()) {
                continue;
            }
            const auto decoded = decodeFrame((*frame)->bitstream, (*frame)->width, (*frame)->height);
            REQUIRE_MESSAGE(decoded.has_value(), errorOf(decoded));
            const auto [low, high] = std::ranges::minmax(decoded->y);
            const auto [lowCb, highCb] = std::ranges::minmax(decoded->cb);
            MESSAGE(file.path, " frame ", (*frame)->number, ": luma ", int{low}, "..", int{high}, ", Cb ", int{lowCb},
                    "..", int{highCb});
            CHECK(high - low > 32);
            ++pictures;
        }
        CHECK(pictures == 3);
    }
}
