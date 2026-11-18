# 0019. A single bindless descriptor set and per-frame data by buffer address

- **Status:** Accepted
- **Date:** 2026-10-19

## Context

The phase 2 renderer has several clients (map backgrounds and layers, skinned models, RmlUi, minimap) that draw with
different textures and data that changes every frame (bone matrices, UI vertices). Managing descriptor sets per
material or per draw adds a lot of code in `rhi`, pools that run out and synchronization with the frames in flight.
Vulkan 1.2 already ships descriptor indexing and buffer device address, and all the PC GPUs that meet ADR 0003
support them.

## Decision

- **A single descriptor set in the whole engine** (`rhi::BindlessTable`): binding 0, an array of sampled images (4096
  by default, limited by the GPU) with `UPDATE_AFTER_BIND`, `UPDATE_UNUSED_WHILE_PENDING` and `PARTIALLY_BOUND`;
  binding 1, four immutable samplers (nearest/linear × clamp/repeat). The nearest ones take the closest mip and do not
  filter (pixel art, UI); the linear ones blend mips and use 16× anisotropy (or the GPU's maximum).
- A texture is named by `rhi::TextureId`: its index in the array, which is the only thing the shader receives (in push
  constants or in the vertices), and the generation of that slot. The table bumps the generation when a texture is
  removed, so an old id stops being valid even if its index is reused; removing it twice does nothing.
- **A single pipeline layout**: that set plus 128 bytes of push constants for all graphics stages. It is bound once
  per frame.
- **Per-frame data by buffer address**: `rhi::UploadRing` splits a host-visible buffer into one chunk per frame in
  flight; each allocation gives its GPU address, which the shader reads as a pointer (`float4x4*` in Slang). Dynamic
  vertices are bound as a vertex buffer with an offset inside the same ring.
- Textures are freed with `Device::destroyLater`; their index is reused only when the frames that could use it have
  finished. That is why a descriptor is only written into slots that no pending frame uses, which is what
  `UPDATE_UNUSED_WHILE_PENDING` allows doing with frames still in flight.
- **Color in gamma space**: the swapchain is `B8G8R8A8_UNORM` (or `R8G8B8A8_UNORM` if the GPU does not offer the
  first), not sRGB, and textures are `R8G8B8A8_UNORM`. Shaders work with the values as they come and blending happens
  in gamma space, like the original and as Dear ImGui expects. A linear render (sRGB textures and swapchain) would be
  another decision.

## Consequences

- Adding a texture is writing a descriptor; a new pipeline does not need layouts of its own.
- UI draws with several textures fit in a single draw (the index goes in the vertex).
- It requires Vulkan 1.2 with `descriptorIndexing`, `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`,
  `descriptorBindingSampledImageUpdateAfterBind`, `descriptorBindingUpdateUnusedWhilePending`,
  `shaderSampledImageArrayNonUniformIndexing`, `bufferDeviceAddress` and `separateDepthStencilLayouts` (the depth
  target uses `DEPTH_ATTACHMENT_OPTIMAL`), and `samplerAnisotropy`; the device requires them when choosing the GPU.
- Shaders must use `NonUniformResourceIndex` when the index is not uniform (`sampleTexture` does it).
- Push constants use `VK_SHADER_STAGE_ALL_GRAPHICS` and not `VK_SHADER_STAGE_ALL`: with `ALL`, the NVIDIA driver
  tested leaks memory on every `vkCmdPushConstants` (LeakSanitizer detects it).
- Storage buffers or uniforms with descriptors, if they were needed, would be added as new bindings of the same set.

## Alternatives rejected

- **Sets per material or per draw:** more code, pools to manage and more calls per draw.
- **Push descriptors:** they still need a layout per draw type and do not work for the UI with many textures.
- **Dynamic uniform buffers for the matrices:** they need descriptors and alignments of their own; the buffer address
  is simpler and has no practical size limit.
