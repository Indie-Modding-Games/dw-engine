#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include "upscale_recipe.hpp"

namespace dwe::dwx {

// What upscalePacks does with an image of the recipe that no pack provides.
enum class MissingImage : std::uint8_t {
  // It fails: dwx upscale runs a recipe by hand, on packs that should have every image.
  Fail,
  // It leaves the image out: dwx extract runs the bundled recipes on whatever the disc gave.
  Skip,
};

// What dwx upscale wrote.
struct UpscaleSummary {
  std::size_t images = 0;
  std::size_t metadata = 0;
  std::size_t skipped = 0;
};

// Grows the recipe's images, read from the packs in `packs` (a pack folder, or a folder of packs
// such as a content folder), and writes them with their metadata into a remaster pack at `out`
// named after the recipe. The images keep their asset ids, so the pack replaces them over the base;
// their _meta.yaml gains `scale`. A folder at `out` is only replaced when it is a pack this recipe
// wrote before. When no selected image is there to grow, nothing is written.
core::Result<UpscaleSummary> upscalePacks(const UpscaleRecipe& recipe,
                                          const std::filesystem::path& packs,
                                          const std::filesystem::path& out,
                                          MissingImage missing = MissingImage::Fail);

// The recipes dwx extract runs after it writes the base pack (content/enhance/, built into dwx),
// each writing its pack next to the base pack, named after the recipe. Prints what each wrote to
// `report`.
core::Result<void> runBundledRecipes(const std::filesystem::path& basePack, std::FILE* report);

// dwx upscale <recipe> <packs> -o <out>: reads the recipe, runs upscalePacks and prints what it
// wrote.
int upscale(const std::filesystem::path& recipe, const std::filesystem::path& packs,
            const std::filesystem::path& out);

} // namespace dwe::dwx
