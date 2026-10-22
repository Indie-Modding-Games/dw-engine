#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dwe::assets {

// What the tools and the checks need to know about a glTF model without keeping it.
struct ModelSummary {
  // The buffers and images in other files, as URIs relative to the model; embedded and data: ones
  // are left out.
  std::vector<std::string> externalUris;
  // Per skin, the parent of each joint as an index into the skin's joints; -1 for a joint without a
  // parent joint.
  std::vector<std::vector<std::int32_t>> skins;
  // The names of the animations, in order; an unnamed one is empty.
  std::vector<std::string> animations;
};

// Parses a glTF model, JSON (.gltf) or binary (.glb), and checks it with cgltf's validation.
core::Result<ModelSummary> readModel(std::span<const std::byte> bytes);

} // namespace dwe::assets
