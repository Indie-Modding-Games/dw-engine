#include <dwe/core/log.hpp>
#include <dwe/render/model_renderer.hpp>
#include <dwe/shaders/skinned_model.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace dwe::render {

namespace {

using assets::AlphaMode;
using assets::ModelVertex;
using assets::SemiTransparency;

constexpr std::uint32_t FlagUnlit = 1;
constexpr std::uint32_t FlagMask = 2;
// Alpha 0 texels are the original's transparent ones: a blended material never draws them either.
constexpr float TranslucentCutoff = 0.5F;
constexpr float AverageAlpha = 0.5F;
constexpr float QuarterAlpha = 0.25F;

constexpr std::array ModelAttributes{
    rhi::VertexAttribute{.location = 0,
                         .format = rhi::VertexFormat::Float3,
                         .offset = static_cast<std::uint32_t>(offsetof(ModelVertex, position))},
    rhi::VertexAttribute{.location = 1,
                         .format = rhi::VertexFormat::Float3,
                         .offset = static_cast<std::uint32_t>(offsetof(ModelVertex, normal))},
    rhi::VertexAttribute{.location = 2,
                         .format = rhi::VertexFormat::Float2,
                         .offset = static_cast<std::uint32_t>(offsetof(ModelVertex, uv))},
    rhi::VertexAttribute{.location = 3,
                         .format = rhi::VertexFormat::Float4,
                         .offset = static_cast<std::uint32_t>(offsetof(ModelVertex, color))},
    rhi::VertexAttribute{.location = 4,
                         .format = rhi::VertexFormat::Uint16x4,
                         .offset = static_cast<std::uint32_t>(offsetof(ModelVertex, joints))},
    rhi::VertexAttribute{.location = 5,
                         .format = rhi::VertexFormat::Float4,
                         .offset = static_cast<std::uint32_t>(offsetof(ModelVertex, weights))},
};

// The C++ mirror of the shader's Frame (std430).
struct alignas(16) GpuLight {
  glm::vec4 direction;
  glm::vec4 color;
};
struct alignas(16) GpuFrame {
  glm::mat4 viewProjection;
  std::array<GpuLight, 3> lights;
  glm::vec4 ambient;
};

// The C++ mirror of the shader's Push.
struct ModelPush {
  std::uint64_t joints = 0;
  std::uint64_t frame = 0;
  glm::vec4 baseColor{1.0F};
  std::uint32_t texture = 0;
  std::uint32_t flags = 0;
  float alphaCutoff = 0.5F;
  float blendAlpha = 1.0F;
};
static_assert(offsetof(ModelPush, baseColor) == 16 && sizeof(ModelPush) == 48,
              "must match skinned_model.slang");

constexpr std::array TranslucentModes{rhi::BlendMode::Alpha, rhi::BlendMode::Additive,
                                      rhi::BlendMode::Subtractive};

std::size_t pipelineIndex(const MaterialBlend& blend, bool doubleSided) {
  const std::size_t cull = doubleSided ? 1 : 0;
  if (!blend.translucent) {
    return cull;
  }
  const auto mode = static_cast<std::size_t>(std::ranges::find(TranslucentModes, blend.mode) -
                                             TranslucentModes.begin());
  return 2 + (mode * 2) + cull;
}

// A light's direction, normalised; a zero direction (a light that is off) stays zero instead of
// becoming NaN.
glm::vec3 lightDirection(const glm::vec3& direction) {
  const float length = glm::length(direction);
  return length > 0.0F ? direction / length : glm::vec3{0.0F};
}

// The joint matrices of a model at rest: each joint's world transform in the rest pose times its
// inverse bind matrix, which is the identity only when the rest pose is the bind pose.
std::vector<glm::mat4> restJointMatrices(const assets::ModelData& model) {
  if (!model.skin) {
    return {glm::mat4{1.0F}};
  }
  std::vector<glm::mat4> world(model.nodes.size(), glm::mat4{1.0F});
  for (const std::uint32_t n : model.nodeOrder) {
    const auto& parent = model.nodes[n].parent;
    const glm::mat4 local = model.nodes[n].rest.matrix();
    world[n] = parent ? world[*parent] * local : local;
  }
  std::vector<glm::mat4> out(model.skin->joints.size());
  for (std::size_t j = 0; j < out.size(); ++j) {
    out[j] = world[model.skin->joints[j]] * model.skin->inverseBindMatrices[j];
  }
  return out;
}

} // namespace

ModelLighting defaultModelLighting() {
  ModelLighting lighting;
  lighting.ambient = glm::vec3{0.55F};
  lighting.lights[0] = {.direction = glm::normalize(glm::vec3{-0.4F, -1.0F, -0.6F}),
                        .color = glm::vec3{0.6F}};
  lighting.lights[1] = {.direction = glm::normalize(glm::vec3{0.8F, -0.2F, 0.5F}),
                        .color = glm::vec3{0.15F}};
  return lighting;
}

MaterialBlend materialBlend(const assets::ModelMaterial& material) {
  if (material.alphaMode != AlphaMode::Blend) {
    return {};
  }
  switch (material.blending) {
  case SemiTransparency::Average:
    return {.mode = rhi::BlendMode::Alpha, .alpha = AverageAlpha, .translucent = true};
  case SemiTransparency::Add:
    return {.mode = rhi::BlendMode::Additive, .alpha = 1.0F, .translucent = true};
  case SemiTransparency::Subtract:
    return {.mode = rhi::BlendMode::Subtractive, .alpha = 1.0F, .translucent = true};
  case SemiTransparency::AddQuarter:
    return {.mode = rhi::BlendMode::Additive, .alpha = QuarterAlpha, .translucent = true};
  }
  return {};
}

core::Result<std::unique_ptr<ModelRenderer>> ModelRenderer::create(Renderer& renderer) {
  std::unique_ptr<ModelRenderer> out{new ModelRenderer{renderer}};
  const auto make = [&](rhi::BlendMode blend, bool translucent,
                        bool doubleSided) -> core::Result<void> {
    auto pipeline = renderer.createPipeline(
        {.spirv = shaders::SkinnedModel,
         .vertex = {.stride = sizeof(ModelVertex), .attributes = ModelAttributes},
         .cull = doubleSided ? rhi::CullMode::None : rhi::CullMode::Back,
         .frontFace = rhi::FrontFace::CounterClockwise,
         .depth = {.test = true, .write = !translucent, .compare = rhi::CompareOp::LessOrEqual},
         .blend = blend,
         .name = "skinned model"},
        Pass::Scene);
    if (!pipeline) {
      return std::unexpected{pipeline.error()};
    }
    out->pipelines_.push_back(std::move(*pipeline));
    return {};
  };
  for (const bool doubleSided : {false, true}) {
    if (auto made = make(rhi::BlendMode::Opaque, false, doubleSided); !made) {
      return std::unexpected{made.error()};
    }
  }
  for (const rhi::BlendMode mode : TranslucentModes) {
    for (const bool doubleSided : {false, true}) {
      if (auto made = make(mode, true, doubleSided); !made) {
        return std::unexpected{made.error()};
      }
    }
  }
  return out;
}

ModelRenderer::~ModelRenderer() {
  models_.forEach([&](ModelHandle /*handle*/, const GpuModel& model) {
    for (const rhi::TextureId texture : model.textures) {
      renderer_->destroyTexture(texture);
    }
  });
}

core::Result<ModelHandle> ModelRenderer::add(const assets::ModelData& model) {
  std::size_t vertexCount = 0;
  std::size_t indexCount = 0;
  for (const assets::ModelPrimitive& primitive : model.primitives) {
    vertexCount += primitive.vertices.size();
    indexCount += primitive.indices.size();
  }
  if (vertexCount == 0 || indexCount == 0) {
    return core::fail("the model has nothing to draw");
  }
  // One buffer and one upload: the vertices, then the indices (4-byte aligned, as a vertex is a
  // multiple of 4).
  static_assert(sizeof(ModelVertex) % alignof(std::uint32_t) == 0);
  const std::size_t indexOffset = vertexCount * sizeof(ModelVertex);
  std::vector<std::byte> bytes(indexOffset + (indexCount * sizeof(std::uint32_t)));
  std::vector<Primitive> primitives;
  primitives.reserve(model.primitives.size());
  std::size_t vertexAt = 0;
  std::size_t indexAt = 0;
  for (const assets::ModelPrimitive& primitive : model.primitives) {
    primitives.push_back({.firstIndex = static_cast<std::uint32_t>(indexAt),
                          .indexCount = static_cast<std::uint32_t>(primitive.indices.size()),
                          .vertexOffset = static_cast<std::int32_t>(vertexAt),
                          .material = primitive.material});
    const std::span vertexBytes = std::as_bytes(std::span{primitive.vertices});
    std::ranges::copy(vertexBytes,
                      bytes.begin() + static_cast<std::ptrdiff_t>(vertexAt * sizeof(ModelVertex)));
    const std::span indexBytes = std::as_bytes(std::span{primitive.indices});
    std::ranges::copy(indexBytes,
                      bytes.begin() + static_cast<std::ptrdiff_t>(
                                          indexOffset + (indexAt * sizeof(std::uint32_t))));
    vertexAt += primitive.vertices.size();
    indexAt += primitive.indices.size();
  }
  auto geometry = rhi::Buffer::createWithData(
      renderer_->device(), rhi::BufferUsage::Vertex | rhi::BufferUsage::Index, bytes);
  if (!geometry) {
    return std::unexpected{geometry.error()};
  }
  GpuModel gpu{.geometry = std::move(*geometry),
               .indexOffset = indexOffset,
               .primitives = std::move(primitives),
               .materials = {},
               .textures = {},
               .restJoints = {},
               .hasTranslucent = false};
  for (const assets::Image& image : model.images) {
    auto texture =
        renderer_->queueTexture({.width = image.width, .height = image.height}, image.pixels);
    if (!texture) {
      for (const rhi::TextureId made : gpu.textures) {
        renderer_->destroyTexture(made);
      }
      return std::unexpected{texture.error()};
    }
    gpu.textures.push_back(*texture);
  }
  for (const assets::ModelMaterial& material : model.materials) {
    const MaterialBlend blend = materialBlend(material);
    std::uint32_t flags = material.unlit ? FlagUnlit : 0U;
    float cutoff = material.alphaCutoff;
    if (material.alphaMode == AlphaMode::Mask) {
      flags |= FlagMask;
    } else if (blend.translucent) {
      flags |= FlagMask;
      cutoff = TranslucentCutoff;
    }
    gpu.hasTranslucent = gpu.hasTranslucent || blend.translucent;
    gpu.materials.push_back(
        {.texture = material.image ? gpu.textures[*material.image] : renderer_->whiteTexture(),
         .flags = flags,
         .alphaCutoff = cutoff,
         .baseColor = material.baseColor,
         .blend = blend,
         .doubleSided = material.doubleSided});
  }
  gpu.restJoints = restJointMatrices(model);
  return models_.insert(std::move(gpu));
}

void ModelRenderer::remove(ModelHandle model) {
  const std::optional<GpuModel> gpu = models_.erase(model);
  if (!gpu) {
    return;
  }
  for (const rhi::TextureId texture : gpu->textures) {
    renderer_->destroyTexture(texture);
  }
}

std::size_t ModelRenderer::jointCount(ModelHandle model) const {
  const GpuModel* gpu = models_.find(model);
  return gpu != nullptr ? gpu->restJoints.size() : 0;
}

bool ModelRenderer::submit(const ModelInstance& instance) {
  const GpuModel* gpu = models_.find(instance.model);
  if (gpu == nullptr) {
    return false;
  }
  const std::size_t count = gpu->restJoints.size();
  const std::span<const glm::mat4> joints = instance.joints.size() == count
                                                ? instance.joints
                                                : std::span<const glm::mat4>{gpu->restJoints};
  const std::size_t first = joints_.size();
  for (const glm::mat4& joint : joints) {
    joints_.push_back(instance.world * joint);
  }
  submitted_.push_back(
      {.model = instance.model, .world = instance.world, .firstJoint = first, .jointCount = count});
  return true;
}

void ModelRenderer::clear() {
  submitted_.clear();
  joints_.clear();
}

const rhi::GraphicsPipeline& ModelRenderer::pipeline(const Material& material) const {
  return pipelines_[pipelineIndex(material.blend, material.doubleSided)];
}

void ModelRenderer::prepare(PrepareContext& context) {
  prepared_.clear();
  frameAddress_ = 0;
  const std::size_t submitted = submitted_.size();
  if (submitted == 0) {
    clear();
    return;
  }
  GpuFrame frame{.viewProjection = camera_.viewProjection(),
                 .lights = {},
                 .ambient = glm::vec4{lighting_.ambient, 0}};
  for (std::size_t i = 0; i < frame.lights.size(); ++i) {
    frame.lights[i] = {.direction = glm::vec4{lightDirection(lighting_.lights[i].direction), 0.0F},
                       .color = glm::vec4{lighting_.lights[i].color, 0.0F}};
  }
  rhi::UploadRing& ring = context.upload();
  const auto frameData = ring.upload(std::span<const GpuFrame>{&frame, 1});
  if (frameData) {
    frameAddress_ = frameData->address;
    // Each instance's joints in their own allocation: when the ring is full, the instances that fit
    // still draw.
    for (const Submitted& instance : submitted_) {
      const auto joints = ring.upload(
          std::span<const glm::mat4>{joints_}.subspan(instance.firstJoint, instance.jointCount));
      if (!joints) {
        break;
      }
      prepared_.push_back({.model = instance.model,
                           .viewDepth = (camera_.view * instance.world[3]).z,
                           .joints = joints->address});
    }
  }
  const std::size_t dropped = submitted - prepared_.size();
  if (dropped != droppedLast_) {
    if (dropped > 0) {
      core::logError(
          "models: {} of {} instances not drawn: the upload ring is full ({} of {} bytes used)",
          dropped, submitted, ring.used(), ring.bytesPerFrame());
    } else {
      core::logInfo("models: every instance fits in the upload ring again");
    }
    droppedLast_ = dropped;
  }
  clear();
}

void ModelRenderer::record(rhi::CommandList& commands, const GpuModel& model,
                           const Prepared& instance, const Primitive& primitive,
                           const rhi::GraphicsPipeline*& bound) const {
  const Material& material = model.materials[primitive.material];
  const rhi::GraphicsPipeline& wanted = pipeline(material);
  if (bound != &wanted) {
    commands.bindPipeline(wanted);
    bound = &wanted;
  }
  commands.bindVertexBuffer(model.geometry);
  commands.bindIndexBuffer(model.geometry, rhi::IndexType::Uint32, model.indexOffset);
  commands.pushConstants(ModelPush{.joints = instance.joints,
                                   .frame = frameAddress_,
                                   .baseColor = material.baseColor,
                                   .texture = material.texture.index,
                                   .flags = material.flags,
                                   .alphaCutoff = material.alphaCutoff,
                                   .blendAlpha = material.blend.alpha});
  commands.drawIndexed(primitive.indexCount, primitive.firstIndex, primitive.vertexOffset);
}

void ModelRenderer::drawOpaque(DrawContext& context) {
  if (context.pass() != Pass::Scene || prepared_.empty()) {
    return;
  }
  rhi::CommandList& commands = context.commands();
  if (camera_.viewport.width > 0.0F && camera_.viewport.height > 0.0F) {
    commands.setViewport(camera_.viewport);
  }
  const rhi::GraphicsPipeline* bound = nullptr;
  for (const Prepared& instance : prepared_) {
    // A model removed after its instance was submitted is skipped.
    const GpuModel* model = models_.find(instance.model);
    if (model == nullptr) {
      continue;
    }
    for (const Primitive& primitive : model->primitives) {
      if (!model->materials[primitive.material].blend.translucent) {
        record(commands, *model, instance, primitive, bound);
      }
    }
  }
}

void ModelRenderer::drawTranslucent(DrawContext& context) {
  if (context.pass() != Pass::Scene || prepared_.empty()) {
    return;
  }
  translucentScratch_.clear();
  for (const Prepared& instance : prepared_) {
    const GpuModel* model = models_.find(instance.model);
    if (model == nullptr || !model->hasTranslucent) {
      continue;
    }
    for (const Primitive& primitive : model->primitives) {
      if (model->materials[primitive.material].blend.translucent) {
        translucentScratch_.push_back({.viewDepth = instance.viewDepth,
                                       .model = model,
                                       .instance = &instance,
                                       .primitive = &primitive});
      }
    }
  }
  if (translucentScratch_.empty()) {
    return;
  }
  // View space looks down -Z: the farthest instance has the lowest z and draws first. Within an
  // instance the primitives keep their order.
  std::ranges::stable_sort(translucentScratch_, std::ranges::less{}, &Translucent::viewDepth);
  rhi::CommandList& commands = context.commands();
  if (camera_.viewport.width > 0.0F && camera_.viewport.height > 0.0F) {
    commands.setViewport(camera_.viewport);
  }
  const rhi::GraphicsPipeline* bound = nullptr;
  for (const Translucent& item : translucentScratch_) {
    record(commands, *item.model, *item.instance, *item.primitive, bound);
  }
}

} // namespace dwe::render
