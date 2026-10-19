#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/model/animation.hpp>
#include <dwe/formats/model/skeleton.hpp>
#include <dwe/formats/model/tmd.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dwe::formats::model {

// A palette of a model texture: where it sits in VRAM, which is how a primitive's CBA picks it, and the texture's
// image drawn with it, as a PNG.
struct TexturePalette {
    std::uint16_t clutX = 0; // in halfwords
    std::uint16_t clutY = 0;
    std::span<const std::byte> png;
};

// An image a model's primitives read, as the extractor converted it: where it sits in the console's VRAM, which is how
// the primitives address it (TSB page and UVs), and the palettes they draw it with.
struct ModelTexture {
    std::uint16_t pixelX = 0; // x of the image in VRAM, in halfwords
    std::uint16_t pixelY = 0; // y of the image in VRAM
    std::uint16_t width = 0;  // size of the image in texels
    std::uint16_t height = 0;
    std::uint8_t depth = 0; // the TSB colour depth of its texels: 0 = 4-bit, 1 = 8-bit, 2 = 16-bit
    // Empty: the UVs are written but no image is embedded. A 16-bit image has one entry, whatever its position.
    std::vector<TexturePalette> palettes;
};

// Writes a model as a single binary glTF 2.0 file (.glb): a node per bone, one mesh skinned rigidly to the bones
// (each vertex has weight 1 on the bone that draws its object), and an animation per simulated animation, sampled at
// every tick with linear interpolation. The axes are turned from the console's Y down to glTF's Y up (180 degrees
// about X) and the units stay the original's. The same input always gives the same bytes. animations may be empty;
// the first one gives the bind pose.
//
// A textured primitive takes the first texture of its colour depth that holds every texel its UVs reach and that has
// no palettes or the palette at its CBA; one that finds none fails the model. Without textures the UVs are left out.
core::Result<std::vector<std::byte>> writeModelGlb(const Tmd& tmd, std::span<const SkeletonBone> skeleton,
                                                   std::span<const Animation> animations,
                                                   std::span<const ModelTexture> textures);

} // namespace dwe::formats::model
