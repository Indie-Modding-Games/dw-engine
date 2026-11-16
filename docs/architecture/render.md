# Render

How the engine draws: the GPU base in `rhi` (Vulkan 1.3, [ADR 0003](../adr/0003-vulkan-and-sdl3.md)), the frame flow
in `render`, and how the map, the models and the UI plug in their drawing.

## Layers

| Module | Layer | What it has |
|---|---|---|
| `rhi` | base | `Device` (swapchain, frames in flight, `destroyLater`), VMA, `Buffer`, `Image`, `BindlessTable`, `GraphicsPipeline`, `UploadRing`, `CommandList` |
| `render` | systems | `Renderer` (frame flow, textures, depth target), `Drawer`, `Camera`, `QuadBatch`, `ScreenFade`, `ModelRenderer`, `MapRenderer` (field maps), `SceneGrade` |
| `apps/game` | apps | Puts it all together: the field, battle and the screens; the test scene (`TestScene`) stays as a development view |

Vulkan calls are only in `rhi`. `render`, `ui` and the others use its types (`rhi::CommandList`, `rhi::Buffer`...),
never `vk*` functions. The exception is `debug`, which initializes Dear ImGui with the native handles.

## Models inside the UI

`Renderer::createSceneTexture(extent)` creates a sampleable color compatible with the scene pipelines and an
independent depth. `createScreenSceneTexture(shape)` does the same for a scene that shows a whole original screen
(320×240, the 3D figures of the menus: the player, the partner, the medal): each frame it draws it at the window's
size (`screenTargetExtent` of `<dwe/render/screen_target.hpp>`: the window's height and the width from the shape), and
when the window changes it rebuilds it with the same identifier (the UI keeps it), waiting for the GPU and pointing
the bindless table entry at the new image (`BindlessTable::replace`). This way the figure stays sharp at any size;
before, it was drawn at 320×240 and the page enlarged it, pixelated. `FrameDesc::textureScenes` receives the
identifier and its drawers: they are prepared before the uploads are applied and drawn before the window's passes.
RHI synchronizes the transition from texture read to attachment and back; each use clears transparent color and
depth.

`UiRenderer::bindTexture("runtime/name", id)` allows using `<img src="/runtime/name"/>` without copying pixels to the
CPU. It registers the loan before loading the document: RmlUi knows the dimensions and draws the image among its other
elements, applying its clipping. The owner keeps the texture until the UI is destroyed; releasing it from RmlUi does
not destroy the resource. Bindings are not replaced, because the documents keep the images in cache. A `runtime/`
image without a binding (the portrait could not be created, or a test without a renderer) loads as a transparent
pixel: it is not seen and RmlUi does not warn that it could not load it. The content must respect the UI's
premultiplied alpha; the opaque parts of a model already have alpha one.

The partner card uses this mechanism with a model independent of the field actor, its raising placement and a fixed
pose. Opening the card does not change the world actor's position, rotation or animation. The target is only drawn
while Status is visible. It is released with `destroyTexture`, after closing the UI and before destroying the
Renderer.

## Frame flow

`Renderer::renderFrame(FrameDesc)` does, in order:

1. `Device::beginFrame()`: waits for the previous frame of the same slot and acquires the swapchain image. If there
   is none (minimized window, freshly rebuilt swapchain) it returns `false` and draws nothing.
2. Resets that slot's chunk of the upload ring.
3. Calls `Drawer::prepare` on each drawer of `FrameDesc::scene` and `FrameDesc::ui`, outside the passes.
4. Records the queued texture uploads (`queueTexture`, `queueTextureUpdate`) that fit in the ring, with their
   barriers, in the frame's command buffer.
5. Rebuilds the depth target if the window changed size.
6. **Scene pass** (`Pass::Scene`): swapchain color (cleared with `clearColor`) + depth (cleared to 1). It calls each
   `Drawer` of `FrameDesc::scene`.
7. If the frame carries `FrameDesc::post`, the scene of step 6 was drawn into a window-sized texture (the renderer
   rebuilds it when the size changes) and the **post pass** (`Pass::Post`) draws it onto the swapchain with that
   drawer, which reads it with `DrawContext::sceneColor()`. Without `post` the scene goes straight to the swapchain,
   as always. Color barrier between passes.
8. **UI pass** (`Pass::Ui`): the same color, loaded, without depth. It calls each `Drawer` of `FrameDesc::ui` (the
   map-change fade, RmlUi) and then `FrameDesc::overlay` with the raw `rhi::Frame` (the ImGui overlay). The overlay
   is always the last thing recorded: it can change any state (pipeline, descriptor set) without restoring it.
9. It closes the upload ring and `Device::endFrame()` presents.

Before each `Drawer` the viewport and scissor are the whole target. A drawer can change them; `setScissor` clips the
rectangle to the target, so the off-screen clips that RmlUi gives are fine. When binding a pipeline with dynamic depth
bias the bias goes back to 0: it is set with `setDepthBias` after `bindPipeline`.

## Resources and ownership

- **Owned values** (`rhi::Buffer`, `rhi::Image`, `rhi::GraphicsPipeline`, `rhi::UploadRing`): they are moved, not
  copied. When destroyed they send their Vulkan objects to `Device::destroyLater`, which destroys them when the
  frames that could use them finish. They can be released at any time, but must never outlive the `Device`.
- **Textures**: owned by the `Renderer`. `createTexture` returns a `TextureId`, a value with the index in the bindless
  table (what the shader receives) and a generation; `destroyTexture` releases it and its index is reused when the
  frames in flight finish. A destroyed id stops being valid even if its index is reused: destroying it again or
  updating it does nothing (it warns in the log). Whoever creates a texture destroys it. `whiteTexture()` is a 1×1
  white texture for drawing without a texture.
- **Drawers**: owned by whoever creates them (the map, model or UI code); the `Renderer` only borrows them during
  `renderFrame`.
- **Destruction order**: drawers and textures → `Renderer` → `Device`.

## Shaders

In Slang, compiled to SPIR-V in the build and embedded in the executable
([ADR 0018](../adr/0018-slang-shaders-with-vcpkg.md)).

1. Write `<module>/shaders/<name>.slang` with a `[shader("vertex")] vertexMain` and a
   `[shader("fragment")] fragmentMain` (the default names in `GraphicsPipelineDesc`).
2. In the module's `CMakeLists.txt`: `dwe_add_shaders(dwe_<module> FILES shaders/<name>.slang)`.
3. In C++: `#include <dwe/shaders/<name>.hpp>` and use `dwe::shaders::<Name>` (the name in PascalCase:
   `skinned_model.slang` → `SkinnedModel`) as `GraphicsPipelineDesc::spirv`.

Shader conventions:

- `import dwe.bindless;` gives `gTextures[]`, `gSamplers[4]` and `sampleTexture(texture, sampler, uv)`. The samplers
  are `SamplerNearestClamp`, `SamplerNearestRepeat`, `SamplerLinearClamp` and `SamplerLinearRepeat`, in the order of
  `rhi::SamplerKind`.
- Push constants: a struct with `[[vk::push_constant]]`, at most 128 bytes (`rhi::PushConstantBytes`), with std430
  layout. Its C++ mirror must have the same offsets.
- Vertex inputs with `[[vk::location(n)]]`, which match `rhi::VertexAttribute::location`.
- Matrices: `float4x4` in column-major, the same as GLM; `mul(m, v)` is GLM's `m * v`.
- Large per-frame data (bone matrices): a pointer in the push constants (`float4x4* joints;`) to an allocation in the
  ring (`UploadAllocation::address`).

## Pipelines

```cpp
auto pipeline = renderer.createPipeline(
    {.spirv = dwe::shaders::SkinnedModel,
     .vertex = {.stride = sizeof(Vertex), .attributes = Attributes},
     .cull = rhi::CullMode::Back,
     .depth = {.test = true, .write = true},
     .blend = rhi::BlendMode::Opaque,
     .name = "skinned model"},
    render::Pass::Scene);
```

`createPipeline` fills in the pass's color and depth formats. A pipeline is only good for its pass (the UI one has no
depth). Viewport and scissor are dynamic; the depth bias too if `DepthState::bias` is on. Blend modes: `Opaque`,
`Alpha`, `PremultipliedAlpha` (RmlUi), `Additive` and `Subtractive`. The original's four semitransparency modes come
from them: B/2+F/2 = `Alpha` with alpha 0.5; B+F = `Additive` with alpha 1; B−F = `Subtractive` with alpha 1;
B+F/4 = `Additive` with alpha 0.25. `rhi::validate(desc)` checks the description without a GPU.

`FrontFace` follows Vulkan's definition. Since `render::perspective` flips the clip space's Y, the result is that of
OpenGL: counterclockwise faces seen from the front (glTF's) are `FrontFace::CounterClockwise`.

## Light and color pass

Optional field improvements, none with an equivalent in the original or with any effect on rules, collision or random
numbers (options and texts: [game.md](../modules/apps/game.md#shadows-light-and-vivid-colors)).

- **Shadows.** The round one is the original's (`drop_shadow.c: renderDropShadow`): a flat billboard of 2 × the
  species' radius under the feet, SPOT256 (uv 0x40, 0x80, 63 × 63) subtracted with 0x30/128.
  `MapRenderer::groundDrawer()` draws them right after the background and before the models, as the original does in
  its 0xFFD entry of the ordering table: if they were layer billboards (after the models), the depth would darken the
  legs. The sun's is another, longer blotch (`sunShadow`) that leaves the feet in the direction opposite the sun:
  `sunAt` moves the sun from +x at the start of the day to −x at the start of the night, higher at midday (shadow of
  1.3 radii) and low at the extremes (up to 3.7), with strength 0 at night and on maps without a time and ×0.65 with
  the dusk palette. Flat billboards now rotate around the vertical axis (`MapBillboard::turn`), without changing the
  ones with turn 0.
- **Time-of-day tint.** `daylightAt` interpolates nine keys pinned to the schedule's hours (`TimeOfDaySchedule`): gold
  at dawn (1.06; 0.99; 0.89), neutral at midday, orange at dusk (1.07; 0.94; 0.87), blue at night (0.91; 0.96; 1.10).
  It is small on purpose: the background palettes already change with the time (map.slang) and the tint only leans
  toward where the paint already goes. Maps without a time (fixed time or a single palette: caves, houses) stay
  neutral and with the lamps on.
- **Map lights.** `field/lights` of the game package: per map, lights in background pixels (so they follow the painted
  lamp at any window size and camera offset) with radius, color, intensity and type: `fire` (always, stronger at
  night, flickers with three sines), `lamp` (from dusk to dawn; always on maps without a time) or `glow` (always, slow
  pulse). The flicker is a real-time wave, never from the game's generator. `windowLights` converts them to window
  pixels with the map's camera, removes the off-screen ones and keeps the 16 strongest. With a scene camera of its own
  they are not drawn (the background does not move with it).
- **Color pass** (`SceneGrade`, `shaders/scene_grade.slang`): a triangle covering the window reads the scene texel by
  texel, multiplies it by the tint, adds the lights (they mostly brighten by multiplying, so a dark wall stays dark,
  and add a small glow of their own), applies vibrance (muted colors gain more saturation than vivid ones), a soft
  contrast toward an S curve that keeps the black and the white, and a glow with four nearby texels above 0.72. With
  "Vivid colours": vibrance 0.3, contrast 0.12, glow 0.22. The screen fade and the UI go in the UI pass, afterward:
  they are never touched.

Cost on llvmpipe at 1280 × 720 (measured with other agents loading the machine, load ~20 on 12 cores, in pairs
launched at the same time to equalize the load): with the three options off the frame costs the same as before the
change; with everything on, around 25 % more frame time (e.g. 67 → 85 ms and 77 → 96 ms in twna01 at night). The
first version, with linear sampling and eight glow samples, cost double.

## Drawing: `Drawer`

```cpp
class MapDrawer final : public render::Drawer {
    void draw(render::DrawContext& context) override {
        auto joints = context.upload().upload(std::span<const glm::mat4>{matrices});  // valid this frame
        context.commands().bindPipeline(pipeline_);
        context.commands().bindVertexBuffer(vertices_);
        context.commands().bindIndexBuffer(indices_, rhi::IndexType::Uint16);
        context.commands().pushConstants(Push{.joints = joints->address, .texture = texture_.index});
        context.commands().drawIndexed(indexCount_);
    }
};
```

`Drawer::prepare(PrepareContext&)` is optional and runs before the passes: this is where textures are created or
updated with `queueTexture`/`queueTextureUpdate` to be drawn in that same frame. A UI library that creates textures
and geometry while "rendering" (RmlUi) does it in `prepare` and replays the stored draws in `draw`.

`DrawContext` brings the `CommandList` (`commands()`), the `UploadRing` (`upload()`), the pass (`pass()`) and the
target's size in pixels (`extent()`). The drawers are passed to the frame in `FrameDesc::scene` or `FrameDesc::ui`, in
the order in which they must be drawn. When filling in `FrameDesc` all its fields have to be named (an empty span if a
pass draws nothing): the build warns about the missing ones.

## Meshes, textures and per-frame data

There are two moments: **loading**, where it is fine to wait for the GPU, and **the game running**, where nothing
should wait.

| What | When loading (waits for the GPU) | With the game running (no waiting) |
|---|---|---|
| Static mesh | `rhi::Buffer::createWithData(device, BufferUsage::Vertex, bytes)` (GPU memory) | `rhi::Buffer::createMapped(device, usage, bytes)` (host memory, drawable immediately) |
| New texture | `renderer.createTexture(extent, rgba)` | `renderer.queueTexture(extent, rgba)` |
| Change a region | `renderer.updateTexture(id, rect, rgba)` | `renderer.queueTextureUpdate(id, rect, rgba)` |
| Per-frame data | — | `context.upload().upload(span)` or `allocate(bytes)` in `prepare`/`draw` |

- Textures: 8-bit RGBA, rows from the top; no pixels = transparent.
- The queued ones return the id immediately and keep a copy of the pixels. They are recorded at the start of the next
  frame, before its passes: calls between frames or in `prepare` are seen in that frame; calls in `draw`, in the next
  one (a texture created in `draw` is not drawn until then). They are applied in order; if one does not fit in that
  frame's ring it waits for the next one along with those that come after it. A single upload cannot exceed
  `RendererDesc::uploadBytesPerFrame`.
- The ring (16 MiB per frame by default, aligned to 16) only exists during a frame: the `Renderer` gives it to the
  drawers in `prepare` and `draw`; allocating outside the frame aborts. If it fills up, it returns empty and logs it.

## Skinned meshes

Convention for the model track: **one interleaved vertex buffer** (binding 0, `VertexLayout::stride`) with `position`
`Float3`, `normal` `Float3`, `uv` `Float2`, `joints` `Uint8x4` or `Uint16x4` and `weights` `Unorm8x4`, `Unorm16x4` or
`Float4`, plus an index buffer. The models add `color` `Float4` (location 3, the vertex color) and keep vertices and
indices in a single buffer, with the indices after the vertices. Skinning is done in the vertex shader with the frame's
bone matrices in the ring, read through a pointer (`float4x4* joints` in push constants). We use neither several
bindings nor vertex pulling: one binding is enough while the animation is on the CPU and it lets `rhi::validate` check
the layout.

## Camera and spaces

- `render::Camera`: `view`, `projection`, `viewport`; `viewProjection()`.
- `render::perspective(fovY, aspect, near, far)`: right-handed projection with depth 0..1 and Y flipped for Vulkan's
  clip space. The world is that of [world-space.md](world-space.md) (+Y up).
- `render::pixelProjection(extent)`: pixels with origin at the top left → clip space, for 2D.
- `render::QuadBatch`: textured rectangles in pixels, in a single draw, in either of the two passes.
- `render::ScreenFade`: subtracts a gray from the whole target (`Subtractive` blend, background − gray) over what was
  drawn before, like the original's fade rectangle (`fade.c: renderFade`); it does not blend toward black. The game
  puts it first in `FrameDesc::ui`: it subtracts from the scene and the UI stays on top (the map name in front of the
  fade).
- `render::MapCamera` and `render::MapRenderer`: a field map's fixed camera, with an off-center projection over its
  background, and the drawing of the background and its depth layers
  ([ADR 0020](../adr/0020-map-camera-and-depth-layers.md), [module page](../modules/engine/render.md)).

## Samplers

`rhi::SamplerKind`: `NearestClamp`, `NearestRepeat` (sharp pixels, nearest mip) and `LinearClamp`, `LinearRepeat`
(bilinear filtering, mip blending and 16× anisotropy or the GPU's maximum). Textures created with `mipmapped` (those
of the models with `texture_filter` at `smooth` or `upscaled`) have their whole chain; the others, a single level.

## Sharpness: edge smoothing and model textures

Two optional improvements, on by default, that only change the image's definition (the reasons and the measurements
are in [Sharpness](remaster-pipeline.md#sharpness)):

- **Edge smoothing** (`antialiasing`): `Renderer::setMultisampling` draws the scene pass into a color and a depth of 4
  samples per pixel (`MsaaSamples`; Vulkan requires accepting 4 on color and depth, so no other path is needed) and
  averages them at the end of the pass into the window or into the light pass's texture. Each `Pass::Scene` pipeline is
  created with its multisampled variant. The own scenes (`textureScenes`: the menus' portraits and figures) are
  smoothed the same way, with their 4-sample color and depth resolved into their texture; the HD background pass
  (`keep`, draws over what it had) stays at one sample. The fragment shader runs once per pixel: only the triangles'
  edges are smoothed (the outlines of the models and of the quads), and what a shader discards (the transparent
  texels, the layers' mask) is still the original's pixel.
- **Model textures** (`texture_filter`): `ModelRenderer::setTextureFilter`. `Nearest` is the original's texel;
  `Smooth` filters with mipmaps and anisotropy (`FlagSmooth` in `skinned_model.slang`), and first fills the color of
  the transparent texels with that of their drawn neighbors so that the filtering does not bring in a dark edge;
  `Upscaled`, the default, filters the textures doubled when the model is uploaded by the engine's scaler
  (`upscaleRgba`, [Sharpness](remaster-pipeline.md#sharpness)), with the alpha interpolated: alpha cutouts give smooth
  outlines. The textures that are rewritten in game (`updateImage`, the faces) are doubled the same way. Changing the
  filter re-uploads each model's textures.

## Models

`render::ModelRenderer` uploads each `assets::ModelData` once and draws the instances sent to it each frame with their
bone matrices (from `animation`) in the ring. It provides two drawers for `FrameDesc::scene`: `opaque()` (what is
opaque and what is alpha-cut) and `translucent()` (what is semitransparent, without writing depth). The game scene's
order is **background → opaque models → map layers → semitransparent models**: the map layers cover the opaque models
by depth, and what is semitransparent blends over everything opaque. `opaque()` always goes first: its `prepare` takes
the instances and uploads the matrices, and `translucent()` draws from that; on its own, `translucent()` draws
nothing. What is semitransparent is sorted **per instance** (the view-space depth of its origin), not per triangle.
How it paints light, transparency and faces is in [models](models.md#drawing-modelrenderer).

## GPU requirements

Vulkan 1.3 with dynamic rendering and synchronization2; from 1.2, descriptor indexing (runtime array, partially bound,
update-after-bind and update-unused-while-pending of images, non-uniform indexing), buffer device address and separate
depth layouts; anisotropic filtering ([ADR 0019](../adr/0019-bindless-descriptors.md)). Swapchain `B8G8R8A8_UNORM` or
`R8G8B8A8_UNORM`, with blending in gamma space. Depth: `D32_SFLOAT`, or `X8_D24` or `D16` if there is none.

To test the life of the textures, `dwe-game --texture-churn` creates and destroys a texture on each frame (the little
square under the orange bar); with the validation layer, no `vulkan:` message should appear.

## vk-bootstrap uses the linked loader

Each `vkb::InstanceBuilder` (and `vkb::SystemInfo::get_system_info`) receives the `vkGetInstanceProcAddr` of the
Vulkan loader that the programs link (`vulkanEntry`, `packages/engine/rhi/src/renderer_requirements.hpp`). Without it,
vk-bootstrap opens a loader by its bare name (`dlopen("libvulkan.dylib")`). On macOS, dyld looks for that name only in
its fallback folders, never inside the application bundle, so it picked up another Vulkan library from the system (or
none). The linked loader then rejected the instance that library created: the first test of the Intel package in CI
ended with `vkEnumeratePhysicalDevices: Invalid instance`. On Linux and Windows the bare name already gave the linked
loader; now it is the same path on all three systems.

## AddressSanitizer and vk-bootstrap

vk-bootstrap is a vcpkg C++ library built without AddressSanitizer. When linking it, its copies of libc++ templates
(the growth of `std::vector<std::string>`, for example) can win over the project's instrumented copies, or the other
way around; mixing the two leaves the containers' annotations out of sync and ASan reports container overflows that
do not exist (it happened in `assets::DiskFileSystem::listFiles`). Changing the link order only moves the problem to
vk-bootstrap's code, and instrumenting it would require another vcpkg triplet.

That is why `rhi/src/device.cpp` defines `__asan_default_options` with `detect_container_overflow=0` in ASan builds:
only the executables that link `Device` (and with it vk-bootstrap) carry it, that is, the game and the `render` tests.
The other suites keep the detection. `ASAN_OPTIONS` can turn it back on. `render/tests/link_test.cpp` reproduces the
case with the bad link order.
