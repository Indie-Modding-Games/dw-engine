#include "render_bridge.hpp"

#include <dwe/core/log.hpp>
#include <dwe/render/scenery_upscale.hpp>
#include <dwe/ui/image_reference.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <span>
#include <stop_token>
#include <vector>

namespace dwe::ui {
namespace {

// RmlUi draws with premultiplied alpha; images from files have straight alpha. In place, through
// plain pointers: whole sprite sheets go through it, and a checked access per byte made that slow
// in debug builds. An opaque pixel stays as it is.
void premultiply(std::vector<std::byte>& pixels) {
  constexpr std::size_t Channels = 4;
  constexpr unsigned MaxValue = 255;
  std::byte* pixel = pixels.data();
  std::byte* const end = pixel + (pixels.size() / Channels * Channels);
  for (; pixel != end; pixel += Channels) {
    const auto alpha = std::to_integer<unsigned>(pixel[3]);
    if (alpha == MaxValue) {
      continue;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const auto value = std::to_integer<unsigned>(pixel[channel]);
      pixel[channel] = static_cast<std::byte>(((value * alpha) + (MaxValue / 2)) / MaxValue);
    }
  }
}

// The version of what smoothed returns: a change to it upscales every kept image again.
constexpr std::uint32_t SmoothImageVersion = 1;

// An image a source asks to smooth, doubled by the texture upscaler the field's models get
// (render::upscaleRgba: alpha interpolated like a colour, transparent texels taking their opaque
// neighbours' colour first) until it is SmoothUpscaleWidth wide. Straight alpha in and out.
std::vector<std::byte> smoothed(std::uint32_t width, std::uint32_t height,
                                std::vector<std::byte> pixels, std::uint32_t doublings,
                                const render::UpscaleCache* cache) {
  const std::size_t bytes = pixels.size() << (2U * doublings);
  render::UpscaleKey key;
  key.add(SmoothImageVersion).add(doublings).add(width).add(height);
  key.add(std::span<const std::byte>{pixels});
  if (cache != nullptr) {
    if (auto kept = cache->load(key.value(), bytes)) {
      return std::move(*kept);
    }
  }
  std::vector<std::byte> grown = render::upscaleRgba(width, height, pixels, doublings);
  if (cache != nullptr && grown.size() == bytes) {
    cache->store(key.value(), grown);
  }
  return grown;
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

struct RenderBridge::Smoothing {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // Premultiplied, once finished; empty when it was abandoned before it started.
  std::vector<std::byte> pixels;
  double milliseconds = 0.0;
  std::atomic<bool> finished{false};
};

void RenderBridge::beginFrame() {
  if (gpu_ != nullptr) {
    gpu_->draws.clear();
  }
  finishSmoothings();
}

void RenderBridge::startSmoothing(Rml::TextureHandle texture, std::uint32_t width,
                                  std::uint32_t height, std::vector<std::byte> pixels,
                                  std::uint32_t doublings) {
  auto job = std::make_shared<Smoothing>();
  job->width = width << doublings;
  job->height = height << doublings;
  std::jthread thread{[job, turn = smoothingTurn_, cache = upscaleCache_, width, height,
                       pixels = std::move(pixels), doublings](const std::stop_token& stop) mutable {
    const std::scoped_lock lock{*turn};
    if (!stop.stop_requested()) {
      const auto start = std::chrono::steady_clock::now();
      job->pixels = smoothed(width, height, std::move(pixels), doublings, cache.get());
      premultiply(job->pixels);
      job->milliseconds =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
    }
    job->finished.store(true, std::memory_order_release);
  }};
  smoothings_.insert_or_assign(texture,
                               SmoothingWork{.job = std::move(job), .thread = std::move(thread)});
}

void RenderBridge::finishSmoothings() {
  std::erase_if(abandonedSmoothings_, [](const SmoothingWork& work) {
    return work.job->finished.load(std::memory_order_acquire);
  });
  for (auto work = smoothings_.begin(); work != smoothings_.end();) {
    const Smoothing& job = *work->second.job;
    if (!job.finished.load(std::memory_order_acquire)) {
      ++work;
      continue;
    }
    const Rml::Vector2i texels{static_cast<int>(job.width), static_cast<int>(job.height)};
    if (const Rml::TextureHandle smooth = createTexture(texels, job.pixels); smooth != 0) {
      smoothedTextures_.insert_or_assign(work->first, textureOf(smooth));
      core::logDebug("ui: smoothed an image to {}x{} in {:.0f} ms", job.width, job.height,
                     job.milliseconds);
    }
    work = smoothings_.erase(work);
  }
}

render::TextureId RenderBridge::drawnTexture(Rml::TextureHandle texture) const {
  const auto smooth = smoothedTextures_.find(texture);
  return smooth != smoothedTextures_.end() ? smooth->second : textureOf(texture);
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
      .texture = texture == 0 ? gpu_->renderer->whiteTexture() : drawnTexture(texture),
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
  // Optional runtime images have no file fallback (for example when a species has no model, or a
  // test without a renderer): one the game has not bound draws as a transparent pixel, which RmlUi
  // takes as loaded instead of warning that it could not load it.
  if (source.starts_with("runtime/")) {
    dimensions = {1, 1};
    constexpr std::array<std::byte, 4> Transparent{};
    return createTexture(dimensions, Transparent);
  }
  auto image = sources_->load(source);
  if (!image) {
    core::logWarning("ui: {}", image.error().message);
    return 0;
  }
  // RmlUi lays out and cuts sprites in the drawn size; the texture keeps every pixel.
  dimensions = {static_cast<int>(image->size.width), static_cast<int>(image->size.height)};
  std::uint32_t width = image->pixels.width;
  std::uint32_t height = image->pixels.height;
  std::vector<std::byte> pixels = std::move(image->pixels.pixels);
  const std::uint32_t doublings = TextureSources::isSmoothed(source) ? smoothDoublings(width) : 0;
  // Headless, nothing draws: the smoothed size is all a test can see.
  if (doublings > 0 && gpu_ == nullptr) {
    pixels = smoothed(width, height, std::move(pixels), doublings, upscaleCache_.get());
    width <<= doublings;
    height <<= doublings;
  }
  // With a renderer the image draws filtered as it is until its smoothed pixels are ready.
  std::vector<std::byte> straight =
      doublings > 0 && gpu_ != nullptr ? pixels : std::vector<std::byte>{};
  const Rml::Vector2i texels{static_cast<int>(width), static_cast<int>(height)};
  premultiply(pixels);
  const Rml::TextureHandle texture = createTexture(texels, pixels);
  if (texture != 0 && TextureSources::isPixelArt(source)) {
    sharpTextures_.insert(texture);
  }
  if (texture != 0 && !straight.empty()) {
    startSmoothing(texture, width, height, std::move(straight), doublings);
  }
  return texture;
}

Rml::TextureHandle RenderBridge::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                 Rml::Vector2i dimensions) {
  return createTexture(dimensions, std::as_bytes(std::span{source.data(), source.size()}));
}

void RenderBridge::ReleaseTexture(Rml::TextureHandle texture) {
  sharpTextures_.erase(texture);
  if (const auto work = smoothings_.find(texture); work != smoothings_.end()) {
    work->second.thread.request_stop();
    abandonedSmoothings_.push_back(std::move(work->second));
    smoothings_.erase(work);
  }
  if (gpu_ != nullptr && texture != 0) {
    if (const auto smooth = smoothedTextures_.find(texture); smooth != smoothedTextures_.end()) {
      gpu_->renderer->destroyTexture(smooth->second);
      smoothedTextures_.erase(smooth);
    }
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
