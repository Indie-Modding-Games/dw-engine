#pragma once

#include <dwe/core/math.hpp>
#include <dwe/rhi/types.hpp>
#include <dwe/ui/ui_renderer.hpp>

#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "rml_interfaces.hpp"
#include "ui_renderer_impl.hpp"

namespace dwe::ui {

// RmlUi's render interface. With a UiRenderer, it creates real textures and records each frame's
// draws for it; headless (tests, tools), it only answers what layout needs: texture sizes.
class RenderBridge final : public Rml::RenderInterface {
public:
  RenderBridge(TextureSources& sources, UiRenderer* renderer)
      : sources_{&sources}, gpu_{renderer == nullptr ? nullptr : &renderer->impl()} {}

  // Forgets the last frame's draws, before RmlUi renders the next one.
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

private:
  // A texture for premultiplied RGBA pixels: a renderer texture, or a plain handle when headless.
  Rml::TextureHandle createTexture(Rml::Vector2i dimensions, std::span<const std::byte> pixels);

  TextureSources* sources_;
  UiRenderer::Impl* gpu_;
  std::uintptr_t nextHandle_ = 1;
  std::map<Rml::CompiledGeometryHandle, std::shared_ptr<const UiGeometry>> geometries_;
  // The textures of the disc's pixel art (TextureSources::isPixelArt), drawn with sharp texel
  // edges.
  std::set<Rml::TextureHandle> sharpTextures_;
  bool scissorEnabled_ = false;
  rhi::Rect scissor_;
  glm::mat4 transform_{1.0F};
};

} // namespace dwe::ui
