#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/model/animation.hpp>
#include <dwe/formats/model/skeleton.hpp>
#include <dwe/formats/model/tmd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dwe::formats::model {

// The texture a model's primitives point at, as the extractor converted it: where its image and palettes sit in the
// console's VRAM (which is how the primitives address it) and the image as a PNG drawn with each palette row.
struct ModelTexture {
    std::uint16_t pixelX = 0; // x of the image in VRAM, in halfwords
    std::uint16_t pixelY = 0; // y of the image in VRAM
    std::uint16_t clutX = 0;  // x of the palettes in VRAM, in halfwords
    std::uint16_t clutY = 0;  // y of the first palette row; each row below is the next palette
    std::uint16_t width = 0;  // size of the image in texels
    std::uint16_t height = 0;
    // The PNG of each palette row, from clutY down. Empty: the UVs are written but no image is embedded.
    std::vector<std::span<const std::byte>> pngPerPaletteRow;
};

// Writes a model as a single binary glTF 2.0 file (.glb): a node per bone, one mesh skinned rigidly to the bones
// (each vertex has weight 1 on the bone that draws its object), and an animation per simulated animation, sampled at
// every tick with linear interpolation. The axes are turned from the console's Y down to glTF's Y up (180 degrees
// about X) and the units stay the original's. The same input always gives the same bytes. animations may be empty;
// the first one gives the bind pose.
core::Result<std::vector<std::byte>> writeModelGlb(const Tmd& tmd, std::span<const SkeletonBone> skeleton,
                                                   std::span<const Animation> animations,
                                                   const std::optional<ModelTexture>& texture);

} // namespace dwe::formats::model
