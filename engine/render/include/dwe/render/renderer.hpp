#pragma once

#include <dwe/core/error.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/rhi/bindless.hpp>
#include <dwe/rhi/command_list.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/rhi/image.hpp>
#include <dwe/rhi/pipeline.hpp>
#include <dwe/rhi/types.hpp>
#include <dwe/rhi/upload_ring.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace dwe::render {

// A texture of the renderer: its index in the bindless table, which shaders take to sample it.
using TextureId = rhi::TextureId;

struct RendererDesc {
    // How many textures may exist at once (clamped to what the GPU supports).
    std::uint32_t maxTextures = 4096;
    // Bytes of per-frame data (joint matrices, UI vertices) each frame may upload.
    std::size_t uploadBytesPerFrame = std::size_t{16} << 20U;
};

// One frame: what each pass draws, in order. Drawers are borrowed for the call. Name every field (an empty span
// for a pass with nothing to draw): the build warns about missing ones.
struct FrameDesc {
    std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};
    // The 3D scene, with depth.
    std::span<Drawer* const> scene;
    // 2D on top of the scene, without depth: the game's UI.
    std::span<Drawer* const> ui;
    // Recorded last, still inside the UI pass, with the raw frame: the debug overlay (Dear ImGui). Nothing is
    // recorded after it in the frame, so it may change any state (pipelines, descriptor sets) it likes.
    std::function<void(const rhi::Frame&)> overlay = nullptr;
};

// The render foundation (docs/architecture/render.md): owns the bindless table, the upload ring, the depth target that
// follows the window and the textures, and runs each frame's passes (scene with depth → UI → debug overlay). The map,
// model and UI code plug in as Drawers and make their pipelines with createPipeline.
class Renderer {
public:
    // The device must outlive the renderer.
    static core::Result<std::unique_ptr<Renderer>> create(rhi::Device& device, const RendererDesc& desc = {});

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    ~Renderer();

    // Draws and presents one frame. Returns false when there was nothing to draw into (the window is minimized or
    // its swapchain was just rebuilt); the caller may then wait for events.
    bool renderFrame(const FrameDesc& frame);

    // A pipeline for one pass: desc's color and depth formats are filled in with that pass's targets.
    core::Result<rhi::GraphicsPipeline> createPipeline(rhi::GraphicsPipelineDesc desc, Pass pass);

    // Creates a texture from 8-bit RGBA pixels (extent.width * extent.height * 4 bytes, rows from the top), or a
    // transparent one when pixels is empty. Sampled with the bindless table's samplers. It waits for the GPU: for
    // load time. While the game runs, use queueTexture.
    core::Result<TextureId> createTexture(rhi::Extent extent, std::span<const std::byte> pixels = {});

    // Replaces a region of a texture (region.width * region.height * 4 bytes). Frames already submitted keep the
    // old pixels. It waits for the GPU: for load time. While the game runs, use queueTextureUpdate.
    core::Result<void> updateTexture(TextureId texture, const rhi::Rect& region, std::span<const std::byte> pixels);

    // Like createTexture, without waiting: the id is usable at once and the pixels (a copy is kept) are uploaded in
    // the next frame, before its passes. Called between frames or in Drawer::prepare, the texture can be drawn in
    // that next frame; called in Drawer::draw, only from the frame after. pixels may be at most
    // RendererDesc::uploadBytesPerFrame.
    core::Result<TextureId> queueTexture(rhi::Extent extent, std::span<const std::byte> pixels = {});

    // Like updateTexture, without waiting: the region is copied in the next frame, before its passes, with the
    // timing of queueTexture. Queued updates apply in the order they were queued; one that does not fit in that
    // frame's upload memory waits for the next frame, with the ones after it.
    core::Result<void> queueTextureUpdate(TextureId texture, const rhi::Rect& region,
                                          std::span<const std::byte> pixels);

    // Destroys a texture and drops its queued updates. Its id must not be drawn with afterwards; the index is reused
    // once the frames in flight finish. Ids that name no texture (destroyed already) are ignored with a warning.
    void destroyTexture(TextureId texture);

    // The size of a texture, or nothing when the id names none.
    [[nodiscard]] std::optional<rhi::Extent> textureExtent(TextureId texture) const;

    // A 1x1 opaque white texture, for untextured draws.
    [[nodiscard]] TextureId whiteTexture() const { return white_; }

    [[nodiscard]] rhi::Device& device() const { return *device_; }
    [[nodiscard]] const rhi::BindlessTable& bindless() const { return *bindless_; }
    [[nodiscard]] rhi::Format colorFormat() const { return colorFormat_; }
    [[nodiscard]] rhi::Format depthFormat() const { return depthFormat_; }
    // The size of the targets of the last frame drawn (the window's size in pixels).
    [[nodiscard]] rhi::Extent targetExtent() const { return extent_; }

private:
    Renderer(rhi::Device& device, std::unique_ptr<rhi::BindlessTable> bindless, rhi::UploadRing upload);

    // A texture upload waiting for the next frame.
    struct PendingWrite {
        TextureId texture;
        rhi::Rect region;
        std::vector<std::byte> pixels;
        // A texture made by queueTexture: clear it before anything samples it.
        bool initialize = false;
    };

    core::Result<TextureId> addTexture(rhi::Image image);
    // Records the queued uploads that fit in this frame's upload memory.
    void applyPendingWrites(rhi::CommandList& commands);

    // The depth target for this frame's size, remade when the window changed size; nothing if it cannot be made.
    const rhi::Image* depthTarget(rhi::Extent extent);
    rhi::Image* findTexture(TextureId texture);
    [[nodiscard]] const rhi::Image* findTexture(TextureId texture) const;

    rhi::Device* device_;
    std::unique_ptr<rhi::BindlessTable> bindless_;
    rhi::UploadRing upload_;
    rhi::Format colorFormat_ = rhi::Format::Undefined;
    rhi::Format depthFormat_ = rhi::Format::Undefined;
    std::optional<rhi::Image> depth_;
    rhi::Extent extent_;
    // Indexed by TextureId::index.
    std::vector<std::optional<rhi::Image>> textures_;
    TextureId white_;
    std::vector<PendingWrite> pending_;
};

} // namespace dwe::render
