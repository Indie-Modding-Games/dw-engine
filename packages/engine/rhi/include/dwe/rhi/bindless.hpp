#pragma once

#include <dwe/core/error.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/rhi/image.hpp>
#include <dwe/rhi/slot_allocator.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace dwe::rhi {

// The fixed samplers of the bindless table, in the order of the Sampler constants of
// packages/engine/rhi/shaders/dwe/bindless.slang.
enum class SamplerKind : std::uint8_t {
  NearestClamp,
  NearestRepeat,
  LinearClamp,
  LinearRepeat,
};

inline constexpr std::uint32_t SamplerKindCount = 4;

// A texture in the bindless table: its index in the shaders' gTextures array, which is what shaders
// take, and the generation of that slot, which tells a live texture from an earlier one that used
// the same index.
struct TextureId {
  static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

  std::uint32_t index = InvalidIndex;
  std::uint32_t generation = 0;

  [[nodiscard]] bool valid() const { return index != InvalidIndex; }
  friend bool operator==(const TextureId&, const TextureId&) = default;
};

// Bytes of push constants every pipeline may use: the minimum every GPU supports. Shared by all
// stages.
inline constexpr std::uint32_t PushConstantBytes = 128;

// The one descriptor set of the engine (docs/architecture/render.md): binding 0 is an array of
// sampled images that shaders index with a TextureId, binding 1 the fixed samplers. Every pipeline
// uses its pipeline layout: this set plus PushConstantBytes of push constants. Per-draw data goes
// in push constants or in buffers read by device address.
class BindlessTable {
public:
  // capacity is clamped to what the GPU supports.
  static core::Result<std::unique_ptr<BindlessTable>> create(Device& device,
                                                             std::uint32_t capacity);

  BindlessTable(const BindlessTable&) = delete;
  BindlessTable& operator=(const BindlessTable&) = delete;
  BindlessTable(BindlessTable&&) = delete;
  BindlessTable& operator=(BindlessTable&&) = delete;
  ~BindlessTable();

  // Registers a sampled image. The image must stay alive until remove(), and remove() must come
  // before the image is dropped.
  core::Result<TextureId> add(const Image& image);

  // Unregisters a texture. Its index is reused only once the frames in flight, which may still
  // sample it, finish. Returns false, and changes nothing, when the id names no registered texture
  // (already removed, or stale).
  bool remove(TextureId texture);

  // Points a registered texture's id at another image, which must stay alive until the next
  // replace() or remove(). No frame in flight may sample the old image: wait for the device first.
  // Returns false, and changes nothing, when the id names no registered texture.
  bool replace(TextureId texture, const Image& image);

  // Whether the id names a registered texture, generation included.
  [[nodiscard]] bool contains(TextureId texture) const;

  [[nodiscard]] VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }
  [[nodiscard]] VkDescriptorSet descriptorSet() const { return set_; }
  [[nodiscard]] std::uint32_t capacity() const;
  [[nodiscard]] std::uint32_t used() const;

private:
  explicit BindlessTable(Device& device) : device_(&device) {}
  // Points the shaders' texture `slot` at the image.
  void write(std::uint32_t slot, const Image& image);

  Device* device_;
  VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  std::array<VkSampler, SamplerKindCount> samplers_{};
  // Shared with the deferred releases, which may run after the table is gone.
  std::shared_ptr<SlotAllocator> slots_;
  // Per slot: its current generation and whether a texture is registered in it.
  std::vector<std::uint32_t> generations_;
  std::vector<bool> live_;
};

} // namespace dwe::rhi
