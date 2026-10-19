#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "base_pack.hpp"
#include "disc_fixtures.hpp"
#include "padding_extractor.hpp"

using namespace dwe::dwx;

namespace {

// A disc with an all-zero MMM.DAT, or one with a few bytes set.
struct PaddingDisc {
    explicit PaddingDisc(std::vector<std::byte> padding) {
        fixtures::IsoBuilder builder;
        builder.addFile("MMM.DAT", std::move(padding));
        image = builder.build();
        source =
            std::make_unique<dwe::formats::disc::MemorySectorSource>(image, dwe::formats::disc::SectorLayout::Cooked);
        volume = *dwe::formats::disc::readIsoVolume(*source);
    }

    std::vector<std::byte> image;
    std::unique_ptr<dwe::formats::disc::MemorySectorSource> source;
    dwe::formats::disc::IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
    DiscInfo info{.volumeId = "FIXTURE", .executablePath = "", .executableSha1 = "", .version = ""};
};

const FileReport& runOn(const PaddingDisc& fixture, ExtractionRun& run) {
    const Disc disc{*fixture.source, fixture.volume, fixture.info};
    auto result = runExtractors(disc, extractors());
    REQUIRE(result.has_value());
    run = std::move(*result);
    REQUIRE(run.files.size() == 1);
    return run.files.front();
}

} // namespace

TEST_CASE("the runs of bytes that are not zero are found and described") {
    std::vector<std::byte> bytes(32);
    bytes[3] = std::byte{1};
    bytes[4] = std::byte{2};
    bytes[31] = std::byte{3};
    const auto ranges = nonZeroRanges(bytes);
    REQUIRE(ranges.size() == 2);
    CHECK(ranges[0].begin == 3);
    CHECK(ranges[0].end == 5);
    CHECK(ranges[1].begin == 31);
    CHECK(ranges[1].end == 32);
    CHECK(describeRanges(ranges) == "0x3-0x5, 0x1f-0x20");
    CHECK(nonZeroRanges(std::vector<std::byte>(16)).empty());

    std::vector<std::byte> many(40);
    for (std::size_t i = 0; i < many.size(); i += 4) {
        many[i] = std::byte{1};
    }
    CHECK(describeRanges(nonZeroRanges(many)).ends_with(" and 2 more"));
}

TEST_CASE("an all-zero filler file is extracted with a note and no output") {
    const PaddingDisc fixture{std::vector<std::byte>(5000)};
    ExtractionRun run;
    const auto& report = runOn(fixture, run);
    CHECK(report.extractor == "padding");
    CHECK(report.status == FileStatus::Extracted);
    CHECK(report.outputs.empty());
    REQUIRE(report.notes.size() == 1);
    CHECK(report.notes.front().starts_with("padding: all zero"));
}

TEST_CASE("a filler file with bytes that are not zero is partial and names them") {
    std::vector<std::byte> padding(5000);
    padding[2100] = std::byte{0x7f};
    const PaddingDisc fixture{std::move(padding)};
    ExtractionRun run;
    const auto& report = runOn(fixture, run);
    CHECK(report.status == FileStatus::Partial);
    REQUIRE(report.unknowns.size() == 1);
    CHECK(report.unknowns.front().contains("0x834-0x835"));
}
