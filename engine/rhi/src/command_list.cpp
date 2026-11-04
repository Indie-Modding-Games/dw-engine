#include <dwe/core/assert.hpp>
#include <dwe/rhi/command_list.hpp>

#include "vulkan_util.hpp"

namespace dwe::rhi {

CommandList::CommandList(VkCommandBuffer commands, const BindlessTable& table)
    : commands_(commands), layout_(table.pipelineLayout()) {
  const VkDescriptorSet set = table.descriptorSet();
  vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0,
                          nullptr);
}

void CommandList::beginRendering(const RenderingDesc& desc) {
  target_ = desc.extent;
  VkRenderingAttachmentInfo color{};
  color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color.imageView = desc.color.view;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = desc.color.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  if (desc.color.clear) {
    const auto& [red, green, blue, alpha] = *desc.color.clear;
    color.clearValue.color = {{red, green, blue, alpha}};
  }

  VkRenderingAttachmentInfo depth{};
  depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depth.imageView = desc.depth.view;
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = desc.depth.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  // The depth is only needed within the frame.
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.clearValue.depthStencil = {.depth = desc.depth.clear.value_or(1.0F), .stencil = 0};

  VkRenderingInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering.renderArea = {.offset = {.x = 0, .y = 0},
                          .extent = {.width = desc.extent.width, .height = desc.extent.height}};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;
  rendering.pDepthAttachment = desc.depth.view != VK_NULL_HANDLE ? &depth : nullptr;
  vkCmdBeginRendering(commands_, &rendering);
}

void CommandList::endRendering() {
  vkCmdEndRendering(commands_);
}

void CommandList::colorPassBarrier() {
  VkMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  barrier.dstAccessMask =
      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.memoryBarrierCount = 1;
  dependency.pMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(commands_, &dependency);
}

void CommandList::prepareDepthTarget(const Image& depth) {
  constexpr VkPipelineStageFlags2 DepthStages =
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  transitionImage(commands_, depth.native(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, DepthStages,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, DepthStages,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

void CommandList::initializeImage(const Image& image) {
  DWE_ASSERT(bytesPerPixel(image.format()) != 0);
  recordImageClear(commands_, image.native());
}

void CommandList::prepareSampledTarget(const Image& color) {
  transitionImage(commands_, color.native(), VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

void CommandList::finishSampledTarget(const Image& color) {
  transitionImage(
      commands_, color.native(), VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void CommandList::writeImage(const Image& image, const Rect& region,
                             const UploadAllocation& source) {
  DWE_ASSERT(source.buffer != nullptr);
  DWE_ASSERT(
      checkImageWrite(image.extent(), image.format(), region, source.data.size()).has_value());
  recordImageWrite(commands_, image.native(), source.buffer->native(), source.offset, region);
}

void CommandList::bindPipeline(const GraphicsPipeline& pipeline) {
  DWE_ASSERT(pipeline.layout() == layout_);
  vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.native());
  if (pipeline.hasDepthBias()) {
    // Dynamic state outlives the pipeline: without this the bias of an earlier draw would carry
    // over.
    setDepthBias(0.0F, 0.0F);
  }
}

void CommandList::setViewport(const Viewport& viewport) {
  const VkViewport native{.x = viewport.x,
                          .y = viewport.y,
                          .width = viewport.width,
                          .height = viewport.height,
                          .minDepth = viewport.minDepth,
                          .maxDepth = viewport.maxDepth};
  vkCmdSetViewport(commands_, 0, 1, &native);
}

void CommandList::setScissor(const Rect& scissor) {
  const Rect clipped = clampToExtent(scissor, target_);
  const VkRect2D native{.offset = {.x = clipped.x, .y = clipped.y},
                        .extent = {.width = clipped.width, .height = clipped.height}};
  vkCmdSetScissor(commands_, 0, 1, &native);
}

void CommandList::setDepthBias(float constantFactor, float slopeFactor) {
  vkCmdSetDepthBias(commands_, constantFactor, 0.0F, slopeFactor);
}

void CommandList::pushConstants(std::span<const std::byte> data, std::uint32_t offset) {
  DWE_ASSERT(offset + data.size() <= PushConstantBytes);
  vkCmdPushConstants(commands_, layout_, VK_SHADER_STAGE_ALL_GRAPHICS, offset,
                     static_cast<std::uint32_t>(data.size()), data.data());
}

void CommandList::bindVertexBuffer(const Buffer& buffer, std::size_t offset) {
  const VkBuffer native = buffer.native();
  const VkDeviceSize nativeOffset = offset;
  vkCmdBindVertexBuffers(commands_, 0, 1, &native, &nativeOffset);
}

void CommandList::bindIndexBuffer(const Buffer& buffer, IndexType type, std::size_t offset) {
  vkCmdBindIndexBuffer(commands_, buffer.native(), offset,
                       type == IndexType::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void CommandList::draw(std::uint32_t vertexCount, std::uint32_t firstVertex,
                       std::uint32_t instanceCount, std::uint32_t firstInstance) {
  vkCmdDraw(commands_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandList::drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex,
                              std::int32_t vertexOffset, std::uint32_t instanceCount,
                              std::uint32_t firstInstance) {
  vkCmdDrawIndexed(commands_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

} // namespace dwe::rhi
