// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one.

#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <cgltf.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "disc_description.hpp"
#include "extractor.hpp"

using namespace dwe::dwx;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("the user's disc: every effect slot decodes and every byte of its data is typed, fill or unused") {
    const std::optional<std::filesystem::path> mountedPath = dwe::test_support::userDisc();
    if (!mountedPath) {
        MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
        return;
    }
    const std::filesystem::path& path = *mountedPath;
    auto image = dwe::formats::disc::DiscImage::open(path);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));
    const auto volume = dwe::formats::disc::readIsoVolume(*image);
    REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));
    const DiscDescription description = describeDisc(*image, *volume);
    const GameTables* tables = description.tables ? &*description.tables : nullptr;
    const Disc disc{*image, *volume, description.info, tables};
    const auto* file = disc.findFile("ETCHI/EFEDAT.EFE");
    REQUIRE(file != nullptr);
    const Extractor* extractor = findExtractor(extractors(), file->path);
    REQUIRE(extractor != nullptr);
    CHECK(extractor->name == "effect");
    const auto extraction = extractor->extract(disc, *file);
    REQUIRE_MESSAGE(extraction.has_value(), errorOf(extraction));

    std::size_t models = 0;
    std::size_t scripts = 0;
    for (const OutputFile& output : extraction->outputs) {
        scripts += output.id.str().ends_with("/script") ? 1 : 0;
        if (output.extension != "glb") {
            continue;
        }
        ++models;
        const cgltf_options options{};
        cgltf_data* data = nullptr;
        REQUIRE(cgltf_parse(&options, output.bytes.data(), output.bytes.size(), &data) == cgltf_result_success);
        const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> parsed(data, &cgltf_free);
        REQUIRE(cgltf_load_buffers(&options, data, nullptr) == cgltf_result_success);
        CHECK_MESSAGE(cgltf_validate(data) == cgltf_result_success, output.id.str());
        CHECK_MESSAGE(data->images_count > 0, output.id.str());
    }
    for (const std::string& unknown : extraction->unknowns) {
        MESSAGE(unknown);
    }
    CHECK(extraction->unknowns.empty());
    std::size_t unusedSlots = 0;
    for (const std::string& note : extraction->notes) {
        unusedSlots += note.contains("unused data, kept in script.yaml") ? 1 : 0;
    }
    MESSAGE(scripts, " scripts, ", models, " models, ", unusedSlots, " slots with unused data");
    CHECK(scripts == 121);
    CHECK(models == 89);
    // 40 ranges in 33 slots; slot 14's CLUT row spans 62 bytes of a range that was 92.
    CHECK(unusedSlots == 33);
    CHECK(std::ranges::any_of(extraction->notes, [](const std::string& note) {
        return note.starts_with("unused data: 40 ranges (590 bytes in 33 slots)");
    }));
}
