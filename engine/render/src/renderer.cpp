#include <dwe/core/log.hpp>
#include <dwe/render/camera.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/command_list.hpp>

#include <array>
#include <format>
#include <optional>
#include <utility>
#include <vector>

namespace dwe::render {

core::Result<std::unique_ptr<Renderer>> Renderer::create(rhi::Device& device, const RendererDesc& desc) {
    auto bindless = rhi::BindlessTable::create(device, desc.maxTextures);
    if (!bindless) {
        return std::unexpected{bindless.error()};
    }
    auto upload = rhi::UploadRing::create(device, desc.uploadBytesPerFrame);
    if (!upload) {
        return std::unexpected{upload.error()};
    }
    std::unique_ptr<Renderer> renderer{new Renderer{device, std::move(*bindless), std::move(*upload)}};
    if (renderer->colorFormat_ == rhi::Format::Undefined) {
        return core::fail("the swapchain's format is not one the renderer knows");
    }

    constexpr std::array<std::byte, 4> White{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    auto white = renderer->createTexture({.width = 1, .height = 1}, White);
    if (!white) {
        return std::unexpected{white.error()};
    }
    renderer->white_ = *white;
    core::logInfo("renderer: {} KiB of uploads per frame", renderer->upload_.bytesPerFrame() / 1024);
    return renderer;
}

Renderer::Renderer(rhi::Device& device, std::unique_ptr<rhi::BindlessTable> bindless, rhi::UploadRing upload)
    : device_(&device), bindless_(std::move(bindless)), upload_(std::move(upload)),
      colorFormat_(rhi::swapchainFormat(device)), depthFormat_(rhi::bestDepthFormat(device)) {}

// The images go to Device::destroyLater, like the table and its set: nothing here waits for the GPU.
Renderer::~Renderer() = default;

const rhi::Image* Renderer::depthTarget(rhi::Extent extent) {
    if (depth_ && depth_->extent() == extent) {
        return &*depth_;
    }
    // The old target may still be in use by a frame in flight: dropping it defers its destruction.
    depth_.reset();
    auto depth =
        rhi::Image::create(*device_, {.extent = extent, .format = depthFormat_, .usage = rhi::ImageUsage::DepthTarget});
    if (!depth) {
        core::logError("{}", depth.error().message);
        return nullptr;
    }
    depth_ = std::move(*depth);
    return &*depth_;
}

rhi::Image* Renderer::findTexture(TextureId texture) {
    if (!bindless_->contains(texture) || texture.index >= textures_.size()) {
        return nullptr;
    }
    std::optional<rhi::Image>& image = textures_[texture.index];
    return image ? &*image : nullptr;
}

const rhi::Image* Renderer::findTexture(TextureId texture) const {
    if (!bindless_->contains(texture) || texture.index >= textures_.size()) {
        return nullptr;
    }
    const std::optional<rhi::Image>& image = textures_[texture.index];
    return image ? &*image : nullptr;
}

bool Renderer::renderFrame(const FrameDesc& frame) {
    const auto begun = device_->beginFrame();
    if (!begun) {
        return false;
    }
    const rhi::Extent extent{.width = begun->extent.width, .height = begun->extent.height};
    extent_ = extent;
    upload_.beginFrame(begun->slot);

    rhi::CommandList commands{begun->commands, *bindless_};
    PrepareContext prepare{upload_, extent};
    for (const std::span<Drawer* const> drawers : {frame.scene, frame.ui}) {
        for (Drawer* drawer : drawers) {
            drawer->prepare(prepare);
        }
    }
    applyPendingWrites(commands);

    const rhi::Image* depth = depthTarget(extent);
    const rhi::Viewport viewport = fullViewport(extent);
    const rhi::Rect scissor{.x = 0, .y = 0, .width = extent.width, .height = extent.height};

    if (depth != nullptr) {
        commands.prepareDepthTarget(*depth);
    }
    commands.beginRendering({.extent = extent,
                             .color = {.view = begun->view, .clear = frame.clearColor},
                             .depth = {.view = depth != nullptr ? depth->view() : VK_NULL_HANDLE, .clear = 1.0F}});
    // Without a depth target the scene's pipelines cannot draw; the frame still clears and shows the UI.
    if (depth != nullptr) {
        DrawContext context{commands, upload_, Pass::Scene, extent};
        for (Drawer* drawer : frame.scene) {
            commands.setViewport(viewport);
            commands.setScissor(scissor);
            drawer->draw(context);
        }
    }
    commands.endRendering();

    commands.colorPassBarrier();
    commands.beginRendering({.extent = extent, .color = {.view = begun->view, .clear = std::nullopt}, .depth = {}});
    DrawContext context{commands, upload_, Pass::Ui, extent};
    for (Drawer* drawer : frame.ui) {
        commands.setViewport(viewport);
        commands.setScissor(scissor);
        drawer->draw(context);
    }
    if (frame.overlay) {
        frame.overlay(*begun);
    }
    commands.endRendering();

    upload_.endFrame();
    device_->endFrame(*begun);
    return true;
}

core::Result<rhi::GraphicsPipeline> Renderer::createPipeline(rhi::GraphicsPipelineDesc desc, Pass pass) {
    desc.colorFormat = colorFormat_;
    desc.depthFormat = pass == Pass::Scene ? depthFormat_ : rhi::Format::Undefined;
    return rhi::GraphicsPipeline::create(*device_, *bindless_, desc);
}

core::Result<TextureId> Renderer::createTexture(rhi::Extent extent, std::span<const std::byte> pixels) {
    auto image =
        pixels.empty()
            ? rhi::Image::create(
                  *device_, {.extent = extent, .format = rhi::Format::Rgba8Unorm, .usage = rhi::ImageUsage::Sampled})
            : rhi::Image::createTexture(*device_, extent, pixels);
    if (!image) {
        return std::unexpected{image.error()};
    }
    return addTexture(std::move(*image));
}

core::Result<TextureId> Renderer::queueTexture(rhi::Extent extent, std::span<const std::byte> pixels) {
    const rhi::Rect whole{.x = 0, .y = 0, .width = extent.width, .height = extent.height};
    if (!pixels.empty()) {
        if (auto valid = rhi::checkImageWrite(extent, rhi::Format::Rgba8Unorm, whole, pixels.size()); !valid) {
            return std::unexpected{valid.error()};
        }
        if (pixels.size() > upload_.bytesPerFrame()) {
            return core::fail(std::format("a {}x{} texture does not fit in a frame's {} bytes of uploads; create it "
                                          "at load time with createTexture",
                                          extent.width, extent.height, upload_.bytesPerFrame()));
        }
    }
    auto image = rhi::Image::create(
        *device_,
        {.extent = extent, .format = rhi::Format::Rgba8Unorm, .usage = rhi::ImageUsage::Sampled, .initialize = false});
    if (!image) {
        return std::unexpected{image.error()};
    }
    auto texture = addTexture(std::move(*image));
    if (texture) {
        pending_.push_back(
            {.texture = *texture, .region = whole, .pixels = {pixels.begin(), pixels.end()}, .initialize = true});
    }
    return texture;
}

core::Result<void> Renderer::queueTextureUpdate(TextureId texture, const rhi::Rect& region,
                                                std::span<const std::byte> pixels) {
    const rhi::Image* image = findTexture(texture);
    if (image == nullptr) {
        return core::fail(std::format("there is no texture {}", texture.index));
    }
    if (auto valid = rhi::checkImageWrite(image->extent(), image->format(), region, pixels.size()); !valid) {
        return valid;
    }
    if (pixels.size() > upload_.bytesPerFrame()) {
        return core::fail(std::format("a {}x{} update does not fit in a frame's {} bytes of uploads", region.width,
                                      region.height, upload_.bytesPerFrame()));
    }
    pending_.push_back(
        {.texture = texture, .region = region, .pixels = {pixels.begin(), pixels.end()}, .initialize = false});
    return {};
}

core::Result<TextureId> Renderer::addTexture(rhi::Image image) {
    auto texture = bindless_->add(image);
    if (!texture) {
        return std::unexpected{texture.error()};
    }
    if (texture->index >= textures_.size()) {
        textures_.resize(std::size_t{texture->index} + 1);
    }
    textures_[texture->index] = std::move(image);
    return *texture;
}

void Renderer::applyPendingWrites(rhi::CommandList& commands) {
    std::vector<PendingWrite> later;
    for (PendingWrite& write : pending_) {
        const rhi::Image* image = findTexture(write.texture);
        if (image == nullptr) {
            continue;
        }
        // New textures are cleared even when their pixels must wait: this frame may already draw them.
        if (write.initialize) {
            commands.initializeImage(*image);
            write.initialize = false;
        }
        if (write.pixels.empty()) {
            continue;
        }
        // Once one copy waits, the ones after it wait too, so updates of a texture keep their order.
        std::optional<rhi::UploadAllocation> staging;
        if (later.empty()) {
            staging = upload_.upload(std::span<const std::byte>{write.pixels}, 4);
        }
        if (!staging) {
            later.push_back(std::move(write));
            continue;
        }
        commands.writeImage(*image, write.region, *staging);
    }
    pending_ = std::move(later);
}

core::Result<void> Renderer::updateTexture(TextureId texture, const rhi::Rect& region,
                                           std::span<const std::byte> pixels) {
    rhi::Image* image = findTexture(texture);
    if (image == nullptr) {
        return core::fail(std::format("there is no texture {}", texture.index));
    }
    return image->write(region, pixels);
}

void Renderer::destroyTexture(TextureId texture) {
    if (findTexture(texture) == nullptr) {
        core::logWarning("cannot destroy texture {} (generation {}): there is no such texture", texture.index,
                         texture.generation);
        return;
    }
    std::erase_if(pending_, [texture](const PendingWrite& write) { return write.texture == texture; });
    bindless_->remove(texture);
    textures_[texture.index].reset();
}

std::optional<rhi::Extent> Renderer::textureExtent(TextureId texture) const {
    const rhi::Image* image = findTexture(texture);
    if (image == nullptr) {
        return std::nullopt;
    }
    return image->extent();
}

} // namespace dwe::render
