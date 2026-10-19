#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/exe/tables.hpp>
#include <dwe/formats/image/tim.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "extractor.hpp"
#include "model_textures.hpp"

// The extractors of the original's 3D models. Each model becomes one binary glTF file (formats::model::writeModelGlb)
// with its textures inside.
namespace dwe::dwx {

// The Digimon models, CHDAT/MMD<n>/<name>.MMD: the skeleton of their type from the executable, every animation and
// the type's texture from CHDAT/ALLTIM.TIM.
bool handlesDigimonModel(std::string_view path);
core::Result<Extraction> extractDigimonModel(const Disc& disc, const formats::disc::IsoFile& file);

// Every other .TMD file: a model without a skeleton or animations, a node per object, textured with what the original
// has in VRAM when it draws it.
bool handlesStaticModel(std::string_view path);
core::Result<Extraction> extractStaticModel(const Disc& disc, const formats::disc::IsoFile& file);

// Returns the Digimon type of an MMD path: the type with that file name whose folder is MMD<type / 30>, as
// model.c: loadMMD builds it.
core::Result<std::size_t> digimonTypeOfModel(std::string_view path, std::span<const std::string> fileNames);

// A model ready for the pack, what the extractor could not take from it and what else the report should say.
struct BuiltModel {
    std::vector<std::byte> glb;
    std::vector<std::string> unknowns;
    std::vector<std::string> notes;
};

// Builds a Digimon's model from its MMD file, its texture TIM (uploaded where the TIM says) and its skeleton.
core::Result<BuiltModel> buildDigimonModel(std::span<const std::byte> bytes, const formats::image::Tim& texture,
                                           std::span<const formats::exe::SkeletonBone> bones);

// A place the original draws a model in: what it has in VRAM there, and a name for the report.
struct NamedScene {
    std::string name;
    VramScene scene;
};

// Builds a model without a skeleton from a TMD file: a node per object. It is textured from the first scene that
// holds all its texels, or the first scene if none does; scenes that draw it with other texels are reported. Without
// scenes its primitives keep their UVs and the model has no image; unknownTexture says why, for the report.
core::Result<BuiltModel> buildStaticModel(std::span<const std::byte> bytes, std::span<const NamedScene> scenes,
                                          std::string_view unknownTexture);

} // namespace dwe::dwx
