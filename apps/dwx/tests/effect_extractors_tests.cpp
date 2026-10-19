#include <dwe/formats/effect/efe_file.hpp>

#include <cgltf.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "effect_extractors.hpp"
#include "effect_fixtures.hpp"

using namespace dwe::dwx;
namespace fx = dwe::formats::effect;
namespace fixtures = dwe::formats::effect::testing;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// A file of three slots: the sample, an empty one and the sample with another slot's effect id.
std::vector<std::byte> sampleFile() {
    std::vector<std::byte> file = fixtures::buildSlot(fixtures::sampleSlotParts());
    file.resize(fx::EfeSlotSize * 2, std::byte{0xCD});
    fixtures::SlotParts misnumbered = fixtures::sampleSlotParts();
    misnumbered.effectId = 0x100;
    const std::vector<std::byte> third = fixtures::buildSlot(misnumbered);
    file.insert(file.end(), third.begin(), third.end());
    return file;
}

std::vector<std::string> outputPaths(const Extraction& extraction) {
    std::vector<std::string> paths;
    paths.reserve(extraction.outputs.size());
    for (const OutputFile& output : extraction.outputs) {
        paths.push_back(outputPath(output));
    }
    return paths;
}

bool mentions(const std::vector<std::string>& texts, std::string_view part) {
    return std::ranges::any_of(texts, [&](const std::string& text) { return text.contains(part); });
}

} // namespace

TEST_CASE("the effect file is taken by its own extractor") {
    const Extractor* extractor = findExtractor(extractors(), "ETCHI/EFEDAT.EFE");
    REQUIRE(extractor != nullptr);
    CHECK(extractor->name == "effect");
    CHECK(findExtractor(extractors(), "ETCHI/OTHER.EFE") == nullptr);
}

TEST_CASE("every slot in use becomes its effect, script, texture and model") {
    const auto extraction = extractEffects(sampleFile(), nullptr, "ETCHI/EFEDAT.EFE");
    REQUIRE_MESSAGE(extraction.has_value(), errorOf(extraction));
    for (const std::string& unknown : extraction->unknowns) {
        MESSAGE(unknown);
    }
    CHECK(extraction->unknowns.empty());
    const auto paths = outputPaths(*extraction);
    for (const std::string& expected :
         {"effect/000/effect.yaml", "effect/000/script.yaml", "effect/000/texture.png", "effect/000/texture_meta.yaml",
          "effect/000/model.glb", "effect/002/effect.yaml", "effect/002/model.glb"}) {
        CHECK_MESSAGE(std::ranges::contains(paths, expected), expected);
    }
    CHECK_FALSE(std::ranges::any_of(paths, [](const std::string& path) { return path.starts_with("effect/001/"); }));
    CHECK(mentions(extraction->notes, "slots that are all fill, which no technique uses: 1"));
    CHECK(mentions(extraction->notes, "slot 2: its effect id is 0x100, not 0x102"));

    const auto model = std::ranges::find(paths, std::string{"effect/000/model.glb"});
    REQUIRE(model != paths.end());
    const auto& glb = extraction->outputs[static_cast<std::size_t>(model - paths.begin())].bytes;
    const cgltf_options options{};
    cgltf_data* data = nullptr;
    REQUIRE(cgltf_parse(&options, glb.data(), glb.size(), &data) == cgltf_result_success);
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> parsed(data, &cgltf_free);
    REQUIRE(cgltf_load_buffers(&options, data, nullptr) == cgltf_result_success);
    CHECK(cgltf_validate(data) == cgltf_result_success);
    CHECK(data->images_count == 1);
}

TEST_CASE("a slot that does not read is an unknown range, the rest still extract") {
    std::vector<std::byte> file = sampleFile();
    for (std::size_t i = 0; i < 4; ++i) {
        file[0x24 + i] = std::byte{0x7F};
    }
    const auto extraction = extractEffects(file, nullptr, "ETCHI/EFEDAT.EFE");
    REQUIRE_MESSAGE(extraction.has_value(), errorOf(extraction));
    REQUIRE(extraction->unknowns.size() == 1);
    CHECK(extraction->unknowns.front().starts_with("slot 0: 0x0-0x5000 not read"));
    CHECK(std::ranges::contains(outputPaths(*extraction), std::string{"effect/002/script.yaml"}));
    CHECK_FALSE(extractEffects(std::vector<std::byte>(100), nullptr, "ETCHI/EFEDAT.EFE").has_value());
}
