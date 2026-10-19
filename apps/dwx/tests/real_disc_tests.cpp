// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one.

#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>

#include "base_pack.hpp"
#include "disc_description.hpp"
#include "temp_folder.hpp"

using namespace dwe::dwx;

namespace {

// The message of a failed result. doctest builds a message even when the check passes, so it cannot call error().
template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("the user's disc: the tables and images are extracted into a base pack the engine mounts") {
    const char* path = std::getenv("DWE_DISC");
    if (path == nullptr || std::string_view{path}.empty()) {
        MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
        return;
    }
    auto image = dwe::formats::disc::DiscImage::open(path);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));
    const auto volume = dwe::formats::disc::readIsoVolume(*image);
    REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));
    const DiscDescription description = describeDisc(*image, *volume);
    REQUIRE(description.warnings.empty());
    REQUIRE(description.tables.has_value());
    const DiscInfo& info = description.info;
    const Disc disc{*image, *volume, info, description.tables ? &*description.tables : nullptr};

    const auto run = runExtractors(disc, extractors());
    REQUIRE_MESSAGE(run.has_value(), errorOf(run));
    const CoverageSummary summary = summarize(*run);
    CHECK(summary.files > 500);
    CHECK(summary.extracted + summary.partial >= 50);
    CHECK(summary.pending > 0);
    const auto taken = [&run](std::string_view extractor) {
        return std::ranges::count(run->files, extractor, &FileReport::extractor);
    };
    CHECK(taken("tim_pack") == 2);
    CHECK(taken("stacked_tim") == 2);
    CHECK(taken("tim_file") > 10);
    CHECK(taken("executable") == 1);
    CHECK(taken("boot_config") == 1);
    // Every map file but one has a MAP_ENTRIES record; the one without stays partial.
    CHECK(taken("map") > 200);
    CHECK(taken("map_background") == taken("map"));
    CHECK(taken("arena_collision") == 2);
    // The seven sound files, every bank and sequence understood.
    CHECK(taken("sound_bank") == 7);
    for (const auto& report : run->files) {
        if (report.extractor == "sound_bank") {
            CHECK_MESSAGE(report.status == FileStatus::Extracted, report.file.path);
        }
    }
    const auto statusOf = [&run](std::string_view path) {
        const auto found =
            std::ranges::find(run->files, path, [](const FileReport& report) { return report.file.path; });
        REQUIRE(found != run->files.end());
        return found->status;
    };
    CHECK(taken("padding") == 1);
    CHECK(statusOf("MMM.DAT") == FileStatus::Extracted);
    const auto has = [&run](std::string_view prefix) {
        return std::ranges::any_of(run->outputs,
                                   [prefix](const OutputFile& output) { return output.id.str().starts_with(prefix); });
    };
    CHECK(has("data/digimon"));
    CHECK(has("data/maps"));
    CHECK(has("map/"));
    CHECK(has("music/033/font"));
    CHECK(has("sound/vball/070/bank"));
    // With the tables, Digimon textures are named, never numbered.
    CHECK(has("digimon/"));
    CHECK_FALSE(has("digimon/0"));
    CHECK_FALSE(has("digimon/1"));
    // ALLTIM's 180 Digimon textures and CARD.ALL's 66 cards, with their metadata.
    CHECK(summary.outputs > (180 + 66) * 2);

    const dwe::assets::testing::TempFolder temp;
    REQUIRE(prepareOutputFolder(temp.path() / "base", false).has_value());
    const auto written = writeBasePack(temp.path() / "base", *run, info);
    REQUIRE_MESSAGE(written.has_value(), errorOf(written));
    const auto assets = checkBasePack(temp.path() / "base", *run);
    REQUIRE_MESSAGE(assets.has_value(), errorOf(assets));
    CHECK(*assets == summary.outputs + 1);
}
