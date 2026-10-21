// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one.

#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <cgltf.h>
#include <doctest/doctest.h>

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

TEST_CASE("the user's disc: every model becomes a GLB that cgltf validates") {
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
    // The model extractors read the executable's tables.
    const DiscDescription description = describeDisc(*image, *volume);
    const GameTables* tables = description.tables ? &*description.tables : nullptr;
    REQUIRE(tables != nullptr);
    const Disc disc{*image, *volume, description.info, tables};

    std::size_t digimon = 0;
    std::size_t others = 0;
    std::size_t variants = 0;
    std::size_t textured = 0;
    std::size_t partial = 0;
    std::size_t totalBytes = 0;
    for (const auto& file : volume->files) {
        const Extractor* extractor = findExtractor(extractors(), file.path);
        if (extractor == nullptr || (extractor->name != "digimon_model" && extractor->name != "static_model")) {
            continue;
        }
        const auto extraction = extractor->extract(disc, file);
        REQUIRE_MESSAGE(extraction.has_value(), file.path, ": ", errorOf(extraction));
        const bool isDigimon = extractor->name == "digimon_model";
        REQUIRE(!extraction->outputs.empty());
        // A Digimon's model comes with its contract and has no variants.
        CHECK((!isDigimon || extraction->outputs.size() == 2));
        if (isDigimon) {
            CHECK(extraction->outputs[1].id.str().ends_with("/contract"));
            CHECK(extraction->outputs[1].extension == "yaml");
        }
        (isDigimon ? digimon : others) += 1;
        variants += isDigimon ? 0 : extraction->outputs.size() - 1;
        partial += extraction->unknowns.empty() ? 0 : 1;
        for (const auto& output : extraction->outputs) {
            if (output.extension != "glb") {
                continue;
            }
            const auto& glb = output.bytes;
            totalBytes += glb.size();
            const cgltf_options options{};
            cgltf_data* data = nullptr;
            REQUIRE_MESSAGE(cgltf_parse(&options, glb.data(), glb.size(), &data) == cgltf_result_success, file.path);
            const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> parsed(data, &cgltf_free);
            REQUIRE(cgltf_load_buffers(&options, data, nullptr) == cgltf_result_success);
            CHECK_MESSAGE(cgltf_validate(data) == cgltf_result_success, file.path);
            textured += data->images_count > 0 ? 1 : 0;
            if (isDigimon) {
                CHECK_MESSAGE(data->images_count > 0, file.path);
                CHECK_MESSAGE(data->animations_count > 0, file.path);
            }
        }
    }
    MESSAGE(digimon, " Digimon models, ", others, " other models with ", variants, " variants, ", textured,
            " textured GLB, ", partial, " files with unknowns, ", totalBytes, " bytes");
    CHECK(digimon == 178);
    CHECK(others == 66);
}
