#include <dwe/formats/exe/tables.hpp>
#include <dwe/formats/image/tim.hpp>
#include <dwe/formats/model/tmd.hpp>

#include <cgltf.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "model_extractors.hpp"
#include "model_fixtures.hpp"
#include "model_textures.hpp"
#include "tim_builder.hpp"

using namespace dwe::dwx;
using dwe::formats::image::Color15;
using dwe::formats::image::Tim;
using dwe::formats::image::TimPixelMode;
using dwe::formats::image::VramRect;
using dwe::formats::image::test::buildTim;
using dwe::formats::image::test::indexedTim;
namespace fixtures = dwe::formats::model::testing;

namespace {

// A 4-bit TIM of 256 x 128 texels at `image`, with `rows` palettes of 16 colours from `clut`. Colour 0 of each
// palette is the transparent 0x0000.
Tim texture(VramRect image, VramRect clut) {
    std::vector<Color15> colors;
    colors.reserve(std::size_t{clut.width} * clut.height);
    for (std::size_t i = 0; i < std::size_t{clut.width} * clut.height; ++i) {
        colors.push_back(i % 16 == 0 ? Color15{0} : static_cast<Color15>(0x0400 + i));
    }
    std::vector<std::byte> pixels(std::size_t{image.width} * 2 * image.height);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::byte>(i * 7);
    }
    auto tim = dwe::formats::image::readTim(
        buildTim(indexedTim(TimPixelMode::Indexed4, clut, std::move(colors), image, std::move(pixels))));
    REQUIRE(tim.has_value());
    return *tim;
}

// Where the sample model's primitives look: page (320, 0), palettes (0, 488) and (0, 489).
Tim sampleTexture() {
    return texture({.x = 320, .y = 0, .width = 64, .height = 128}, {.x = 0, .y = 488, .width = 16, .height = 2});
}

struct CgltfDeleter {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};
using ParsedGlb = std::unique_ptr<cgltf_data, CgltfDeleter>;

ParsedGlb parse(const std::vector<std::byte>& glb) {
    const cgltf_options options{};
    cgltf_data* data = nullptr;
    REQUIRE(cgltf_parse(&options, glb.data(), glb.size(), &data) == cgltf_result_success);
    ParsedGlb parsed(data);
    REQUIRE(cgltf_load_buffers(&options, data, nullptr) == cgltf_result_success);
    REQUIRE(cgltf_validate(data) == cgltf_result_success);
    return parsed;
}

bool hasUnknown(const BuiltModel& model, std::string_view text) {
    return std::ranges::any_of(model.unknowns, [text](const std::string& unknown) { return unknown.contains(text); });
}

} // namespace

TEST_CASE("Digimon model paths name their type through the folder and the file name") {
    std::vector<std::string> names(40, "NONE");
    names[3] = "AGUM";
    names[33] = "GREY";
    CHECK(handlesDigimonModel("CHDAT/MMD0/AGUM.MMD"));
    CHECK_FALSE(handlesDigimonModel("CHDAT/MMD0/AGUM.TMD"));
    CHECK_FALSE(handlesDigimonModel("CHDAT/MMDX/AGUM.MMD"));
    CHECK_FALSE(handlesDigimonModel("CHDAT/MMD0/SUB/AGUM.MMD"));
    CHECK_FALSE(handlesDigimonModel("CHDAT/ALLTIM.TIM"));

    const auto agum = digimonTypeOfModel("CHDAT/MMD0/AGUM.MMD", names);
    const auto grey = digimonTypeOfModel("CHDAT/MMD1/GREY.MMD", names);
    REQUIRE(agum.has_value());
    REQUIRE(grey.has_value());
    CHECK(*agum == 3);
    CHECK(*grey == 33);
    // The folder must be the one the original builds for the type.
    CHECK_FALSE(digimonTypeOfModel("CHDAT/MMD1/AGUM.MMD", names).has_value());
    CHECK_FALSE(digimonTypeOfModel("CHDAT/MMD0/WHAT.MMD", names).has_value());
}

TEST_CASE("a Digimon model gets its skeleton, its animations and an image per palette in use") {
    const auto tmd = fixtures::buildSampleTmd();
    const auto mmd = fixtures::buildMmd(
        tmd, {fixtures::buildSampleAnimationWithLoop(), {}, fixtures::buildSampleAnimationForever()});
    const std::vector<dwe::formats::exe::SkeletonBone> skeleton{{.objIndex = -1, .parentIndex = -1},
                                                                {.objIndex = -1, .parentIndex = 0},
                                                                {.objIndex = 0, .parentIndex = 1},
                                                                {.objIndex = 1, .parentIndex = 2}};
    const auto model = buildDigimonModel(mmd, sampleTexture(), skeleton);
    REQUIRE(model.has_value());
    // The sample's line primitive is the only thing left out.
    CHECK(model->unknowns.size() == 1);
    const auto parsed = parse(model->glb);
    CHECK(parsed->nodes_count == skeleton.size() + 1);
    CHECK(parsed->animations_count == 2);
    CHECK(parsed->images_count == 2);
    // The contract names the animations of the original's slots, skipping the empty one.
    CHECK(model->animations == std::vector<std::string>{"anim_00", "anim_02"});

    // A texture somewhere else leaves the primitives with their UVs and no image, and says so.
    const auto elsewhere = buildDigimonModel(
        mmd, texture({.x = 384, .y = 0, .width = 64, .height = 128}, {.x = 0, .y = 488, .width = 16, .height = 2}),
        skeleton);
    REQUIRE(elsewhere.has_value());
    CHECK(hasUnknown(*elsewhere, "no image holds their texels"));
    CHECK(parse(elsewhere->glb)->images_count == 0);

    // A skeleton that draws an object the model does not have is an error.
    auto wrongObject = skeleton;
    wrongObject[3].objIndex = 2;
    CHECK_FALSE(buildDigimonModel(mmd, sampleTexture(), wrongObject).has_value());
}

TEST_CASE("textures come from what was uploaded last, and palettes may come from anywhere") {
    const auto tmd = dwe::formats::model::readTmd(fixtures::buildSampleTmd());
    REQUIRE(tmd.has_value());

    VramScene scene;
    scene.addTim(sampleTexture());
    const auto first = textureModel(*tmd, scene);
    REQUIRE(first.has_value());
    CHECK(first->texturedPrimitives == 2);
    CHECK(first->resolvedPrimitives == 2);
    CHECK(first->unresolved.empty());
    REQUIRE(first->textures.size() == 1);
    CHECK(first->textures[0].palettes.size() == 2);

    // The same pixels uploaded again with other palettes, the way a map puts one CLUT row's palettes side by side:
    // the later upload wins, so the texels change.
    Tim again = sampleTexture();
    again.hasClut = false;
    scene.addPixels(again);
    scene.addPalette(0, 488, std::vector<Color15>(32, 0x7FFF));
    scene.addPalette(0, 489, std::vector<Color15>(16, 0x001F));
    const auto second = textureModel(*tmd, scene);
    REQUIRE(second.has_value());
    CHECK(second->resolvedPrimitives == 2);
    CHECK(second->texels != first->texels);
    const auto white = dwe::formats::image::toRgba8(0x7FFF);
    const auto red = dwe::formats::image::toRgba8(0x001F);
    CHECK(std::ranges::all_of(second->texels,
                              [&](const dwe::formats::image::Rgba8& color) { return color == white || color == red; }));

    // Without the palette at (0, 489) one primitive keeps only its UVs, on its page.
    VramScene partial;
    Tim onePalette =
        texture({.x = 320, .y = 0, .width = 64, .height = 128}, {.x = 0, .y = 488, .width = 16, .height = 1});
    partial.addTim(onePalette);
    const auto third = textureModel(*tmd, partial);
    REQUIRE(third.has_value());
    CHECK(third->resolvedPrimitives == 1);
    REQUIRE(third->unresolved.size() == 1);
    CHECK(third->unresolved[0].find("palette at (0, 489)") != std::string::npos);
    REQUIRE(third->textures.size() == 2);
    CHECK(third->textures[1].palettes.empty());
    CHECK(third->textures[1].width == 256);
}

TEST_CASE("a model without a skeleton has a node per object and a variant per scene that draws it differently") {
    const auto tmd = fixtures::buildSampleTmd();
    std::vector<NamedScene> scenes(3);
    scenes[0].name = "first";
    scenes[0].scene.addTim(
        texture({.x = 320, .y = 0, .width = 64, .height = 128}, {.x = 0, .y = 400, .width = 16, .height = 1}));
    scenes[1].name = "second";
    scenes[1].scene.addTim(sampleTexture());
    scenes[2].name = "again";
    scenes[2].scene.addTim(sampleTexture());
    // The semi-transparency bit on every opaque colour: the PNG does not keep it, so the model is drawn the same.
    Tim flagged = sampleTexture();
    for (Color15& color : flagged.clut) {
        if (color != 0) {
            color = static_cast<Color15>(color | 0x8000U);
        }
    }
    scenes.push_back({.name = "flagged", .slug = "flagged", .scene = {}});
    scenes.back().scene.addTim(flagged);
    Tim other = sampleTexture();
    other.clut.assign(other.clut.size(), 0x1234);
    NamedScene third{.name = "third", .slug = "third_place", .scene = {}};
    third.scene.addTim(other);
    scenes.push_back(std::move(third));

    const auto model = buildStaticModel(tmd, scenes);
    REQUIRE(model.has_value());
    // The first scene does not hold the palettes, so the second one textures the model; the third gives a variant.
    CHECK(model->unknowns.size() == 2); // the first scene and the sample's line primitive
    CHECK(hasUnknown(*model, "first does not hold all its texels"));
    CHECK(std::ranges::contains(
        model->notes, std::string{"textured with what second has in VRAM; the same texels in again, flagged"}));
    CHECK(
        std::ranges::contains(model->notes, std::string{"variant third_place: textured with what third has in VRAM"}));
    const auto parsed = parse(model->glb);
    CHECK(parsed->nodes_count == 2 + 1);
    CHECK(parsed->skins[0].skeleton == nullptr);
    CHECK(parsed->images_count == 2);
    REQUIRE(model->variants.size() == 1);
    CHECK(model->variants[0].name == "third_place");
    CHECK(parse(model->variants[0].glb)->images_count == 2);
    CHECK(model->variants[0].glb != model->glb);

    // With no scene that holds every texel, the first one textures what it can and the rest are reported.
    const auto partial = buildStaticModel(tmd, std::span(scenes).first(1));
    REQUIRE(partial.has_value());
    CHECK(hasUnknown(*partial, "first: 1 textured primitive"));
    CHECK(partial->variants.empty());

    const auto untextured = buildStaticModel(tmd, {});
    REQUIRE(untextured.has_value());
    CHECK(untextured->texturedPrimitives == 2);
    CHECK(parse(untextured->glb)->images_count == 0);

    CHECK_FALSE(buildStaticModel(std::vector<std::byte>(8), {}).has_value());
}
