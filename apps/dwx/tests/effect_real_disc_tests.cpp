// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one.

#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <cgltf.h>
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdlib>
#include <memory>
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

TEST_CASE("the user's disc: every effect slot decodes, its data is only unreferenced ranges short of whole") {
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
        CHECK_MESSAGE(unknown.ends_with("data that no instruction addresses"), unknown);
    }
    MESSAGE(scripts, " scripts, ", models, " models, ", extraction->unknowns.size(), " unreferenced ranges");
    CHECK(scripts == 121);
    CHECK(models == 89);
}
