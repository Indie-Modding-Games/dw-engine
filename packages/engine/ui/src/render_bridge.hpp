#pragma once

#include <dwe/core/math.hpp>
#include <dwe/render/upscale_cache.hpp>
#include <dwe/rhi/types.hpp>
#include <dwe/ui/ui_renderer.hpp>

#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "rml_interfaces.hpp"
#include "ui_renderer_impl.hpp"

namespace dwe::ui {

// RmlUi's render interface. With a UiRenderer, it creates real textures and records each frame's
// draws for it; headless (tests, tools), it only answers what layout needs: texture sizes.
class RenderBridge final : public Rml::RenderInterface {
public:
  RenderBridge(TextureSources& sources, UiRenderer* renderer)
      : sources_{&sources}, gpu_{renderer == nullptr ? nullptr : &renderer->impl()} {}

  // Forgets the last frame's draws, before RmlUi renders the next one, and swaps in the smoothed
  // pixels of the images whose smoothing finished (LoadTexture).
  void beginFrame();

  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                              Rml::Span<const int> indices) override;
  void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
  void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
  Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                     Rml::Vector2i dimensions) override;
  void ReleaseTexture(Rml::TextureHandle texture) override;
  void EnableScissorRegion(bool enable) override;
  void SetScissorRegion(Rml::Rectanglei region) override;
  void SetTransform(const Rml::Matrix4f* transform) override;

  // Whether a texture draws with sharp texel edges (rml.slang: sharpUv): only one LoadTexture read
  // from a source TextureSources::isPixelArt names. Generated textures (font atlases, minimaps)
  // never do.
  [[nodiscard]] bool drawsSharp(Rml::TextureHandle texture) const {
    return sharpTextures_.contains(texture);
  }

  // Where the images a source asks to smooth (TextureSources::isSmoothed) keep their upscaled
  // pixels, so each is upscaled once; none upscales them on every load.
  void setUpscaleCache(std::shared_ptr<const render::UpscaleCache> cache) {
    upscaleCache_ = std::move(cache);
  }

private:
  // An image smoothed on a thread of its own (smoothed in render_bridge.cpp), so the frame that
  // first draws it does not wait: the title's pictures take seconds to smooth the first time.
  struct Smoothing;
  struct SmoothingWork {
    std::shared_ptr<Smoothing> job;
    std::jthread thread;
  };

  // A texture for premultiplied RGBA pixels: a renderer texture, or a plain handle when headless.
  Rml::TextureHandle createTexture(Rml::Vector2i dimensions, std::span<const std::byte> pixels);
  // Smooths a texture's straight-alpha pixels on a thread; it draws as it is until they are ready.
  void startSmoothing(Rml::TextureHandle texture, std::uint32_t width, std::uint32_t height,
                      std::vector<std::byte> pixels, std::uint32_t doublings);
  // Makes a texture of each finished smoothing, drawn from then on instead of its image's.
  void finishSmoothings();
  // The renderer texture a handle draws: its smoothed one once there is one.
  [[nodiscard]] render::TextureId drawnTexture(Rml::TextureHandle texture) const;

  TextureSources* sources_;
  UiRenderer::Impl* gpu_;
  std::uintptr_t nextHandle_ = 1;
  std::map<Rml::CompiledGeometryHandle, std::shared_ptr<const UiGeometry>> geometries_;
  // The textures of the disc's pixel art (TextureSources::isPixelArt), drawn with sharp texel
  // edges.
  std::set<Rml::TextureHandle> sharpTextures_;
  std::shared_ptr<const render::UpscaleCache> upscaleCache_;
  // The smoothings under way, by the texture they smooth, and those whose texture was released
  // first (joined once finished, so none blocks a frame). One smooths at a time: the upscaler
  // already spreads each image over the processor's threads.
  std::map<Rml::TextureHandle, SmoothingWork> smoothings_;
  std::vector<SmoothingWork> abandonedSmoothings_;
  std::shared_ptr<std::mutex> smoothingTurn_ = std::make_shared<std::mutex>();
  // The smoothed textures, drawn instead of the handles' own.
  std::map<Rml::TextureHandle, render::TextureId> smoothedTextures_;
  bool scissorEnabled_ = false;
  rhi::Rect scissor_;
  glm::mat4 transform_{1.0F};
};

} // namespace dwe::ui
