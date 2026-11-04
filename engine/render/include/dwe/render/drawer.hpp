#pragma once

#include <dwe/rhi/bindless.hpp>
#include <dwe/rhi/command_list.hpp>
#include <dwe/rhi/types.hpp>
#include <dwe/rhi/upload_ring.hpp>

#include <cstdint>

namespace dwe::render {

// The passes of a frame, in the order they are drawn.
enum class Pass : std::uint8_t {
  // The 3D scene: the window's color target and a depth target, both cleared.
  Scene,
  // 2D on top (the game's UI, then the debug overlay): the same color target, no depth.
  Ui,
  // Between the two, only in frames with a post drawer (FrameDesc::post): the scene was drawn into
  // a texture (DrawContext::sceneColor) and this pass draws it into the window, no depth.
  Post,
};

// What a drawer records into, during one pass of one frame.
class DrawContext {
public:
  DrawContext(rhi::CommandList& commands, rhi::UploadRing& upload, Pass pass, rhi::Extent extent,
              rhi::TextureId sceneColor = {})
      : commands_(&commands), upload_(&upload), pass_(pass), extent_(extent),
        sceneColor_(sceneColor) {}

  [[nodiscard]] rhi::CommandList& commands() const { return *commands_; }
  // Per-frame memory for data that changes every frame; valid until the end of this frame on the
  // GPU.
  [[nodiscard]] rhi::UploadRing& upload() const { return *upload_; }
  [[nodiscard]] Pass pass() const { return pass_; }
  // The size of the targets in pixels. The viewport and scissor start as the whole target.
  [[nodiscard]] rhi::Extent extent() const { return extent_; }
  // In Pass::Post, the texture the scene pass drew this frame, the window's size; sampled with
  // the bindless table. In the other passes, an invalid id.
  [[nodiscard]] rhi::TextureId sceneColor() const { return sceneColor_; }

private:
  rhi::CommandList* commands_;
  rhi::UploadRing* upload_;
  Pass pass_;
  rhi::Extent extent_;
  rhi::TextureId sceneColor_;
};

// What a drawer may use before the passes of a frame start (Drawer::prepare).
class PrepareContext {
public:
  PrepareContext(rhi::UploadRing& upload, rhi::Extent extent) : upload_(&upload), extent_(extent) {}

  // Per-frame memory, as in DrawContext: what is written here may be drawn in this frame's passes.
  [[nodiscard]] rhi::UploadRing& upload() const { return *upload_; }
  // The size of this frame's targets in pixels.
  [[nodiscard]] rhi::Extent extent() const { return extent_; }

private:
  rhi::UploadRing* upload_;
  rhi::Extent extent_;
};

// Something that records draws into a pass: the map, the models, the UI. Drawers bind their own
// pipelines (made with Renderer::createPipeline for that pass) and set their own push constants.
class Drawer {
public:
  Drawer() = default;
  Drawer(const Drawer&) = default;
  Drawer& operator=(const Drawer&) = default;
  Drawer(Drawer&&) = default;
  Drawer& operator=(Drawer&&) = default;
  virtual ~Drawer() = default;

  // Called once per frame for each entry in FrameDesc::scene and FrameDesc::ui, before any pass
  // starts. Textures queued here (Renderer::queueTexture, queueTextureUpdate) are uploaded before
  // this frame's passes, so the draws of this frame already see them: a UI library that makes its
  // textures and geometry while it renders does that here and replays the draws in draw().
  virtual void prepare(PrepareContext& /*context*/) {}

  // Records the drawer's draws into one pass.
  virtual void draw(DrawContext& context) = 0;
};

} // namespace dwe::render
