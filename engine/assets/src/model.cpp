#include <dwe/assets/model.hpp>

#include <cgltf.h>

#include <algorithm>
#include <format>
#include <memory>
#include <string_view>

namespace dwe::assets {

namespace {

struct CgltfDeleter {
  void operator()(cgltf_data* data) const { cgltf_free(data); }
};

std::string_view resultText(cgltf_result result) {
  switch (result) {
  case cgltf_result_data_too_short:
    return "the data is too short";
  case cgltf_result_unknown_format:
    return "neither glTF JSON nor GLB";
  case cgltf_result_invalid_json:
    return "invalid JSON";
  case cgltf_result_invalid_gltf:
    return "invalid glTF";
  case cgltf_result_out_of_memory:
    return "out of memory";
  case cgltf_result_legacy_gltf:
    return "glTF 1.0, which is not supported";
  default:
    return "cannot be read";
  }
}

void addExternalUri(ModelSummary& summary, const char* uri) {
  if (uri != nullptr && !std::string_view{uri}.starts_with("data:")) {
    summary.externalUris.emplace_back(uri);
  }
}

// The parent of each joint as an index into the skin's joints: the closest ancestor node that is a
// joint.
std::vector<std::int32_t> jointParents(const cgltf_skin& skin) {
  const std::span joints{skin.joints, skin.joints_count};
  std::vector<std::int32_t> parents;
  parents.reserve(joints.size());
  for (const cgltf_node* joint : joints) {
    std::int32_t parent = -1;
    for (const cgltf_node* node = joint->parent; node != nullptr && parent < 0;
         node = node->parent) {
      const auto found = std::ranges::find(joints, node);
      if (found != joints.end()) {
        parent = static_cast<std::int32_t>(found - joints.begin());
      }
    }
    parents.push_back(parent);
  }
  return parents;
}

} // namespace

core::Result<ModelSummary> readModel(std::span<const std::byte> bytes) {
  const cgltf_options options{};
  cgltf_data* parsed = nullptr;
  if (const cgltf_result result = cgltf_parse(&options, bytes.data(), bytes.size(), &parsed);
      result != cgltf_result_success) {
    return core::fail(std::format("not a glTF model: {}", resultText(result)));
  }
  const std::unique_ptr<cgltf_data, CgltfDeleter> data{parsed};
  if (const cgltf_result result = cgltf_validate(data.get()); result != cgltf_result_success) {
    return core::fail(std::format("the glTF model does not validate: {}", resultText(result)));
  }
  ModelSummary summary;
  for (const cgltf_buffer& buffer : std::span{data->buffers, data->buffers_count}) {
    addExternalUri(summary, buffer.uri);
  }
  for (const cgltf_image& image : std::span{data->images, data->images_count}) {
    addExternalUri(summary, image.uri);
  }
  for (const cgltf_skin& skin : std::span{data->skins, data->skins_count}) {
    summary.skins.push_back(jointParents(skin));
  }
  for (const cgltf_animation& animation : std::span{data->animations, data->animations_count}) {
    summary.animations.emplace_back(animation.name != nullptr ? animation.name : "");
  }
  return summary;
}

} // namespace dwe::assets
