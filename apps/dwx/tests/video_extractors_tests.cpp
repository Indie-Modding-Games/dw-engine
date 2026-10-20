#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>
#include <dwe/formats/video/str.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base_pack.hpp"
#include "disc_fixtures.hpp"
#include "extractor.hpp"
#include "video_extractors.hpp"
#include "video_fixtures.hpp"
#include "written_pack.hpp"

using namespace dwe::dwx;
using dwe::formats::disc::SectorForm;
using Bytes = std::vector<std::byte>;
namespace submode = dwe::formats::video::submode;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

constexpr std::uint32_t MovieSectors = 10;
constexpr std::uint8_t VideoSubmode = submode::Data | submode::RealTime;
constexpr std::uint8_t AudioSubmode = submode::Audio | submode::Form2 | submode::RealTime;
constexpr std::uint8_t StereoCoding = 0x01;

// A one-macroblock frame where every block has only a DC of 0, or a broken header when broken is set.
Bytes frameBitstream(bool broken = false) {
    fixtures::BitWriter bits;
    bits.bits("00 10 00 10"); // Cr and Cb
    for (int block = 0; block < 4; ++block) {
        bits.bits("100 10");
    }
    auto stream = fixtures::makeBitstream(3, 1, 12, bits);
    if (broken) {
        fixtures::putU16(stream, 2, 0x3801);
    }
    return stream;
}

std::uint32_t wordAt(const Bytes& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    }
    return value;
}

// A raw mode 2 sector with this subheader and user data.
Bytes rawSector(std::span<const std::byte> data, SectorForm form, std::uint8_t mode, std::uint8_t coding = 0,
                std::uint8_t channel = 1) {
    auto sector = fixtures::makeRawSector(data, 2, form);
    for (const std::size_t copy : {std::size_t{16}, std::size_t{20}}) {
        fixtures::putU8(sector, copy, 1);
        fixtures::putU8(sector, copy + 1, channel);
        fixtures::putU8(sector, copy + 2, mode);
        fixtures::putU8(sector, copy + 3, coding);
    }
    return sector;
}

struct FrameSize {
    std::uint16_t width = 16;
    std::uint16_t height = 16;
};

Bytes videoSector(std::uint32_t number, const Bytes& frame, std::uint16_t sectorsInFrame = 1, FrameSize size = {}) {
    const auto data = fixtures::makeVideoSectorData({.sectorInFrame = 0,
                                                     .sectorsInFrame = sectorsInFrame,
                                                     .frameNumber = number,
                                                     .frameBytes = static_cast<std::uint32_t>(frame.size()),
                                                     .width = size.width,
                                                     .height = size.height,
                                                     .headm = wordAt(frame, 0),
                                                     .headv = wordAt(frame, 4)},
                                                    frame);
    return rawSector(data, SectorForm::Form1, VideoSubmode);
}

Bytes audioSector(std::uint8_t channel = 1) {
    return rawSector(Bytes(dwe::formats::disc::Form2DataSize), SectorForm::Form2, AudioSubmode, StereoCoding, channel);
}

Bytes paddingSector() {
    return rawSector(Bytes(dwe::formats::disc::Form1DataSize), SectorForm::Form1, 0);
}

// A raw disc with MOVIE/A.STR, ten sectors long: frame 1 at sector 0 and frame 2 at sector 5 (30 frames a second at
// 150 sectors a second), an audio sector after each frame and padding in between.
struct MovieDisc {
    explicit MovieDisc(std::vector<Bytes> sectors) {
        fixtures::IsoBuilder builder;
        builder.addFile("MOVIE/A.STR", Bytes(MovieSectors * dwe::formats::disc::Form1DataSize));
        image = fixtures::toRawImage(builder.build());
        dwe::formats::disc::MemorySectorSource probe{image, dwe::formats::disc::SectorLayout::Raw};
        const auto read = dwe::formats::disc::readIsoVolume(probe);
        REQUIRE(read.has_value());
        const auto* file = &read->files.front();
        REQUIRE(file->path == "MOVIE/A.STR");
        for (std::size_t i = 0; i < sectors.size(); ++i) {
            std::ranges::copy(sectors[i], image.begin() + static_cast<std::ptrdiff_t>(
                                                              (file->lba + i) * dwe::formats::disc::RawSectorSize));
        }
        source = std::make_unique<dwe::formats::disc::MemorySectorSource>(image, dwe::formats::disc::SectorLayout::Raw);
        volume = *dwe::formats::disc::readIsoVolume(*source);
    }

    static std::vector<Bytes> goodSectors() {
        const Bytes frame = frameBitstream();
        return {videoSector(1, frame), audioSector(), paddingSector(), paddingSector(), paddingSector(),
                videoSector(2, frame), audioSector(), paddingSector(), paddingSector(), paddingSector()};
    }

    [[nodiscard]] Disc disc() const { return Disc{*source, volume, info}; }

    Bytes image;
    std::unique_ptr<dwe::formats::disc::MemorySectorSource> source;
    dwe::formats::disc::IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
    DiscInfo info{.volumeId = "FIXTURE", .executablePath = {}, .executableSha1 = {}, .version = {}};
};

const OutputFile& outputOf(const Extraction& extraction, std::string_view id) {
    const auto found =
        std::ranges::find_if(extraction.outputs, [id](const OutputFile& output) { return output.id.str() == id; });
    REQUIRE(found != extraction.outputs.end());
    return *found;
}

std::string textOf(std::span<const std::byte> bytes) {
    std::string text;
    for (const std::byte value : bytes) {
        text += static_cast<char>(value);
    }
    return text;
}

} // namespace

TEST_CASE("only the stream files of the MOVIE folder are movies") {
    CHECK(handlesMovie("MOVIE/OP1.STR"));
    CHECK_FALSE(handlesMovie("MOVIE/OP1.TIM"));
    CHECK_FALSE(handlesMovie("OTHER/OP1.STR"));
}

TEST_CASE("a movie becomes its video, its audio and its document") {
    const MovieDisc fixture{MovieDisc::goodSectors()};
    const Disc disc = fixture.disc();
    const auto extraction = extractMovie(disc, fixture.volume.files.front());
    REQUIRE_MESSAGE(extraction.has_value(), errorOf(extraction));
    CHECK(extraction->unknowns.empty());
    REQUIRE(extraction->outputs.size() == 3);

    const OutputFile& video = outputOf(*extraction, "movie/a/video");
    CHECK(video.extension == "y4m");
    REQUIRE(video.stream);
    std::string written;
    std::size_t parts = 0;
    const auto streamed = video.stream([&](std::span<const std::byte> part) -> dwe::core::Result<void> {
        written += textOf(part);
        ++parts;
        return {};
    });
    REQUIRE_MESSAGE(streamed.has_value(), errorOf(streamed));
    const std::string header = "YUV4MPEG2 W16 H16 F30:1 Ip A1:1 C420jpeg XCOLORRANGE=FULL\n";
    CHECK(parts == 3);
    REQUIRE(written.size() == header.size() + (std::size_t{2} * (6 + 256 + 128)));
    CHECK(written.starts_with(header));
    CHECK(written.substr(header.size(), 6) == "FRAME\n");
    // Every block is a DC of 0: mid grey.
    CHECK(std::ranges::all_of(std::string_view{written}.substr(header.size() + 6, 384),
                              [](char sample) { return static_cast<unsigned char>(sample) == 128; }));

    const OutputFile& audio = outputOf(*extraction, "movie/a/audio");
    CHECK(audio.extension == "wav");
    CHECK(audio.bytes.size() == 44 + (2 * 2016 * 4));

    const OutputFile& document = outputOf(*extraction, "movie/a/movie");
    const auto yaml = dwe::core::parseYaml(textOf(document.bytes));
    REQUIRE_MESSAGE(yaml.has_value(), errorOf(yaml));
    CHECK(yaml->find("video")->find("frames")->text() == "2");
    CHECK(yaml->find("audio")->find("sample_frames")->text() == "4032");
    CHECK(yaml->find("sectors")->find("padding")->text() == "6");
    CHECK(yaml->find("playback")->text() == "null");
    CHECK_FALSE(extraction->notes.empty());
}

TEST_CASE("the pack written from a movie opens with the engine's readers and follows its schemas") {
    const MovieDisc fixture{MovieDisc::goodSectors()};
    const auto run = runExtractors(fixture.disc(), extractors());
    REQUIRE(run.has_value());
    // The video is streamed to its file part by part; the engine opens what that gives.
    CHECK(dwe::dwx::testing::problemsOfWrittenPack(*run, fixture.info) == "");
}

TEST_CASE("frames that do not decode or end early are unknowns") {
    auto sectors = MovieDisc::goodSectors();
    sectors[0] = videoSector(1, frameBitstream(true));
    sectors[5] = videoSector(2, frameBitstream(), 2);
    const MovieDisc fixture{sectors};
    const Disc disc = fixture.disc();
    const auto extraction = extractMovie(disc, fixture.volume.files.front());
    REQUIRE_MESSAGE(extraction.has_value(), errorOf(extraction));
    REQUIRE(extraction->unknowns.size() == 3);
    CHECK(extraction->unknowns[0].find("frame 1") != std::string::npos);
    CHECK(extraction->unknowns[1].find("frame 2") != std::string::npos);
    CHECK(extraction->unknowns[2] == "the file has no frames");
    // No frame is left: only the audio and the document.
    CHECK(extraction->outputs.size() == 2);
}

TEST_CASE("frames whose size is not made of macroblocks are unknowns") {
    auto sectors = MovieDisc::goodSectors();
    // One macroblock's worth of bitstream: only the size tells these frames apart from a good one.
    sectors[0] = videoSector(1, frameBitstream(), 1, {.width = 24, .height = 16});
    sectors[3] = videoSector(3, frameBitstream(), 1, {.width = 0, .height = 0});
    sectors[5] = videoSector(2, frameBitstream());
    const MovieDisc fixture{sectors};
    const Disc disc = fixture.disc();
    const auto extraction = extractMovie(disc, fixture.volume.files.front());
    REQUIRE_MESSAGE(extraction.has_value(), errorOf(extraction));
    REQUIRE(extraction->unknowns.size() == 2);
    CHECK(extraction->unknowns[0].find("frame 1") != std::string::npos);
    CHECK(extraction->unknowns[0].find("24x16") != std::string::npos);
    CHECK(extraction->unknowns[1].find("frame 3") != std::string::npos);
    CHECK(extraction->unknowns[1].find("0x0") != std::string::npos);

    // The good frame after them is the movie's only one, and the video it gives writes to the end.
    const OutputFile& video = outputOf(*extraction, "movie/a/video");
    REQUIRE(video.stream);
    std::size_t size = 0;
    const auto streamed = video.stream([&](std::span<const std::byte> part) -> dwe::core::Result<void> {
        size += part.size();
        return {};
    });
    REQUIRE_MESSAGE(streamed.has_value(), errorOf(streamed));
    CHECK(size > 0);
    const auto yaml = dwe::core::parseYaml(textOf(outputOf(*extraction, "movie/a/movie").bytes));
    REQUIRE_MESSAGE(yaml.has_value(), errorOf(yaml));
    CHECK(yaml->find("video")->find("frames")->text() == "1");
    CHECK(yaml->find("video")->find("first_frame")->text() == "2");
    CHECK(yaml->find("video")->find("width")->text() == "16");
}

TEST_CASE("audio of another channel is an unknown and is left out of the movie's audio") {
    auto sectors = MovieDisc::goodSectors();
    sectors[6] = audioSector(2);
    const MovieDisc fixture{sectors};
    const Disc disc = fixture.disc();
    const auto extraction = extractMovie(disc, fixture.volume.files.front());
    REQUIRE_MESSAGE(extraction.has_value(), errorOf(extraction));
    REQUIRE(extraction->unknowns.size() == 1);
    CHECK(extraction->unknowns[0].find("audio sector 6") != std::string::npos);
    CHECK(extraction->unknowns[0].find("channel 2") != std::string::npos);
    // Only the first audio sector is decoded: 2016 stereo sample frames.
    CHECK(outputOf(*extraction, "movie/a/audio").bytes.size() == 44 + (2016 * 4));
    const auto yaml = dwe::core::parseYaml(textOf(outputOf(*extraction, "movie/a/movie").bytes));
    REQUIRE_MESSAGE(yaml.has_value(), errorOf(yaml));
    CHECK(yaml->find("audio")->find("sample_frames")->text() == "2016");
}

TEST_CASE("a movie needs a raw image") {
    fixtures::IsoBuilder builder;
    builder.addFile("MOVIE/A.STR", Bytes(MovieSectors * dwe::formats::disc::Form1DataSize));
    const Bytes image = builder.build();
    dwe::formats::disc::MemorySectorSource source{image, dwe::formats::disc::SectorLayout::Cooked};
    const auto volume = dwe::formats::disc::readIsoVolume(source);
    REQUIRE(volume.has_value());
    const DiscInfo info{.volumeId = "FIXTURE", .executablePath = {}, .executableSha1 = {}, .version = {}};
    const Disc disc{source, *volume, info};
    CHECK_FALSE(extractMovie(disc, volume->files.front()).has_value());
}
