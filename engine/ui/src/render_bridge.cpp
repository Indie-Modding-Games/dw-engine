#include "render_bridge.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace dwe::ui {
namespace {

// RmlUi draws with premultiplied alpha; images from files have straight alpha.
std::vector<std::byte> premultiplied(std::span<const std::byte> straight) {
  constexpr std::size_t Channels = 4;
  constexpr unsigned MaxValue = 255;
  std::vector<std::byte> pixels(straight.begin(), straight.end());
  for (std::size_t pixel = 0; pixel + Channels <= pixels.size(); pixel += Channels) {
    const auto alpha = std::to_integer<unsigned>(pixels[pixel + 3]);
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const auto value = std::to_integer<unsigned>(pixels[pixel + channel]);
      pixels[pixel + channel] =
          static_cast<std::byte>(((value * alpha) + (MaxValue / 2)) / MaxValue);
    }
  }
  return pixels;
}

// Renderer textures are handed to RmlUi as their generation in the high 32 bits and their index
// plus one in the low ones, so 0 stays "no texture".
constexpr unsigned GenerationShift = 32;
constexpr std::uint64_t IndexMask = 0xFFFF'FFFFU;
static_assert(sizeof(Rml::TextureHandle) >= sizeof(std::uint64_t),
              "a texture handle must hold an index and a generation");

Rml::TextureHandle handleOf(render::TextureId texture) {
  return static_cast<Rml::TextureHandle>((std::uint64_t{texture.generation} << GenerationShift) |
                                         (std::uint64_t{texture.index} + 1));
}

render::TextureId textureOf(Rml::TextureHandle handle) {
  if (handle == 0) {
    return {};
  }
  const auto bits = static_cast<std::uint64_t>(handle);
  return {.index = static_cast<std::uint32_t>((bits & IndexMask) - 1),
          .generation = static_cast<std::uint32_t>(bits >> GenerationShift)};
}

} // namespace

void RenderBridge::beginFrame() {
  if (gpu_ != nullptr) {
    gpu_->draws.clear();
  }
}

Rml::CompiledGeometryHandle RenderBridge::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                          Rml::Span<const int> indices) {
  const Rml::CompiledGeometryHandle handle = nextHandle_++;
  if (gpu_ == nullptr || vertices.empty() || indices.empty()) {
    return handle;
  }
  rhi::Device& device = gpu_->renderer->device();
  auto vertexBuffer = rhi::Buffer::createMapped(
      device, rhi::BufferUsage::Vertex, std::as_bytes(std::span{vertices.data(), vertices.size()}));
  auto indexBuffer = rhi::Buffer::createMapped(
      device, rhi::BufferUsage::Index, std::as_bytes(std::span{indices.data(), indices.size()}));
  if (!vertexBuffer || !indexBuffer) {
    // That element is not drawn; the rest of the UI is.
    core::logWarning("ui: no memory for an element's geometry: {}",
                     (vertexBuffer ? indexBuffer.error() : vertexBuffer.error()).message);
    return handle;
  }
  geometries_.emplace(handle, std::make_shared<const UiGeometry>(UiGeometry{
                                  .vertices = std::move(*vertexBuffer),
                                  .indices = std::move(*indexBuffer),
                                  .indexCount = static_cast<std::uint32_t>(indices.size()),
                              }));
  return handle;
}

void RenderBridge::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                  Rml::TextureHandle texture) {
  if (gpu_ == nullptr) {
    return;
  }
  const auto found = geometries_.find(geometry);
  if (found == geometries_.end()) {
    return;
  }
  gpu_->draws.push_back(UiDraw{
      .geometry = found->second,
      .translation = {translation.x, translation.y},
      .texture = texture == 0 ? gpu_->renderer->whiteTexture() : textureOf(texture),
      .sharp = sharpTextures_.contains(texture),
      .scissor = scissorEnabled_ ? std::optional{scissor_} : std::nullopt,
      .transform = transform_,
  });
}

void RenderBridge::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
  geometries_.erase(geometry);
}

Rml::TextureHandle RenderBridge::createTexture(Rml::Vector2i dimensions,
                                               std::span<const std::byte> pixels) {
  if (gpu_ == nullptr) {
    return nextHandle_++;
  }
  const rhi::Extent extent{.width = static_cast<std::uint32_t>(dimensions.x),
                           .height = static_cast<std::uint32_t>(dimensions.y)};
  // Ui::update() runs between frames, so a queued texture can be drawn in the next one. One too big
  // for a frame's upload memory is made at once instead.
  auto texture = gpu_->renderer->queueTexture(extent, pixels);
  if (!texture) {
    texture = gpu_->renderer->createTexture(extent, pixels);
  }
  if (!texture) {
    core::logWarning("ui: {}", texture.error().message);
    return 0;
  }
  return handleOf(*texture);
}

Rml::TextureHandle RenderBridge::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
  if (gpu_ != nullptr) {
    const auto borrowed = gpu_->borrowedTextures.find(source);
    if (borrowed != gpu_->borrowedTextures.end()) {
      const auto extent = gpu_->renderer->textureExtent(borrowed->second);
      if (!extent)
        return 0;
      dimensions = {static_cast<int>(extent->width), static_cast<int>(extent->height)};
      return handleOf(borrowed->second);
    }
  }
  // Optional runtime images have no file fallback (for example when a species has no model).
  if (source.starts_with("runtime/"))
    return 0;
  auto image = sources_->load(source);
  if (!image) {
    core::logWarning("ui: {}", image.error().message);
    return 0;
  }
  dimensions = {static_cast<int>(image->width), static_cast<int>(image->height)};
  const std::vector<std::byte> pixels = premultiplied(std::span<const std::byte>{image->pixels});
  const Rml::TextureHandle texture = createTexture(dimensions, pixels);
  if (texture != 0 && TextureSources::isPixelArt(source)) {
    sharpTextures_.insert(texture);
  }
  return texture;
}

Rml::TextureHandle RenderBridge::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                 Rml::Vector2i dimensions) {
  return createTexture(dimensions, std::as_bytes(std::span{source.data(), source.size()}));
}

void RenderBridge::ReleaseTexture(Rml::TextureHandle texture) {
  sharpTextures_.erase(texture);
  if (gpu_ != nullptr && texture != 0) {
    if (std::ranges::any_of(gpu_->borrowedTextures, [texture](const auto& binding) {
          return handleOf(binding.second) == texture;
        }))
      return;
    gpu_->renderer->destroyTexture(textureOf(texture));
  }
}

void RenderBridge::EnableScissorRegion(bool enable) {
  scissorEnabled_ = enable;
}

void RenderBridge::SetScissorRegion(Rml::Rectanglei region) {
  scissor_ = {.x = region.Left(),
              .y = region.Top(),
              .width = static_cast<std::uint32_t>(std::max(region.Width(), 0)),
              .height = static_cast<std::uint32_t>(std::max(region.Height(), 0))};
}

void RenderBridge::SetTransform(const Rml::Matrix4f* transform) {
  if (transform == nullptr) {
    transform_ = glm::mat4{1.0F};
    return;
  }
  std::memcpy(&transform_, transform->data(), sizeof(transform_));
}

} // namespace dwe::ui
