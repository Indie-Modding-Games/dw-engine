// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one.

#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>

#include "base_pack.hpp"
#include "temp_folder.hpp"

using namespace dwe::dwx;

namespace {

// The message of a failed result. doctest builds a message even when the check passes, so it cannot call error().
template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("the user's disc: the images are extracted into a base pack the engine mounts") {
    const char* path = std::getenv("DWE_DISC");
    if (path == nullptr || std::string_view{path}.empty()) {
        MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
        return;
    }
    auto image = dwe::formats::disc::DiscImage::open(path);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));
    const auto volume = dwe::formats::disc::readIsoVolume(*image);
    REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));
    const DiscInfo info{.volumeId = volume->volumeId, .executablePath = "", .executableSha1 = "", .version = ""};
    const Disc disc{*image, *volume, info};

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
