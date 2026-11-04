#pragma once

#include <dwe/rhi/bindless.hpp>
#include <dwe/rhi/buffer.hpp>
#include <dwe/rhi/image.hpp>
#include <dwe/rhi/pipeline.hpp>
#include <dwe/rhi/types.hpp>
#include <dwe/rhi/upload_ring.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace dwe::rhi {

struct ColorAttachment {
  VkImageView view = VK_NULL_HANDLE;
  // Clears the target to this color, or keeps what it holds when empty.
  std::optional<std::array<float, 4>> clear;
};

struct DepthAttachment {
  VkImageView view = VK_NULL_HANDLE;
  // Clears the target to this depth (1 is the far plane), or keeps what it holds when empty.
  std::optional<float> clear = 1.0F;
};

// The targets of one dynamic rendering pass, already in the attachment layouts.
struct RenderingDesc {
  Extent extent{};
  ColorAttachment color{};
  // No depth target when its view is null.
  DepthAttachment depth{};
};

// Records draws into a frame's command buffer: the global descriptor set, pipelines, dynamic state,
// push constants, vertex and index buffers. A thin layer over Vulkan, so the render module and its
// drawers never call it directly.
class CommandList {
public:
  // Wraps commands, which must be recording, and binds the bindless table to it.
  CommandList(VkCommandBuffer commands, const BindlessTable& table);

  void beginRendering(const RenderingDesc& desc);
  void endRendering();

  // Makes what earlier passes drew into color targets visible to later passes that load them, and
  // keeps those passes from drawing before the earlier ones finish.
  void colorPassBarrier();

  // Moves a depth target to the attachment layout, discarding what it held, once earlier frames
  // finish with it. For depth targets cleared every frame.
  void prepareDepthTarget(const Image& depth);

  // A sampled color target, cleared or drawn over (its pixels kept) in each use. Call outside
  // rendering; finish before sampling.
  void prepareSampledTarget(const Image& color);
  void finishSampledTarget(const Image& color);

  // Records a clear of a sampled image created with ImageDesc::initialize off, which it must be
  // before anything samples it. Outside rendering only.
  void initializeImage(const Image& image);

  // Records a copy of region.width * region.height tightly packed texels, written to source this
  // frame, into a region of a sampled color image. Draws recorded later see the new texels. Outside
  // rendering only.
  void writeImage(const Image& image, const Rect& region, const UploadAllocation& source);

  // Binds a pipeline. Pipelines made with DepthState::bias start with no bias: call setDepthBias
  // after this.
  void bindPipeline(const GraphicsPipeline& pipeline);
  void setViewport(const Viewport& viewport);
  // Clipped to the current rendering's target: off-target rectangles are fine.
  void setScissor(const Rect& scissor);
  // Only for pipelines made with DepthState::bias.
  void setDepthBias(float constantFactor, float slopeFactor);

  // Sets the push constants (at most PushConstantBytes) of every stage.
  void pushConstants(std::span<const std::byte> data, std::uint32_t offset = 0);

  template <typename T>
    requires std::is_trivially_copyable_v<T>
  void pushConstants(const T& value) {
    static_assert(sizeof(T) <= PushConstantBytes,
                  "push constants are limited to PushConstantBytes");
    pushConstants(std::as_bytes(std::span{&value, 1}));
  }

  void bindVertexBuffer(const Buffer& buffer, std::size_t offset = 0);
  void bindVertexBuffer(const UploadAllocation& allocation) {
    bindVertexBuffer(*allocation.buffer, allocation.offset);
  }
  void bindIndexBuffer(const Buffer& buffer, IndexType type, std::size_t offset = 0);
  void bindIndexBuffer(const UploadAllocation& allocation, IndexType type) {
    bindIndexBuffer(*allocation.buffer, type, allocation.offset);
  }

  void draw(std::uint32_t vertexCount, std::uint32_t firstVertex = 0,
            std::uint32_t instanceCount = 1, std::uint32_t firstInstance = 0);
  void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex = 0,
                   std::int32_t vertexOffset = 0, std::uint32_t instanceCount = 1,
                   std::uint32_t firstInstance = 0);

  // The raw command buffer, for third-party renderers (the debug UI).
  [[nodiscard]] VkCommandBuffer native() const { return commands_; }

private:
  VkCommandBuffer commands_;
  VkPipelineLayout layout_;
  // The target of the current rendering, which scissors are clipped to.
  Extent target_;
};

} // namespace dwe::rhi
