#include <dwe/core/log.hpp>
#include <dwe/render/camera.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/render/screen_target.hpp>
#include <dwe/render/upload_budget.hpp>
#include <dwe/rhi/command_list.hpp>
#include <dwe/rhi/frame_capture.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <utility>
#include <vector>

namespace dwe::render {

namespace {

// The alignment of a texture copy's source in the upload memory: one RGBA8 pixel.
constexpr std::size_t PixelAlignment = 4;
// Samples per pixel of the multisampled scene (Renderer::setMultisampling): 4 smooths edges well
// at a cost every GPU takes (Vulkan requires 4 for colour and depth targets).
constexpr std::uint32_t MsaaSamples = 4;

// A texture's pixels kept until the upload: from plain pointers, a single block copy also in debug
// builds, where the checked iterators copied a sprite sheet byte by byte.
std::vector<std::byte> copied(std::span<const std::byte> pixels) {
  return {pixels.data(), pixels.data() + pixels.size()};
}

// A scene target's own depth, and its color, which the frame's later passes sample.
core::Result<rhi::Image> sceneTargetDepth(rhi::Device& device, rhi::Extent extent,
                                          rhi::Format format) {
  return rhi::Image::create(
      device, {.extent = extent, .format = format, .usage = rhi::ImageUsage::DepthTarget});
}

core::Result<rhi::Image> sceneTargetColor(rhi::Device& device, rhi::Extent extent,
                                          rhi::Format format) {
  return rhi::Image::create(device,
                            {.extent = extent,
                             .format = format,
                             .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::ColorTarget});
}

} // namespace

core::Result<std::unique_ptr<Renderer>> Renderer::create(rhi::Device& device,
                                                         const RendererDesc& desc) {
  auto bindless = rhi::BindlessTable::create(device, desc.maxTextures);
  if (!bindless) {
    return std::unexpected{bindless.error()};
  }
  auto upload = rhi::UploadRing::create(device, desc.uploadBytesPerFrame);
  if (!upload) {
    return std::unexpected{upload.error()};
  }
  std::unique_ptr<Renderer> renderer{
      new Renderer{device, std::move(*bindless), std::move(*upload)}};
  if (renderer->colorFormat_ == rhi::Format::Undefined) {
    return core::fail("the swapchain's format is not one the renderer knows");
  }

  constexpr std::array<std::byte, 4> White{std::byte{255}, std::byte{255}, std::byte{255},
                                           std::byte{255}};
  auto white = renderer->createTexture({.width = 1, .height = 1}, White);
  if (!white) {
    return std::unexpected{white.error()};
  }
  renderer->white_ = *white;
  core::logInfo("renderer: {} KiB of uploads per frame", renderer->upload_.bytesPerFrame() / 1024);
  return renderer;
}

Renderer::Renderer(rhi::Device& device, std::unique_ptr<rhi::BindlessTable> bindless,
                   rhi::UploadRing upload)
    : device_(&device), bindless_(std::move(bindless)), upload_(std::move(upload)),
      colorFormat_(rhi::swapchainFormat(device)), depthFormat_(rhi::bestDepthFormat(device)),
      msaaSamples_(rhi::maxSampleCount(device, MsaaSamples)) {}

// The images go to Device::destroyLater, like the table and its set: nothing here waits for the
// GPU.
Renderer::~Renderer() = default;

const rhi::Image* Renderer::depthTarget(rhi::Extent extent) {
  if (depth_ && depth_->extent() == extent) {
    return &*depth_;
  }
  // The old target may still be in use by a frame in flight: dropping it defers its destruction.
  depth_.reset();
  auto depth = rhi::Image::create(
      *device_, {.extent = extent, .format = depthFormat_, .usage = rhi::ImageUsage::DepthTarget});
  if (!depth) {
    core::logError("{}", depth.error().message);
    return nullptr;
  }
  depth_ = std::move(*depth);
  return &*depth_;
}

bool Renderer::multisampledTargets(rhi::Extent extent) {
  if (msaaColor_ && msaaDepth_ && msaaColor_->extent() == extent) {
    return true;
  }
  // The old targets may still be in use by a frame in flight: dropping them defers their
  // destruction.
  msaaColor_.reset();
  msaaDepth_.reset();
  auto color = rhi::Image::create(*device_, {.extent = extent,
                                             .format = colorFormat_,
                                             .usage = rhi::ImageUsage::ColorTarget,
                                             .samples = msaaSamples_});
  auto depth = rhi::Image::create(*device_, {.extent = extent,
                                             .format = depthFormat_,
                                             .usage = rhi::ImageUsage::DepthTarget,
                                             .samples = msaaSamples_});
  if (!color || !depth) {
    core::logError("renderer: cannot make the {}x{} multisampled targets ({}); drawing without "
                   "antialiasing",
                   extent.width, extent.height,
                   !color ? color.error().message : depth.error().message);
    multisampling_ = false;
    return false;
  }
  msaaColor_ = std::move(*color);
  msaaDepth_ = std::move(*depth);
  return true;
}

const rhi::Image* Renderer::sceneColorTarget(rhi::Extent extent) {
  if (sceneColor_) {
    const rhi::Image* image = findTexture(*sceneColor_);
    if (image != nullptr && image->extent() == extent) {
      return image;
    }
    // A frame in flight may still sample the old one: removing it defers its destruction.
    destroyTexture(*std::exchange(sceneColor_, std::nullopt));
  }
  auto color = rhi::Image::create(
      *device_, {.extent = extent,
                 .format = colorFormat_,
                 .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::ColorTarget});
  if (!color) {
    core::logError("{}", color.error().message);
    return nullptr;
  }
  auto texture = addTexture(std::move(*color));
  if (!texture) {
    core::logError("{}", texture.error().message);
    return nullptr;
  }
  sceneColor_ = *texture;
  return findTexture(*texture);
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
  fitScreenTargets(extent);

  rhi::CommandList commands{begun->commands, *bindless_};
  PrepareContext prepare{upload_, extent};
  for (const std::span<Drawer* const> drawers : {frame.scene, frame.ui}) {
    for (Drawer* drawer : drawers) {
      drawer->prepare(prepare);
    }
  }
  if (frame.post != nullptr) {
    frame.post->prepare(prepare);
  }
  for (const TextureScene& scene : frame.textureScenes) {
    const auto target = std::ranges::find(sceneTargets_, scene.target, &SceneTarget::color);
    const rhi::Image* color = findTexture(scene.target);
    if (target == sceneTargets_.end() || color == nullptr)
      continue;
    const rhi::Extent size = color->extent();
    PrepareContext targetPrepare{upload_, size};
    for (Drawer* drawer : scene.drawers)
      drawer->prepare(targetPrepare);
  }
  applyPendingWrites(commands);

  for (const TextureScene& scene : frame.textureScenes) {
    drawTextureScene(commands, scene);
  }

  // Multisampled, the scene draws into targets of its own, averaged into the frame (or the post
  // pass's texture) at the end of its pass.
  const bool multisampled = multisampling_ && multisampledTargets(extent);
  const rhi::Image* depth = multisampled ? &*msaaDepth_ : depthTarget(extent);
  const rhi::Viewport viewport = fullViewport(extent);
  const rhi::Rect scissor{.x = 0, .y = 0, .width = extent.width, .height = extent.height};

  // With a post drawer the scene goes into a texture, which the post pass draws into the window.
  const rhi::Image* sceneColor =
      frame.post != nullptr && depth != nullptr ? sceneColorTarget(extent) : nullptr;
  if (depth != nullptr) {
    commands.prepareDepthTarget(*depth);
  }
  if (sceneColor != nullptr) {
    commands.prepareSampledTarget(*sceneColor);
  }
  const VkImageView frameView = sceneColor != nullptr ? sceneColor->view() : begun->view;
  if (multisampled) {
    commands.prepareColorTarget(*msaaColor_);
  }
  commands.beginRendering(
      {.extent = extent,
       .color = {.view = multisampled ? msaaColor_->view() : frameView,
                 .clear = frame.clearColor,
                 .resolve = multisampled ? frameView : VK_NULL_HANDLE},
       .depth = {.view = depth != nullptr ? depth->view() : VK_NULL_HANDLE, .clear = 1.0F},
       .samples = multisampled ? msaaSamples_ : 1U});
  // Without a depth target the scene's pipelines cannot draw; the frame still clears and shows the
  // UI.
  if (depth != nullptr) {
    DrawContext context{commands, upload_, Pass::Scene, extent};
    for (Drawer* drawer : frame.scene) {
      commands.setViewport(viewport);
      commands.setScissor(scissor);
      drawer->draw(context);
    }
  }
  commands.endRendering();

  if (sceneColor != nullptr) {
    commands.finishSampledTarget(*sceneColor);
    commands.beginRendering(
        {.extent = extent, .color = {.view = begun->view, .clear = frame.clearColor}, .depth = {}});
    DrawContext context{commands, upload_, Pass::Post, extent, *sceneColor_};
    commands.setViewport(viewport);
    commands.setScissor(scissor);
    frame.post->draw(context);
    commands.endRendering();
  }

  commands.colorPassBarrier();
  commands.beginRendering(
      {.extent = extent, .color = {.view = begun->view, .clear = std::nullopt}, .depth = {}});
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

  std::optional<core::Result<rhi::FrameCapture>> capture;
  if (captureDone_) {
    capture = rhi::FrameCapture::record(*device_, *begun);
  }
  upload_.endFrame();
  device_->endFrame(*begun);
  if (capture) {
    auto done = std::exchange(captureDone_, nullptr);
    if (*capture) {
      done((*capture)->read());
    } else {
      done(std::unexpected{capture->error()});
    }
  }
  return true;
}

core::Result<rhi::GraphicsPipeline> Renderer::createPipeline(rhi::GraphicsPipelineDesc desc,
                                                             Pass pass) {
  desc.colorFormat = colorFormat_;
  desc.depthFormat = pass == Pass::Scene ? depthFormat_ : rhi::Format::Undefined;
  desc.multisampled = pass == Pass::Scene ? msaaSamples_ : 1U;
  return rhi::GraphicsPipeline::create(*device_, *bindless_, desc);
}

core::Result<TextureId> Renderer::createTexture(rhi::Extent extent,
                                                std::span<const std::byte> pixels, bool mipmapped) {
  auto image = pixels.empty() ? rhi::Image::create(*device_, {.extent = extent,
                                                              .format = rhi::Format::Rgba8Unorm,
                                                              .usage = rhi::ImageUsage::Sampled,
                                                              .mipmapped = mipmapped})
                              : rhi::Image::createTexture(*device_, extent, pixels, mipmapped);
  if (!image) {
    return std::unexpected{image.error()};
  }
  return addTexture(std::move(*image));
}

core::Result<TextureId> Renderer::queueTexture(rhi::Extent extent,
                                               std::span<const std::byte> pixels, bool mipmapped) {
  const rhi::Rect whole{.x = 0, .y = 0, .width = extent.width, .height = extent.height};
  if (!pixels.empty()) {
    if (auto valid = rhi::checkImageWrite(extent, rhi::Format::Rgba8Unorm, whole, pixels.size());
        !valid) {
      return std::unexpected{valid.error()};
    }
    if (pixels.size() > upload_.bytesPerFrame()) {
      return core::fail(
          std::format("a {}x{} texture does not fit in a frame's {} bytes of uploads; create it "
                      "at load time with createTexture",
                      extent.width, extent.height, upload_.bytesPerFrame()));
    }
  }
  auto image = rhi::Image::create(*device_, {.extent = extent,
                                             .format = rhi::Format::Rgba8Unorm,
                                             .usage = rhi::ImageUsage::Sampled,
                                             .initialize = false,
                                             .mipmapped = mipmapped});
  if (!image) {
    return std::unexpected{image.error()};
  }
  auto texture = addTexture(std::move(*image));
  if (texture) {
    pending_.push_back(
        {.texture = *texture, .region = whole, .pixels = copied(pixels), .initialize = true});
  }
  return texture;
}

core::Result<TextureId> Renderer::createSceneTexture(rhi::Extent extent) {
  return addSceneTarget(extent, std::nullopt);
}

core::Result<TextureId> Renderer::createScreenSceneTexture(rhi::Extent shape) {
  // Drawn at the shape's size until the first frame says the window's.
  return addSceneTarget(shape, shape);
}

core::Result<TextureId> Renderer::addSceneTarget(rhi::Extent extent,
                                                 std::optional<rhi::Extent> shape) {
  auto depth = sceneTargetDepth(*device_, extent, depthFormat_);
  if (!depth)
    return std::unexpected{depth.error()};
  auto color = sceneTargetColor(*device_, extent, colorFormat_);
  if (!color)
    return std::unexpected{color.error()};
  auto texture = addTexture(std::move(*color));
  if (!texture)
    return std::unexpected{texture.error()};
  sceneTargets_.push_back({.color = *texture,
                           .depth = std::move(*depth),
                           .shape = shape,
                           .msaaColor = std::nullopt,
                           .msaaDepth = std::nullopt});
  return *texture;
}

void Renderer::fitScreenTargets(rhi::Extent window) {
  bool idle = false;
  for (SceneTarget& target : sceneTargets_) {
    const rhi::Image* image = findTexture(target.color);
    if (!target.shape || image == nullptr)
      continue;
    const rhi::Extent wanted = screenTargetExtent(*target.shape, window);
    if (image->extent() == wanted)
      continue;
    auto depth = sceneTargetDepth(*device_, wanted, depthFormat_);
    auto color = sceneTargetColor(*device_, wanted, colorFormat_);
    if (!depth || !color) {
      core::logWarning("renderer: cannot remake a {}x{} screen target ({}); it keeps its size",
                       wanted.width, wanted.height,
                       !depth ? depth.error().message : color.error().message);
      target.shape.reset();
      continue;
    }
    // The id stays (the UI holds it), so no frame in flight may still sample the old image.
    if (!idle) {
      device_->waitIdle();
      idle = true;
    }
    bindless_->replace(target.color, *color);
    textures_[target.color.index] = std::move(*color);
    target.depth = std::move(*depth);
    target.msaaColor.reset();
    target.msaaDepth.reset();
    core::logDebug("renderer: screen target {} drawn at {}x{} for a {}x{} window",
                   target.color.index, wanted.width, wanted.height, window.width, window.height);
  }
}

bool Renderer::multisampledSceneTarget(SceneTarget& target, rhi::Extent extent) {
  if (!multisampling_) {
    target.msaaColor.reset();
    target.msaaDepth.reset();
    return false;
  }
  if (target.msaaColor && target.msaaDepth && target.msaaColor->extent() == extent)
    return true;
  target.msaaColor.reset();
  target.msaaDepth.reset();
  auto color = rhi::Image::create(*device_, {.extent = extent,
                                             .format = colorFormat_,
                                             .usage = rhi::ImageUsage::ColorTarget,
                                             .samples = msaaSamples_});
  auto depth = rhi::Image::create(*device_, {.extent = extent,
                                             .format = depthFormat_,
                                             .usage = rhi::ImageUsage::DepthTarget,
                                             .samples = msaaSamples_});
  if (!color || !depth) {
    core::logWarning("renderer: cannot make a {}x{} multisampled scene target ({}); drawing it "
                     "without antialiasing",
                     extent.width, extent.height,
                     !color ? color.error().message : depth.error().message);
    return false;
  }
  target.msaaColor = std::move(*color);
  target.msaaDepth = std::move(*depth);
  return true;
}

void Renderer::drawTextureScene(rhi::CommandList& commands, const TextureScene& scene) {
  const auto target = std::ranges::find(sceneTargets_, scene.target, &SceneTarget::color);
  const rhi::Image* color = findTexture(scene.target);
  if (target == sceneTargets_.end() || color == nullptr)
    return;
  const rhi::Extent size = color->extent();
  // A target drawn over keeps its pixels, which multisampled targets do not: it stays single.
  const bool multisampled = !scene.keep && multisampledSceneTarget(*target, size);
  const rhi::Image& depth = multisampled ? *target->msaaDepth : target->depth;
  commands.prepareSampledTarget(*color);
  commands.prepareDepthTarget(depth);
  if (multisampled)
    commands.prepareColorTarget(*target->msaaColor);
  const std::optional<std::array<float, 4>> clear =
      scene.keep ? std::nullopt : std::optional{std::array{0.F, 0.F, 0.F, 0.F}};
  commands.beginRendering(
      {.extent = size,
       .color = {.view = multisampled ? target->msaaColor->view() : color->view(),
                 .clear = clear,
                 .resolve = multisampled ? color->view() : VK_NULL_HANDLE},
       .depth = {.view = depth.view(), .clear = 1.F},
       .samples = multisampled ? msaaSamples_ : 1U});
  DrawContext context{commands, upload_, Pass::Scene, size};
  for (Drawer* drawer : scene.drawers) {
    commands.setViewport(fullViewport(size));
    commands.setScissor({.x = 0, .y = 0, .width = size.width, .height = size.height});
    drawer->draw(context);
  }
  commands.endRendering();
  commands.finishSampledTarget(*color);
}

core::Result<void> Renderer::queueTextureUpdate(TextureId texture, const rhi::Rect& region,
                                                std::span<const std::byte> pixels) {
  const rhi::Image* image = findTexture(texture);
  if (image == nullptr) {
    return core::fail(std::format("there is no texture {}", texture.index));
  }
  if (auto valid = rhi::checkImageWrite(image->extent(), image->format(), region, pixels.size());
      !valid) {
    return valid;
  }
  if (pixels.size() > upload_.bytesPerFrame()) {
    return core::fail(std::format("a {}x{} update does not fit in a frame's {} bytes of uploads",
                                  region.width, region.height, upload_.bytesPerFrame()));
  }
  pending_.push_back(
      {.texture = texture, .region = region, .pixels = copied(pixels), .initialize = false});
  return {};
}

bool Renderer::textureWritePending(TextureId texture) const {
  return std::ranges::any_of(
      pending_, [texture](const PendingWrite& write) { return write.texture == texture; });
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
  bool copied = false;
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
    // Once one copy waits, the ones after it wait too, so updates of a texture keep their order. A
    // copy that does not fit in this frame's upload memory waits for the next frame: many textures
    // made at once (a skin's sprite sheets on its first frame) are not an error. The copies leave a
    // share of the memory to the passes, which upload their vertices after them: filling it all
    // once left a map's first frame without room for its vertices. One larger than a whole
    // frame's memory never fits: the upload says so.
    std::optional<rhi::UploadAllocation> staging;
    const bool fits = textureCopyFits(upload_.used(), write.pixels.size() + PixelAlignment,
                                      upload_.bytesPerFrame(),
                                      upload_.bytesPerFrame() / DrawUploadShare, !copied) ||
                      write.pixels.size() > upload_.bytesPerFrame();
    if (later.empty() && fits) {
      staging = upload_.upload(std::span<const std::byte>{write.pixels}, PixelAlignment);
    }
    if (!staging) {
      later.push_back(std::move(write));
      continue;
    }
    copied = true;
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
    core::logWarning("cannot destroy texture {} (generation {}): there is no such texture",
                     texture.index, texture.generation);
    return;
  }
  std::erase_if(pending_,
                [texture](const PendingWrite& write) { return write.texture == texture; });
  bindless_->remove(texture);
  std::erase_if(sceneTargets_,
                [texture](const SceneTarget& target) { return target.color == texture; });
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
