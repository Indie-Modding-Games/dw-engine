#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "index_scale.hpp"

namespace dwe::dwx {

// The recipe format version dwx upscale reads.
inline constexpr std::int64_t UpscaleRecipeVersion = 1;

// A recipe for dwx upscale: which images of a pack to grow and by how much, with the built-in index_scale step only.
// It is the deterministic subset of the remaster recipe (docs/architecture/remaster-pipeline.md, «Recetas»): no
// providers, no models, no prompts, so it runs offline and gives the same bytes on every machine.
struct UpscaleRecipe {
    // The id of the recipe, which is also the id of the pack it writes.
    std::string id;
    // The indexed images it grows, by asset id without extension.
    std::vector<assets::AssetId> images;
    std::uint32_t scale = 1;
    IndexScaleMode mode = IndexScaleMode::Edge;
};

// Reads a recipe (<name>.recipe.yaml) and checks it: its version, an id usable as a pack id, at least one image, and
// one index_scale step whose mode reaches the recipe's scale.
core::Result<UpscaleRecipe> parseUpscaleRecipe(std::string_view yaml);

} // namespace dwe::dwx
