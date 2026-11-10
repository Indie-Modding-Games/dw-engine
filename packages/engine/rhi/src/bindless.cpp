#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/rhi/bindless.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace dwe::rhi {
namespace {

constexpr std::uint32_t TextureBinding = 0;
constexpr std::uint32_t SamplerBinding = 1;

// Anisotropy of the linear samplers, clamped to the GPU's limit.
constexpr float MaxAnisotropy = 16.0F;

struct SamplerSettings {
  VkFilter filter;
  VkSamplerAddressMode address;
};

// In the order of SamplerKind. Nearest samplers keep texels sharp (pixel art, UI) and pick the
// nearest mip level; linear ones blend between mip levels and filter anisotropically.
constexpr std::array<SamplerSettings, SamplerKindCount> SamplerTable{{
    {.filter = VK_FILTER_NEAREST, .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE},
    {.filter = VK_FILTER_NEAREST, .address = VK_SAMPLER_ADDRESS_MODE_REPEAT},
    {.filter = VK_FILTER_LINEAR, .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE},
    {.filter = VK_FILTER_LINEAR, .address = VK_SAMPLER_ADDRESS_MODE_REPEAT},
}};

} // namespace

core::Result<std::unique_ptr<BindlessTable>> BindlessTable::create(Device& device,
                                                                   std::uint32_t capacity) {
  const NativeHandles gpu = device.nativeHandles();
  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
  VkPhysicalDeviceVulkan12Properties properties12{};
  properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
  VkPhysicalDeviceProperties2 properties{};
  properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties.pNext = &properties12;
  vkGetPhysicalDeviceProperties2(gpu.physicalDevice, &properties);
  // Every stage may sample, so the per-stage limit applies as well as the per-set one.
  const std::uint32_t limit =
      std::min(properties12.maxDescriptorSetUpdateAfterBindSampledImages,
               properties12.maxPerStageDescriptorUpdateAfterBindSampledImages);
  if (capacity > limit) {
    core::logWarning("the GPU holds {} bindless textures, not {}", limit, capacity);
    capacity = limit;
  }
  if (capacity == 0) {
    return core::fail("the bindless table needs room for at least one texture");
  }

  std::unique_ptr<BindlessTable> table{new BindlessTable{device}};
  table->slots_ = std::make_shared<SlotAllocator>(capacity);
  table->generations_.resize(capacity, 0);
  table->live_.resize(capacity, false);

  for (std::size_t kind = 0; kind < SamplerKindCount; ++kind) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = SamplerTable[kind].filter;
    samplerInfo.minFilter = SamplerTable[kind].filter;
    const bool linear = SamplerTable[kind].filter == VK_FILTER_LINEAR;
    samplerInfo.mipmapMode =
        linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.anisotropyEnable = linear ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy =
        std::min(MaxAnisotropy, properties.properties.limits.maxSamplerAnisotropy);
    samplerInfo.addressModeU = SamplerTable[kind].address;
    samplerInfo.addressModeV = SamplerTable[kind].address;
    samplerInfo.addressModeW = SamplerTable[kind].address;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(gpu.device, &samplerInfo, nullptr, &table->samplers_[kind]) != VK_SUCCESS) {
      return core::fail("could not create the samplers of the bindless table");
    }
  }

  const std::array bindings{
      VkDescriptorSetLayoutBinding{.binding = TextureBinding,
                                   .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                   .descriptorCount = capacity,
                                   .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                                   .pImmutableSamplers = nullptr},
      VkDescriptorSetLayoutBinding{.binding = SamplerBinding,
                                   .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                   .descriptorCount = SamplerKindCount,
                                   .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                                   .pImmutableSamplers = table->samplers_.data()},
  };
  // Textures come and go while frames that use the set are in flight, and shaders only touch the
  // registered ones: a slot is written only while no pending frame uses it (remove() defers its
  // reuse).
  const std::array<VkDescriptorBindingFlags, 2> bindingFlags{
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
          VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
          VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      0};
  const VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .pNext = nullptr,
      .bindingCount = static_cast<std::uint32_t>(bindingFlags.size()),
      .pBindingFlags = bindingFlags.data()};
  const VkDescriptorSetLayoutCreateInfo layoutInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &flagsInfo,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindingCount = static_cast<std::uint32_t>(bindings.size()),
      .pBindings = bindings.data()};
  if (vkCreateDescriptorSetLayout(gpu.device, &layoutInfo, nullptr, &table->setLayout_) !=
      VK_SUCCESS) {
    return core::fail("could not create the layout of the bindless table");
  }

  const std::array poolSizes{
      VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = capacity},
      VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = SamplerKindCount},
  };
  const VkDescriptorPoolCreateInfo poolInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      .maxSets = 1,
      .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
      .pPoolSizes = poolSizes.data()};
  if (vkCreateDescriptorPool(gpu.device, &poolInfo, nullptr, &table->pool_) != VK_SUCCESS) {
    return core::fail("could not create the pool of the bindless table");
  }
  const VkDescriptorSetAllocateInfo setInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                            .pNext = nullptr,
                                            .descriptorPool = table->pool_,
                                            .descriptorSetCount = 1,
                                            .pSetLayouts = &table->setLayout_};
  if (vkAllocateDescriptorSets(gpu.device, &setInfo, &table->set_) != VK_SUCCESS) {
    return core::fail("could not allocate the bindless table");
  }

  const VkPushConstantRange pushConstants{
      .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS, .offset = 0, .size = PushConstantBytes};
  const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .setLayoutCount = 1,
      .pSetLayouts = &table->setLayout_,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushConstants};
  if (vkCreatePipelineLayout(gpu.device, &pipelineLayoutInfo, nullptr, &table->pipelineLayout_) !=
      VK_SUCCESS) {
    return core::fail("could not create the pipeline layout of the bindless table");
  }
  core::logInfo("bindless table: {} textures", capacity);
  return table;
}

BindlessTable::~BindlessTable() {
  // Frames in flight may still use the set and the layout: they go when those frames finish.
  device_->destroyLater([device = device_->nativeHandles().device, pipelineLayout = pipelineLayout_,
                         pool = pool_, setLayout = setLayout_, samplers = samplers_] {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    for (VkSampler sampler : samplers) {
      vkDestroySampler(device, sampler, nullptr);
    }
  });
}

core::Result<TextureId> BindlessTable::add(const Image& image) {
  const auto slot = slots_->acquire();
  if (!slot) {
    return core::fail(std::format("the bindless table is full ({} textures)", slots_->capacity()));
  }
  write(*slot, image);
  live_[*slot] = true;
  return TextureId{.index = *slot, .generation = generations_[*slot]};
}

bool BindlessTable::replace(TextureId texture, const Image& image) {
  if (!contains(texture)) {
    core::logWarning("the bindless table has no texture {} (generation {}) to replace",
                     texture.index, texture.generation);
    return false;
  }
  write(texture.index, image);
  return true;
}

void BindlessTable::write(std::uint32_t slot, const Image& image) {
  const VkDescriptorImageInfo imageInfo{.sampler = VK_NULL_HANDLE,
                                        .imageView = image.view(),
                                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet descriptor{};
  descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor.dstSet = set_;
  descriptor.dstBinding = TextureBinding;
  descriptor.dstArrayElement = slot;
  descriptor.descriptorCount = 1;
  descriptor.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  descriptor.pImageInfo = &imageInfo;
  vkUpdateDescriptorSets(device_->nativeHandles().device, 1, &descriptor, 0, nullptr);
}

bool BindlessTable::remove(TextureId texture) {
  if (!contains(texture)) {
    core::logWarning("the bindless table has no texture {} (generation {})", texture.index,
                     texture.generation);
    return false;
  }
  // Ids of the removed texture stop matching at once; its slot is reused only after the frames in
  // flight.
  live_[texture.index] = false;
  ++generations_[texture.index];
  device_->destroyLater([slots = slots_, index = texture.index] { slots->release(index); });
  return true;
}

bool BindlessTable::contains(TextureId texture) const {
  return texture.valid() && texture.index < live_.size() && live_[texture.index] &&
         generations_[texture.index] == texture.generation;
}

std::uint32_t BindlessTable::capacity() const {
  return slots_->capacity();
}

std::uint32_t BindlessTable::used() const {
  return slots_->used();
}

} // namespace dwe::rhi
