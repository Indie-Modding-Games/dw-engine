# rhi

The only module that talks to Vulkan: 1.3 at least, 1.4 when the driver has it
([ADR 0003](../../adr/0003-vulkan-and-sdl3.md)). See [render.md](../../architecture/render.md) for the whole.

- `Device`: GPU, queue, swapchain and frames with dynamic rendering (two in flight; `Frame::slot` says which). The
  swapchain is rebuilt by itself when the window size changes. What is released while the GPU may still be using it
  goes through `Device::destroyLater`, which destroys it when the frames that may use it finish. It carries the memory
  allocator (VMA).
- `Buffer`: GPU memory (filled with an upload that waits, `createWithData`) or host memory (written directly,
  `createMapped`), with a GPU address.
- `Image`: sampled 2D images, color or depth; `createTexture` uploads 8-bit RGBA and `write` replaces
  a region, waiting for the GPU. Without waiting: `ImageDesc::initialize = false` and `CommandList::initializeImage` /
  `writeImage` inside the frame. `bestDepthFormat` chooses the depth format. `ImageDesc::mipmapped` gives the
  whole mip chain (`mipLevelCount`), which each write redoes with blits; `ImageDesc::samples` makes multisampled
  targets and `maxSampleCount` says how many samples the GPU accepts.
- `BindlessTable`: the engine's only descriptor set (textures by index and generation, `TextureId`, and four fixed
  samplers)
  and the only pipeline layout (that set and 128 bytes of push constants) ([ADR 0019](../../adr/0019-bindless-descriptors.md)).
- `GraphicsPipeline`: graphics pipelines from a `GraphicsPipelineDesc` (SPIR-V with the two entry points, vertices,
  topology, cull, depth, blending, formats). `validate` checks the description without a GPU. With
  `GraphicsPipelineDesc::multisampled` (2, 4 or 8) the pipeline also carries a variant for a pass with those
  samples: `CommandList::bindPipeline` uses the one that matches the pass (`RenderingDesc::samples`), and
  `ColorAttachment::resolve` averages the multisampled target into another image at the end of the pass.
- `UploadRing`: per-frame memory for data that changes every frame (bones, UI vertices) and for texture uploads;
  it is only allocated between `beginFrame` and `endFrame`.
- `CommandList`: recording of a frame (passes, image uploads, pipelines, viewport, scissor clipped to the target,
  push constants, buffers, draws).
- `Texture`: uploads to the GPU an image with its mipmap chain, in 8-bit RGBA or in BC7 (today, the overlay's asset
  viewer); `Device::supportsBc7` says whether the GPU samples BC7.

**Vulkan path** (`vulkan_path.hpp`). `Device::create` picks one of three paths and logs it with the versions it saw,
for example `vulkan: Vulkan 1.4 path (host image copy on; device 1.4.318, loader 1.4.341)`:

- **Vulkan 1.4**: the loader and the selected GPU both report 1.4. The instance asks for 1.4 and the device enables
  the 1.4 features it uses, after querying `VkPhysicalDeviceVulkan14Features` (nothing is assumed). Today that is
  **host image copy**: `Image::createTexture` (without mips) and `Texture::create` fill a fresh image from the CPU with
  `vkTransitionImageLayout` + `vkCopyMemoryToImage`, straight into `SHADER_READ_ONLY_OPTIMAL`, with no staging buffer,
  no command buffer and no wait on the queue. It is used only when the feature is on, the driver lists that layout
  among its host copy destinations, the format has `HOST_IMAGE_TRANSFER` and the driver says host copy does not slow
  down sampling (`VkHostImageCopyDevicePerformanceQuery::optimalDeviceAccess`); the entry points are loaded with
  `vkGetDeviceProcAddr`, so an older loader cannot keep the game from starting. Otherwise that image goes through
  the 1.3 upload. Writes into images that frames may still be using (`Image::write`, `CommandList::writeImage`)
  stay on the GPU with their barriers. Push descriptors and maintenance5/6 are not used: the renderer has a single
  bindless set and no per-draw descriptors, and they would add a second branch without removing code.
- **Vulkan 1.3**: the loader or the GPU stop at 1.3, or `--vulkan13` (`DeviceDesc::forceVulkan13`) asks for it to
  debug or compare. The instance then asks for 1.3 and nothing from 1.4 is queried.
- **Vulkan 1.2 compatible**: `--compatible`, or the fallback below. The instance asks for 1.2, so every GPU, a 1.4 one
  too, runs as a 1.2 one: the drivers and the validation layer see exactly what an old card would. Dynamic rendering
  and synchronization2 come from their extensions, and their entry points (`vkCmdBeginRenderingKHR`,
  `vkCmdPipelineBarrier2KHR`, `vkQueueSubmit2KHR`...) are loaded with `vkGetDeviceProcAddr` (`vulkan_calls.hpp`): a
  strict driver gives no core 1.3 entry point to a 1.2 device. Mip chains are blitted with the 1.0 `vkCmdBlitImage`,
  which every path has.

**Fallback.** When a path cannot make the device (no GPU passes its selection, `vkCreateDevice` or the swapchain
fails), `Device::create` tries the next one in the same call and logs why: 1.4, then 1.3, then 1.2 compatible
(`nextVulkanAttempt`). A GPU that works keeps its first path, so nothing changes for it. When none works the game
exits with code 114, which the launcher reads as "this graphics step does not start here"
([launcher](../apps/launcher.md#computer-requirements)). `--check-system` follows the same chain and prints
`floor=1.2` when only the compatible floor passed.

`chooseVulkanPath` and `nextVulkanAttempt` are pure functions (versions and feature bits in, path out) with their
tests in `tests/vulkan_path_test.cpp`. `Device::vulkanPath` says which one runs, and the graphics report adds it to the driver
line. `NativeHandles::apiVersion` and VMA get the version of the path.

`LinearAllocator` and `SlotAllocator` are the CPU bookkeeping of the ring and of the table; their tests do not need a GPU.

**Graphics report** (`gpu_report.hpp`). A process-wide record of the GPU, its driver, the current activity and the last
12 Vulkan calls (`gpuCheck`). A failed call writes it to the log, and the game's crash handlers print it to stderr when
the game dies of an uncaught exception or a fatal signal. Two optional breadcrumbs are turned on when the GPU has them,
at almost no cost, and the log says which (`gpu report: device fault details on, GPU markers off`):

- `VK_EXT_device_fault`: once the device is lost, the driver's description, the faulting address ranges and its vendor
  records are added to the report.
- `VK_AMD_buffer_marker`: the GPU writes a marker after each pass, image write, upload and present, so the report names
  the last work it finished and the first it never reached (`frame markers: last finished #… ; first unfinished #…`).

After a `VK_ERROR_DEVICE_LOST` nothing waits for the GPU any more (`Device::lost`): `beginFrame` gives no frame, and
`waitIdle` and uploads return at once. The game then closes without saving, with exit code 113.
