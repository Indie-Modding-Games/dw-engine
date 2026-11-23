#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/rhi/bindless.hpp>
#include <dwe/rhi/gpu_report.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace dwe::rhi {
namespace {

constexpr std::uint32_t TextureBinding = 0;
constexpr std::uint32_t SamplerBinding = 1;

// Anisotropy of the linear samplers, clamped to the GPU's limit.
constexpr float MaxAnisotropy = 16.0F;
// Anisotropic sampling with VK_LOD_CLAMP_NONE faults on some RDNA drivers. Our textures have far
// fewer levels than this, so clamping the upper LOD changes no image and avoids the fault.
constexpr float MaxAnisotropicLod = 24.0F;

// Opt-in safety mode for AMD's RX 6000 (RDNA2), switched on in the launcher's settings (the game
// receives --rdna2-safe). Every other GPU keeps the original descriptor layout even if the switch
// was left on by mistake: the vendor and the model are checked here as well as in the interface.
bool rdna2SafeMode(VkPhysicalDevice physicalDevice, bool requested) {
  if (!requested) {
    return false;
  }
  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);
  return properties.vendorID == 0x1002U &&
         std::string_view{properties.deviceName}.find("RX 6") != std::string_view::npos;
}

// Points a texture descriptor at an image view without needing the table: the deferred releases
// run after frames finish, possibly after the table itself is gone.
void writeView(VkDevice device, VkDescriptorSet set, std::uint32_t slot, VkImageView view) {
  const VkDescriptorImageInfo imageInfo{.sampler = VK_NULL_HANDLE,
                                        .imageView = view,
                                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet descriptor{};
  descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor.dstSet = set;
  descriptor.dstBinding = TextureBinding;
  descriptor.dstArrayElement = slot;
  descriptor.descriptorCount = 1;
  descriptor.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  descriptor.pImageInfo = &imageInfo;
  vkUpdateDescriptorSets(device, 1, &descriptor, 0, nullptr);
}

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
                                                                   std::uint32_t capacity,
                                                                   bool rdna2Safe) {
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
    samplerInfo.maxLod = linear ? MaxAnisotropicLod : VK_LOD_CLAMP_NONE;
    if (gpuCheck("vkCreateSampler", vkCreateSampler(gpu.device, &samplerInfo, nullptr,
                                                    &table->samplers_[kind])) != VK_SUCCESS) {
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
  const bool safe = rdna2SafeMode(gpu.physicalDevice, rdna2Safe);
  if (rdna2Safe && !safe) {
    core::logInfo("--rdna2-safe was passed but this is not an AMD RX 6000; "
                  "keeping the default descriptor layout");
  }
  // By default we keep every flag the GPU advertises, so this changes nothing for existing
  // players. In the opt-in RX 6000 mode we drop the two extra guarantees: every slot already holds
  // a live view (create() fills them with a white texel, add() writes only a freshly acquired slot
  // and remove() defers its reset and reuse until the frames in flight are done), so neither
  // partially bound nor update-unused-while-pending is needed. The simpler layout avoids asking
  // AMD's proprietary driver to track those paths.
  const VkDescriptorBindingFlags textureFlags =
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
      (safe ? 0
            : VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                  VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
  const std::array<VkDescriptorBindingFlags, 2> bindingFlags{textureFlags, 0};
  if (safe) {
    core::logInfo("AMD RX 6000 safe mode: bindless table uses only UPDATE_AFTER_BIND");
  }
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
  if (gpuCheck("vkCreateDescriptorSetLayout",
               vkCreateDescriptorSetLayout(gpu.device, &layoutInfo, nullptr, &table->setLayout_)) !=
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
  if (gpuCheck("vkCreateDescriptorPool", vkCreateDescriptorPool(gpu.device, &poolInfo, nullptr,
                                                                &table->pool_)) != VK_SUCCESS) {
    return core::fail("could not create the pool of the bindless table");
  }
  const VkDescriptorSetAllocateInfo setInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                            .pNext = nullptr,
                                            .descriptorPool = table->pool_,
                                            .descriptorSetCount = 1,
                                            .pSetLayouts = &table->setLayout_};
  if (gpuCheck("vkAllocateDescriptorSets",
               vkAllocateDescriptorSets(gpu.device, &setInfo, &table->set_)) != VK_SUCCESS) {
    return core::fail("could not allocate the bindless table");
  }

  // Pre-fill every slot with a live white texel. A shader that reads an unregistered or stale
  // id then samples defined pixels instead of a descriptor whose image may have been destroyed.
  constexpr std::array WhiteTexel{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
  auto fallback = Image::createTexture(device, {.width = 1, .height = 1}, WhiteTexel);
  if (!fallback) {
    return std::unexpected{fallback.error()};
  }
  table->fallback_.emplace(std::move(*fallback));
  for (std::uint32_t slot = 0; slot < capacity; ++slot) {
    writeView(gpu.device, table->set_, slot, table->fallback_->view());
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
  if (gpuCheck("vkCreatePipelineLayout",
               vkCreatePipelineLayout(gpu.device, &pipelineLayoutInfo, nullptr,
                                      &table->pipelineLayout_)) != VK_SUCCESS) {
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
  writeView(device_->nativeHandles().device, set_, slot, image.view());
}

bool BindlessTable::remove(TextureId texture) {
  if (!contains(texture)) {
    core::logWarning("the bindless table has no texture {} (generation {})", texture.index,
                     texture.generation);
    return false;
  }
  // Ids of the removed texture stop matching at once; its slot is reused only after the frames in
  // flight. In the same deferred step, point the slot back at the fallback: a driver that reads a
  // stale id while the old image is being destroyed then samples a live view, not freed memory.
  live_[texture.index] = false;
  ++generations_[texture.index];
  const VkDevice gpu = device_->nativeHandles().device;
  const VkDescriptorSet set = set_;
  const VkImageView fallbackView = fallback_->view();
  device_->destroyLater([slots = slots_, index = texture.index, gpu, set, fallbackView] {
    writeView(gpu, set, index, fallbackView);
    slots->release(index);
  });
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
