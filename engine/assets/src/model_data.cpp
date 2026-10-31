#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>

#include <cgltf.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view PngMimeType = "image/png";
constexpr std::size_t MaxJoints = std::numeric_limits<std::uint16_t>::max();
constexpr std::uint8_t MaxSemiTransparency = 3;
// extras.loops[].count as dwx writes it: the original's byte, where 0xFF means forever.
constexpr std::uint32_t ExtrasLoopForever = 0xFF;
// How far the weights of a vertex may add up from 1 before the vertex is refused rather than
// normalised.
constexpr float WeightSumTolerance = 0.01F;

struct CgltfDeleter {
  void operator()(cgltf_data* data) const { cgltf_free(data); }
};
using CgltfData = std::unique_ptr<cgltf_data, CgltfDeleter>;

std::string_view resultText(cgltf_result result) {
  switch (result) {
  case cgltf_result_success:
    return "success";
  case cgltf_result_data_too_short:
    return "the data is too short";
  case cgltf_result_unknown_format:
    return "unknown format";
  case cgltf_result_invalid_json:
    return "invalid JSON";
  case cgltf_result_invalid_gltf:
    return "invalid glTF";
  case cgltf_result_invalid_options:
    return "invalid options";
  case cgltf_result_file_not_found:
    return "a buffer is in another file";
  case cgltf_result_io_error:
    return "I/O error";
  case cgltf_result_out_of_memory:
    return "out of memory";
  case cgltf_result_legacy_gltf:
    return "glTF 1.0 is not supported";
  case cgltf_result_max_enum:
    break;
  }
  return "unknown error";
}

template <typename T>
std::uint32_t indexIn(const T* item, const T* first) {
  return static_cast<std::uint32_t>(item - first);
}

// The extras of an object as a tree; glTF extras are JSON, which the YAML parser reads too. Empty
// when there are none.
core::Result<std::optional<YamlNode>> parseExtras(const cgltf_extras& extras,
                                                  std::string_view owner) {
  if (extras.data == nullptr) {
    return std::nullopt;
  }
  auto tree = core::parseYaml(extras.data);
  if (!tree) {
    return core::fail(
        std::format("{}: extras are not valid JSON: {}", owner, tree.error().message));
  }
  if (!tree->isMapping()) {
    return std::nullopt;
  }
  return std::optional<YamlNode>{std::move(*tree)};
}

template <typename T>
std::optional<T> parseNumber(std::string_view text) {
  T value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// Reads an optional whole number of an extras mapping in [0, max].
core::Result<std::optional<std::uint32_t>> extrasNumber(const YamlNode& extras,
                                                        std::string_view key, std::uint32_t max,
                                                        std::string_view owner) {
  const YamlNode* node = extras.find(key);
  if (node == nullptr || (node->isScalar() && node->text() == "null")) {
    return std::nullopt;
  }
  const auto value = node->isScalar() ? parseNumber<std::uint32_t>(node->text()) : std::nullopt;
  if (!value || *value > max) {
    return core::fail(
        std::format("{}: extras.{} must be a whole number from 0 to {}", owner, key, max));
  }
  return value;
}

// Reads an optional VRAM place of an extras mapping, an [x, y] pair.
core::Result<std::optional<VramPoint>> extrasVram(const YamlNode& extras, std::string_view key,
                                                  std::string_view owner) {
  const YamlNode* node = extras.find(key);
  if (node == nullptr || (node->isScalar() && node->text() == "null")) {
    return std::nullopt;
  }
  if (node->isSequence() && node->items().size() == 2) {
    const auto x = parseNumber<std::uint32_t>(node->items()[0].text());
    const auto y = parseNumber<std::uint32_t>(node->items()[1].text());
    if (x && y) {
      return VramPoint{.x = *x, .y = *y};
    }
  }
  return core::fail(std::format("{}: extras.{} must be an [x, y] place in VRAM", owner, key));
}

core::Result<bool> extrasFlag(const YamlNode& extras, std::string_view key,
                              std::string_view owner) {
  const YamlNode* node = extras.find(key);
  if (node == nullptr) {
    return false;
  }
  if (node->isScalar() && (node->text() == "true" || node->text() == "false")) {
    return node->text() == "true";
  }
  return core::fail(std::format("{}: extras.{} must be true or false", owner, key));
}

bool isFinite(glm::vec4 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}

// Whether the accessor's data can be read: it must lie in a buffer view (glTF lets an accessor
// without one stand for `count` zeros, which a hostile file could use to ask for any amount of
// memory) and not be sparse.
core::Result<void> checkReadable(const cgltf_accessor* accessor, std::string_view what) {
  const cgltf_size components = cgltf_num_components(accessor->type);
  if (components == 0 || components > 4 || accessor->is_sparse != 0) {
    return core::fail(std::format("{}: unsupported accessor layout", what));
  }
  if (accessor->buffer_view == nullptr || accessor->buffer_view->buffer->data == nullptr) {
    return core::fail(std::format("{}: the accessor has no buffer view", what));
  }
  return {};
}

// Reads every element of an accessor as floats, padded with `fill` up to four components. The
// caller has bounded accessor->count.
core::Result<std::vector<glm::vec4>> readFloats(const cgltf_accessor* accessor,
                                                std::string_view what,
                                                glm::vec4 fill = glm::vec4{0.0F}) {
  if (auto readable = checkReadable(accessor, what); !readable) {
    return std::unexpected{readable.error()};
  }
  const cgltf_size components = cgltf_num_components(accessor->type);
  std::vector<glm::vec4> values(accessor->count, fill);
  for (cgltf_size i = 0; i < accessor->count; ++i) {
    std::array<float, 4> read{fill.x, fill.y, fill.z, fill.w};
    if (cgltf_accessor_read_float(accessor, i, read.data(), components) == 0) {
      return core::fail(std::format("{}: cannot read element {}", what, i));
    }
    values[i] = glm::vec4{read[0], read[1], read[2], read[3]};
    if (!isFinite(values[i])) {
      return core::fail(std::format("{}: element {} is not a finite number", what, i));
    }
  }
  return values;
}

// Reads every element of an accessor as unsigned integers. The caller has bounded accessor->count.
core::Result<std::vector<std::uint32_t>> readUints(const cgltf_accessor* accessor,
                                                   std::string_view what) {
  if (auto readable = checkReadable(accessor, what); !readable) {
    return std::unexpected{readable.error()};
  }
  const cgltf_size components = cgltf_num_components(accessor->type);
  std::vector<std::uint32_t> values(accessor->count * components);
  for (cgltf_size i = 0; i < accessor->count; ++i) {
    if (cgltf_accessor_read_uint(accessor, i, values.data() + (i * components), components) == 0) {
      return core::fail(std::format("{}: cannot read element {}", what, i));
    }
  }
  return values;
}

class ModelReader {
public:
  explicit ModelReader(const cgltf_data& data) : data_(&data) {}

  core::Result<ModelData> read() {
    if (data_->nodes_count > MaxModelNodes) {
      return core::fail(std::format("{} nodes, more than {}", data_->nodes_count, MaxModelNodes));
    }
    if (data_->skins_count > 1) {
      return core::fail(std::format("{} skins: a model has at most one", data_->skins_count));
    }
    if (auto result = readNodes(); !result) {
      return std::unexpected{result.error()};
    }
    if (auto result = readImages(); !result) {
      return std::unexpected{result.error()};
    }
    if (auto result = readMaterials(); !result) {
      return std::unexpected{result.error()};
    }
    if (auto result = readSkin(); !result) {
      return std::unexpected{result.error()};
    }
    if (auto result = readMeshes(); !result) {
      return std::unexpected{result.error()};
    }
    if (auto result = readAnimations(); !result) {
      return std::unexpected{result.error()};
    }
    return std::move(model_);
  }

private:
  core::Result<void> readNodes() {
    model_.nodes.resize(data_->nodes_count);
    for (cgltf_size i = 0; i < data_->nodes_count; ++i) {
      const cgltf_node& node = data_->nodes[i];
      ModelNode& out = model_.nodes[i];
      out.name = node.name != nullptr ? node.name : "";
      if (node.parent != nullptr) {
        out.parent = indexIn(node.parent, data_->nodes);
      }
      if (node.has_matrix != 0) {
        return core::fail(
            std::format("node {} has a matrix: only translation, rotation and scale are "
                        "supported",
                        i));
      }
      const auto finite = [](const auto& values) {
        return std::ranges::all_of(values, [](float v) { return std::isfinite(v); });
      };
      if (!finite(node.translation) || !finite(node.rotation) || !finite(node.scale)) {
        return core::fail(std::format("node {} has a transform that is not a finite number", i));
      }
      if (node.has_translation != 0) {
        out.rest.translation = {node.translation[0], node.translation[1], node.translation[2]};
      }
      if (node.has_rotation != 0) {
        const glm::quat q{node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]};
        const float length = glm::length(q);
        if (!std::isfinite(length) || length < 0.5F) {
          return core::fail(std::format("node {} has a rotation that is not a unit quaternion", i));
        }
        out.rest.rotation = q / length;
      }
      if (node.has_scale != 0) {
        out.rest.scale = {node.scale[0], node.scale[1], node.scale[2]};
      }
    }
    // Parents first: walk down from the roots. cgltf_validate has already refused cycles.
    for (cgltf_size i = 0; i < data_->nodes_count; ++i) {
      if (data_->nodes[i].parent == nullptr) {
        model_.nodeOrder.push_back(static_cast<std::uint32_t>(i));
      }
    }
    for (std::size_t at = 0; at < model_.nodeOrder.size(); ++at) {
      const cgltf_node& node = data_->nodes[model_.nodeOrder[at]];
      for (cgltf_size c = 0; c < node.children_count; ++c) {
        model_.nodeOrder.push_back(indexIn(node.children[c], data_->nodes));
      }
    }
    if (model_.nodeOrder.size() != data_->nodes_count) {
      return core::fail("the node hierarchy is not a forest");
    }
    return {};
  }

  // The PNG bytes of image i, checked to be embedded.
  [[nodiscard]] core::Result<std::span<const std::byte>> imageBytes(cgltf_size i) const {
    const cgltf_image& image = data_->images[i];
    if (image.buffer_view == nullptr || image.buffer_view->buffer->data == nullptr) {
      return core::fail(std::format("image {} is not embedded in the model", i));
    }
    if (image.mime_type == nullptr || std::string_view{image.mime_type} != PngMimeType) {
      return core::fail(std::format("image {} is not a PNG", i));
    }
    const cgltf_buffer_view& view = *image.buffer_view;
    return std::span{static_cast<const std::byte*>(view.buffer->data) + view.offset, view.size};
  }

  core::Result<void> readImages() {
    // Every header first: the pixels of all the images together must fit the budget before any is
    // decoded.
    std::size_t pixels = 0;
    for (cgltf_size i = 0; i < data_->images_count; ++i) {
      const auto bytes = imageBytes(i);
      if (!bytes) {
        return std::unexpected{bytes.error()};
      }
      const auto header = inspectPng(*bytes);
      if (!header) {
        return core::fail(std::format("image {}: {}", i, header.error().message));
      }
      pixels += std::size_t{header->width} * header->height;
      if (pixels > MaxModelImagePixels) {
        return core::fail(std::format("image {}: more than {} pixels in the model's images", i,
                                      MaxModelImagePixels));
      }
    }
    for (cgltf_size i = 0; i < data_->images_count; ++i) {
      const auto bytes = imageBytes(i);
      if (!bytes) {
        return std::unexpected{bytes.error()};
      }
      auto decoded = decodePng(*bytes);
      if (!decoded) {
        return core::fail(std::format("image {}: {}", i, decoded.error().message));
      }
      model_.images.push_back(std::move(*decoded));
    }
    return {};
  }

  core::Result<void> readMaterials() {
    for (cgltf_size i = 0; i < data_->materials_count; ++i) {
      const cgltf_material& material = data_->materials[i];
      const std::string owner = std::format("material {}", i);
      ModelMaterial out;
      out.name = material.name != nullptr ? material.name : "";
      const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
      if (material.has_pbr_metallic_roughness != 0) {
        out.baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1],
                         pbr.base_color_factor[2], pbr.base_color_factor[3]};
        if (const cgltf_texture* texture = pbr.base_color_texture.texture; texture != nullptr) {
          if (texture->image == nullptr) {
            return core::fail(std::format("{}: its texture has no image", owner));
          }
          if (pbr.base_color_texture.texcoord != 0) {
            return core::fail(
                std::format("{}: only the first set of texture coordinates is supported", owner));
          }
          out.image = indexIn(texture->image, data_->images);
        }
      }
      switch (material.alpha_mode) {
      case cgltf_alpha_mode_opaque:
        out.alphaMode = AlphaMode::Opaque;
        break;
      case cgltf_alpha_mode_mask:
        out.alphaMode = AlphaMode::Mask;
        break;
      case cgltf_alpha_mode_blend:
        out.alphaMode = AlphaMode::Blend;
        break;
      case cgltf_alpha_mode_max_enum:
        return core::fail(std::format("{}: unknown alpha mode", owner));
      }
      out.alphaCutoff = material.alpha_cutoff;
      out.unlit = material.unlit != 0;
      out.doubleSided = material.double_sided != 0;
      auto extras = parseExtras(material.extras, owner);
      if (!extras) {
        return std::unexpected{extras.error()};
      }
      if (*extras) {
        auto blending = extrasNumber(**extras, "blending", MaxSemiTransparency, owner);
        if (!blending) {
          return std::unexpected{blending.error()};
        }
        out.blending = static_cast<SemiTransparency>(blending->value_or(0));
        auto texture = extrasVram(**extras, "image", owner);
        auto clut = extrasVram(**extras, "clut", owner);
        if (!texture || !clut) {
          return std::unexpected{!texture ? texture.error() : clut.error()};
        }
        out.texture = *texture;
        out.clut = *clut;
      }
      model_.materials.push_back(std::move(out));
    }
    return {};
  }

  core::Result<void> readSkin() {
    if (data_->skins_count == 0) {
      return {};
    }
    const cgltf_skin& skin = data_->skins[0];
    if (skin.joints_count == 0 || skin.joints_count > MaxJoints) {
      return core::fail(
          std::format("the skin has {} joints: it needs 1 to {}", skin.joints_count, MaxJoints));
    }
    ModelSkin out;
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
      out.joints.push_back(indexIn(skin.joints[j], data_->nodes));
    }
    out.inverseBindMatrices.assign(skin.joints_count, glm::mat4{1.0F});
    if (const cgltf_accessor* accessor = skin.inverse_bind_matrices; accessor != nullptr) {
      if (accessor->type != cgltf_type_mat4 || accessor->count != skin.joints_count) {
        return core::fail("the skin's inverse bind matrices must be one 4x4 matrix per joint");
      }
      if (accessor->is_sparse != 0 || accessor->buffer_view == nullptr ||
          accessor->buffer_view->buffer->data == nullptr) {
        return core::fail("the skin's inverse bind matrices have no buffer view");
      }
      for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        std::array<float, 16> m{};
        if (cgltf_accessor_read_float(accessor, j, m.data(), m.size()) == 0 ||
            !std::ranges::all_of(m, [](float v) { return std::isfinite(v); })) {
          return core::fail(std::format("the skin's inverse bind matrix {} cannot be read", j));
        }
        out.inverseBindMatrices[j] = glm::make_mat4(m.data());
      }
    }
    model_.skin = std::move(out);
    return {};
  }

  // Every mesh a node uses; a model with a skin draws only skinned meshes, a model without one
  // bakes each mesh's node transform into its vertices.
  core::Result<void> readMeshes() {
    std::vector<glm::mat4> world(data_->nodes_count, glm::mat4{1.0F});
    for (const std::uint32_t n : model_.nodeOrder) {
      const ModelNode& node = model_.nodes[n];
      world[n] = node.parent ? world[*node.parent] * node.rest.matrix() : node.rest.matrix();
    }
    for (cgltf_size n = 0; n < data_->nodes_count; ++n) {
      const cgltf_node& node = data_->nodes[n];
      if (node.mesh == nullptr) {
        continue;
      }
      if ((node.skin != nullptr) != model_.skin.has_value()) {
        return core::fail(
            std::format("node {}: a model with a skin may only have skinned meshes", n));
      }
      const glm::mat4 transform = model_.skin ? glm::mat4{1.0F} : world[n];
      for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
        const std::string owner = std::format("node {} primitive {}", n, p);
        auto primitive = readPrimitive(node.mesh->primitives[p], transform, owner);
        if (!primitive) {
          return std::unexpected{primitive.error()};
        }
        model_.primitives.push_back(std::move(*primitive));
      }
    }
    return {};
  }

  std::uint32_t defaultMaterial() {
    if (!defaultMaterial_) {
      defaultMaterial_ = static_cast<std::uint32_t>(model_.materials.size());
      ModelMaterial material;
      material.name = "default";
      model_.materials.push_back(std::move(material));
    }
    return *defaultMaterial_;
  }

  core::Result<ModelPrimitive> readPrimitive(const cgltf_primitive& primitive,
                                             const glm::mat4& transform, const std::string& owner) {
    if (primitive.type != cgltf_primitive_type_triangles) {
      return core::fail(std::format("{}: only triangle lists are supported", owner));
    }
    const cgltf_accessor* positions = nullptr;
    const cgltf_accessor* normals = nullptr;
    const cgltf_accessor* uvs = nullptr;
    const cgltf_accessor* colors = nullptr;
    const cgltf_accessor* joints = nullptr;
    const cgltf_accessor* weights = nullptr;
    for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
      const cgltf_attribute& attribute = primitive.attributes[a];
      if (attribute.index != 0) {
        continue;
      }
      switch (attribute.type) {
      case cgltf_attribute_type_position:
        positions = attribute.data;
        break;
      case cgltf_attribute_type_normal:
        normals = attribute.data;
        break;
      case cgltf_attribute_type_texcoord:
        uvs = attribute.data;
        break;
      case cgltf_attribute_type_color:
        colors = attribute.data;
        break;
      case cgltf_attribute_type_joints:
        joints = attribute.data;
        break;
      case cgltf_attribute_type_weights:
        weights = attribute.data;
        break;
      default:
        break;
      }
    }
    if (positions == nullptr || positions->type != cgltf_type_vec3) {
      return core::fail(std::format("{}: it has no POSITION", owner));
    }
    const cgltf_size count = positions->count;
    if (count > MaxModelVertices - vertexCount_) {
      return core::fail(
          std::format("{}: more than {} vertices in the model", owner, MaxModelVertices));
    }
    vertexCount_ += count;
    for (const cgltf_accessor* attribute : {normals, uvs, colors, joints, weights}) {
      if (attribute != nullptr && attribute->count != count) {
        return core::fail(std::format("{}: its attributes have different counts", owner));
      }
    }
    if (model_.skin && (joints == nullptr || weights == nullptr)) {
      return core::fail(std::format("{}: a skinned primitive needs JOINTS_0 and WEIGHTS_0", owner));
    }

    ModelPrimitive out{.vertices = std::vector<ModelVertex>(count),
                       .indices = {},
                       .material = 0,
                       .hasNormals = normals != nullptr,
                       .hasUvs = uvs != nullptr,
                       .hasColors = colors != nullptr};
    auto readPositions =
        readFloats(positions, owner + " POSITION", glm::vec4{0.0F, 0.0F, 0.0F, 1.0F});
    if (!readPositions) {
      return std::unexpected{readPositions.error()};
    }
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3{transform}));
    for (cgltf_size v = 0; v < count; ++v) {
      out.vertices[v].position = glm::vec3{transform * (*readPositions)[v]};
    }
    if (normals != nullptr) {
      if (normals->type != cgltf_type_vec3) {
        return core::fail(std::format("{}: NORMAL must be VEC3", owner));
      }
      auto values = readFloats(normals, owner + " NORMAL");
      if (!values) {
        return std::unexpected{values.error()};
      }
      for (cgltf_size v = 0; v < count; ++v) {
        const glm::vec3 normal = normalMatrix * glm::vec3{(*values)[v]};
        const float length = glm::length(normal);
        out.vertices[v].normal = length > 0.0F ? normal / length : glm::vec3{0.0F, 1.0F, 0.0F};
      }
    }
    if (uvs != nullptr) {
      if (uvs->type != cgltf_type_vec2) {
        return core::fail(std::format("{}: TEXCOORD_0 must be VEC2", owner));
      }
      auto values = readFloats(uvs, owner + " TEXCOORD_0");
      if (!values) {
        return std::unexpected{values.error()};
      }
      for (cgltf_size v = 0; v < count; ++v) {
        out.vertices[v].uv = glm::vec2{(*values)[v]};
      }
    }
    if (colors != nullptr) {
      if (colors->type != cgltf_type_vec3 && colors->type != cgltf_type_vec4) {
        return core::fail(std::format("{}: COLOR_0 must be VEC3 or VEC4", owner));
      }
      auto values = readFloats(colors, owner + " COLOR_0", glm::vec4{1.0F});
      if (!values) {
        return std::unexpected{values.error()};
      }
      for (cgltf_size v = 0; v < count; ++v) {
        out.vertices[v].color = (*values)[v];
      }
    }
    if (model_.skin) {
      const std::size_t jointCount = model_.skin->joints.size();
      if (auto result = readSkinning(*joints, *weights, jointCount, out, owner); !result) {
        return std::unexpected{result.error()};
      }
    }
    if (primitive.indices != nullptr) {
      if (primitive.indices->type != cgltf_type_scalar) {
        return core::fail(std::format("{}: the indices must be scalars", owner));
      }
      if (primitive.indices->count > MaxModelIndices - indexCount_) {
        return core::fail(
            std::format("{}: more than {} indices in the model", owner, MaxModelIndices));
      }
      auto indices = readUints(primitive.indices, owner + " indices");
      if (!indices) {
        return std::unexpected{indices.error()};
      }
      out.indices = std::move(*indices);
    } else {
      if (count > MaxModelIndices - indexCount_) {
        return core::fail(
            std::format("{}: more than {} indices in the model", owner, MaxModelIndices));
      }
      out.indices.resize(count);
      for (cgltf_size v = 0; v < count; ++v) {
        out.indices[v] = static_cast<std::uint32_t>(v);
      }
    }
    indexCount_ += out.indices.size();
    if (out.indices.size() % 3 != 0) {
      return core::fail(
          std::format("{}: {} indices do not make whole triangles", owner, out.indices.size()));
    }
    if (std::ranges::any_of(out.indices, [count](std::uint32_t i) { return i >= count; })) {
      return core::fail(std::format("{}: an index is past its {} vertices", owner, count));
    }
    out.material = primitive.material != nullptr ? indexIn(primitive.material, data_->materials)
                                                 : defaultMaterial();
    return out;
  }

  static core::Result<void> readSkinning(const cgltf_accessor& joints,
                                         const cgltf_accessor& weights, std::size_t jointCount,
                                         ModelPrimitive& out, const std::string& owner) {
    if (joints.type != cgltf_type_vec4 || weights.type != cgltf_type_vec4) {
      return core::fail(std::format("{}: JOINTS_0 and WEIGHTS_0 must be VEC4", owner));
    }
    auto jointValues = readUints(&joints, owner + " JOINTS_0");
    if (!jointValues) {
      return std::unexpected{jointValues.error()};
    }
    auto weightValues = readFloats(&weights, owner + " WEIGHTS_0");
    if (!weightValues) {
      return std::unexpected{weightValues.error()};
    }
    for (std::size_t v = 0; v < out.vertices.size(); ++v) {
      ModelVertex& vertex = out.vertices[v];
      const glm::vec4 w = (*weightValues)[v];
      const float sum = w.x + w.y + w.z + w.w;
      if (w.x < 0.0F || w.y < 0.0F || w.z < 0.0F || w.w < 0.0F ||
          std::abs(sum - 1.0F) > WeightSumTolerance) {
        return core::fail(std::format("{}: the weights of vertex {} do not add up to 1", owner, v));
      }
      vertex.weights = w / sum;
      for (std::size_t k = 0; k < 4; ++k) {
        const std::uint32_t joint = (*jointValues)[(v * 4) + k];
        if (joint >= jointCount) {
          return core::fail(
              std::format("{}: vertex {} uses joint {} of {}", owner, v, joint, jointCount));
        }
        vertex.joints[k] = static_cast<std::uint16_t>(joint);
      }
    }
    return {};
  }

  core::Result<void> readAnimations() {
    for (cgltf_size a = 0; a < data_->animations_count; ++a) {
      const cgltf_animation& animation = data_->animations[a];
      const std::string owner = std::format("animation {}", a);
      AnimationClip clip;
      clip.name = animation.name != nullptr ? animation.name : "";
      if (auto result = readClipExtras(animation, clip, owner); !result) {
        return std::unexpected{result.error()};
      }
      for (cgltf_size c = 0; c < animation.channels_count; ++c) {
        auto channel = readChannel(animation.channels[c], std::format("{} channel {}", owner, c));
        if (!channel) {
          return std::unexpected{channel.error()};
        }
        if (channel->morphWeights) {
          continue;
        }
        clip.duration = std::max(clip.duration, channel->channel.times.back());
        clip.channels.push_back(std::move(channel->channel));
      }
      if (auto result = setRepeat(clip, model_.nodes.size(), owner); !result) {
        return std::unexpected{result.error()};
      }
      model_.clips.push_back(std::move(clip));
    }
    return {};
  }

  static core::Result<void> readClipExtras(const cgltf_animation& animation, AnimationClip& clip,
                                           const std::string& owner) {
    auto extras = parseExtras(animation.extras, owner);
    if (!extras) {
      return std::unexpected{extras.error()};
    }
    if (!*extras) {
      return {};
    }
    const YamlNode& node = **extras;
    constexpr std::uint32_t MaxTicksPerSecond = 1000;
    constexpr auto MaxCount = std::numeric_limits<std::uint32_t>::max();
    const auto id = extrasNumber(node, "id", MaxCount, owner);
    const auto ticksPerSecond = extrasNumber(node, "ticksPerSecond", MaxTicksPerSecond, owner);
    const auto tickCount = extrasNumber(node, "tickCount", MaxCount, owner);
    const auto frameCount = extrasNumber(node, "frameCount", MaxCount, owner);
    const auto rootMotionNode = extrasNumber(node, "rootMotionNode", MaxCount, owner);
    const auto endsInLoop = extrasFlag(node, "endsInLoop", owner);
    for (const auto* result : {&id, &ticksPerSecond, &tickCount, &frameCount, &rootMotionNode}) {
      if (!*result) {
        return std::unexpected{result->error()};
      }
    }
    if (!endsInLoop) {
      return std::unexpected{endsInLoop.error()};
    }
    clip.id = *id;
    clip.ticksPerSecond = ticksPerSecond->value_or(0);
    clip.tickCount = tickCount->value_or(0);
    clip.frameCount = frameCount->value_or(0);
    clip.rootMotionNode = *rootMotionNode;
    clip.endsInLoop = *endsInLoop;
    if (const YamlNode* loops = node.find("loops"); loops != nullptr) {
      if (!loops->isSequence()) {
        return core::fail(std::format("{}: extras.loops must be a list", owner));
      }
      for (const YamlNode& loop : loops->items()) {
        if (!loop.isMapping()) {
          return core::fail(std::format("{}: every extras.loops entry must be an object", owner));
        }
        const auto first = extrasNumber(loop, "firstTick", MaxCount, owner);
        const auto last = extrasNumber(loop, "lastTick", MaxCount, owner);
        const auto count = extrasNumber(loop, "count", ExtrasLoopForever, owner);
        for (const auto* result : {&first, &last, &count}) {
          if (!*result) {
            return std::unexpected{result->error()};
          }
        }
        const std::optional<std::uint32_t> firstTick = *first;
        const std::optional<std::uint32_t> lastTick = *last;
        const std::optional<std::uint32_t> loopCount = *count;
        if (!firstTick || !lastTick || !loopCount) {
          return core::fail(std::format("{}: a loop needs firstTick, lastTick and count", owner));
        }
        clip.loops.push_back({.firstTick = *firstTick,
                              .lastTick = *lastTick,
                              .count = *loopCount == ExtrasLoopForever ? std::nullopt : loopCount});
      }
    }
    return readClipSounds(node, clip, owner);
  }

  static core::Result<void> readClipSounds(const YamlNode& node, AnimationClip& clip,
                                           const std::string& owner) {
    const YamlNode* sounds = node.find("sounds");
    if (sounds == nullptr) {
      return {};
    }
    if (!sounds->isSequence()) {
      return core::fail(std::format("{}: extras.sounds must be a list", owner));
    }
    constexpr std::uint32_t MaxByte = 0xFF;
    for (const YamlNode& sound : sounds->items()) {
      if (!sound.isMapping()) {
        return core::fail(std::format("{}: every extras.sounds entry must be an object", owner));
      }
      const auto tick =
          extrasNumber(sound, "tick", std::numeric_limits<std::uint32_t>::max(), owner);
      const auto bank = extrasNumber(sound, "bank", MaxByte, owner);
      const auto value = extrasNumber(sound, "sound", MaxByte, owner);
      for (const auto* result : {&tick, &bank, &value}) {
        if (!*result) {
          return std::unexpected{result->error()};
        }
      }
      if (!*tick || !*bank || !*value) {
        return core::fail(std::format("{}: a sound needs tick, bank and sound", owner));
      }
      if (clip.tickCount != 0 && **tick >= clip.tickCount) {
        return core::fail(std::format("{}: the sound at tick {} is outside its {} ticks", owner,
                                      **tick, clip.tickCount));
      }
      clip.sounds.push_back({.tick = **tick,
                             .bank = static_cast<std::uint8_t>(**bank),
                             .sound = static_cast<std::uint8_t>(**value)});
    }
    std::ranges::stable_sort(clip.sounds, {}, &ClipSound::tick);
    return {};
  }

  struct ReadChannel {
    AnimationChannel channel;
    // glTF also animates morph target weights; the models have no morph targets, so such channels
    // are dropped.
    bool morphWeights = false;
  };

  core::Result<ReadChannel> readChannel(const cgltf_animation_channel& channel,
                                        const std::string& owner) {
    ReadChannel out;
    if (channel.target_node == nullptr) {
      return core::fail(std::format("{}: it has no target node", owner));
    }
    out.channel.node = indexIn(channel.target_node, data_->nodes);
    std::size_t components = 3;
    switch (channel.target_path) {
    case cgltf_animation_path_type_translation:
      out.channel.path = ChannelPath::Translation;
      break;
    case cgltf_animation_path_type_rotation:
      out.channel.path = ChannelPath::Rotation;
      components = 4;
      break;
    case cgltf_animation_path_type_scale:
      out.channel.path = ChannelPath::Scale;
      break;
    case cgltf_animation_path_type_weights:
      out.morphWeights = true;
      return out;
    default:
      return core::fail(std::format("{}: unknown target path", owner));
    }
    const cgltf_animation_sampler& sampler = *channel.sampler;
    switch (sampler.interpolation) {
    case cgltf_interpolation_type_linear:
      out.channel.interpolation = Interpolation::Linear;
      break;
    case cgltf_interpolation_type_step:
      out.channel.interpolation = Interpolation::Step;
      break;
    default:
      return core::fail(std::format("{}: only LINEAR and STEP interpolation are supported", owner));
    }
    if (sampler.input->type != cgltf_type_scalar || sampler.input->count == 0 ||
        sampler.output->count != sampler.input->count ||
        cgltf_num_components(sampler.output->type) != components) {
      return core::fail(std::format("{}: its keyframe times and values do not match", owner));
    }
    if (sampler.input->count > MaxModelKeyframes - keyframes_) {
      return core::fail(
          std::format("{}: more than {} keyframes in the model", owner, MaxModelKeyframes));
    }
    keyframes_ += sampler.input->count;
    auto times = readFloats(sampler.input, owner + " times");
    if (!times) {
      return std::unexpected{times.error()};
    }
    float previous = -1.0F;
    for (const glm::vec4& time : *times) {
      if (time.x < 0.0F || time.x <= previous) {
        return core::fail(std::format("{}: its keyframe times must increase from 0", owner));
      }
      previous = time.x;
      out.channel.times.push_back(time.x);
    }
    auto values = readFloats(sampler.output, owner + " values");
    if (!values) {
      return std::unexpected{values.error()};
    }
    out.channel.values = std::move(*values);
    if (out.channel.path == ChannelPath::Rotation) {
      for (glm::vec4& q : out.channel.values) {
        const float length = glm::length(q);
        if (length < 0.5F) {
          return core::fail(std::format("{}: a rotation is not a unit quaternion", owner));
        }
        q /= length;
      }
    }
    return out;
  }

  static core::Result<void> setRepeat(AnimationClip& clip, std::size_t nodeCount,
                                      const std::string& owner) {
    if (clip.rootMotionNode && *clip.rootMotionNode >= nodeCount) {
      return core::fail(std::format("{}: extras.rootMotionNode is not a node", owner));
    }
    if (clip.ticksPerSecond == 0) {
      if (clip.endsInLoop || !clip.loops.empty()) {
        return core::fail(std::format("{}: loops in ticks need extras.ticksPerSecond", owner));
      }
      return {};
    }
    if (clip.tickCount == 0) {
      return core::fail(std::format("{}: extras.tickCount must not be 0", owner));
    }
    // A clip may hold the rest pose without any channel: its ticks still give its length.
    const auto tps = static_cast<float>(clip.ticksPerSecond);
    clip.duration = std::max(clip.duration, static_cast<float>(clip.tickCount - 1) / tps);
    for (const AnimationLoop& loop : clip.loops) {
      if (loop.firstTick > loop.lastTick || loop.lastTick >= clip.tickCount) {
        return core::fail(std::format("{}: the loop {}..{} is outside its {} ticks", owner,
                                      loop.firstTick, loop.lastTick, clip.tickCount));
      }
    }
    if (!clip.endsInLoop) {
      return {};
    }
    if (clip.loops.empty() || clip.loops.back().lastTick != clip.tickCount - 1) {
      return core::fail(std::format(
          "{}: it ends in a loop, but its last loop does not end at its last tick", owner));
    }
    const AnimationLoop& loop = clip.loops.back();
    clip.repeat = ClipRepeat{.start = static_cast<float>(loop.firstTick) / tps,
                             .end = static_cast<float>(loop.lastTick) / tps,
                             .gap = 1.0F / tps};
    return {};
  }

  const cgltf_data* data_;
  ModelData model_;
  std::optional<std::uint32_t> defaultMaterial_;
  std::size_t vertexCount_ = 0;
  std::size_t keyframes_ = 0;
  std::size_t indexCount_ = 0;
};

} // namespace

glm::mat4 NodeTransform::matrix() const {
  return glm::translate(glm::mat4{1.0F}, translation) * glm::mat4_cast(rotation) *
         glm::scale(glm::mat4{1.0F}, scale);
}

const AnimationClip* ModelData::findClip(std::uint32_t id) const {
  const auto found = std::ranges::find(clips, std::optional<std::uint32_t>{id}, &AnimationClip::id);
  return found != clips.end() ? &*found : nullptr;
}

const AnimationClip* ModelData::findClip(std::string_view name) const {
  const auto found = std::ranges::find(clips, name, &AnimationClip::name);
  return found != clips.end() ? &*found : nullptr;
}

core::Result<ModelData> parseModel(std::span<const std::byte> glb) {
  cgltf_options options{};
  options.type = cgltf_file_type_glb;
  cgltf_data* raw = nullptr;
  if (const cgltf_result result = cgltf_parse(&options, glb.data(), glb.size(), &raw);
      result != cgltf_result_success) {
    return core::fail(std::format("not a binary glTF: {}", resultText(result)));
  }
  const CgltfData data{raw};
  if (const cgltf_result result = cgltf_load_buffers(&options, data.get(), nullptr);
      result != cgltf_result_success) {
    return core::fail(std::format("cannot load the model's buffers: {}", resultText(result)));
  }
  if (const cgltf_result result = cgltf_validate(data.get()); result != cgltf_result_success) {
    return core::fail(std::format("the model is not valid glTF: {}", resultText(result)));
  }
  return ModelReader{*data}.read();
}

core::Result<ModelData> loadModel(const VirtualFileSystem& vfs, const AssetId& id) {
  const auto bytes = vfs.read(id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  auto model = parseModel(*bytes);
  if (!model) {
    return core::fail(std::format("model {}: {}", id.str(), model.error().message));
  }
  return model;
}

} // namespace dwe::assets
