# rhi

The only module that talks to Vulkan 1.3 ([ADR 0003](../../adr/0003-vulkan-and-sdl3.md)). See
[render.md](../../architecture/render.md) for the whole.

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

`LinearAllocator` and `SlotAllocator` are the CPU bookkeeping of the ring and of the table; their tests do not need a GPU.
