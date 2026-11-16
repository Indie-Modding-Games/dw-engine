# render

The game's render, on top of `rhi`. See [render.md](../../architecture/render.md).

- `Renderer`: owner of the bindless table, the upload ring, the depth target (which follows the window's size)
  and the textures (`TextureId`). `renderFrame` draws a frame: scene with depth → UI → debug overlay.
  `createPipeline` makes pipelines for a pass. Textures are created and updated waiting
  (`createTexture`, `updateTexture`, at load) or queued for the next frame (`queueTexture`,
  `queueTextureUpdate`, with the game running). Queued copies leave a quarter of the frame's upload memory free
  (`DrawUploadShare`, `textureCopyFits` in `<dwe/render/upload_budget.hpp>`): the passes upload their
  vertices and matrices after them, and before, a freshly loaded map filled the 16 MiB with its textures and the
  first frame could not upload the map's vertices ("upload ring is out of room"). What does not fit waits for the next
  frame; a copy larger than what is left without the reserve goes alone, the first of its frame.
  `textureWritePending` says whether a texture still has a copy waiting. `setMultisampling` draws the scene
  pass with 4 samples per pixel (the `antialiasing` setting; [sharpness](../../architecture/render.md#sharpness-edge-smoothing-and-model-textures)).
- `FrameDesc::textureScenes`: scenes that are drawn into a texture of their own (`createSceneTexture`) before the window
  passes; with `TextureScene::keep` it is drawn over what the texture had instead of clearing it. With
  `createScreenSceneTexture` the texture follows the window's size (`screenTargetExtent`, same identifier). With
  edge smoothing on, the ones that do not use `keep` are drawn with 4 samples per pixel.
- `Drawer`, `PrepareContext` and `DrawContext`: the interface through which the map, the models and the UI prepare their data
  before the passes and record their draws in a pass.
- `Camera`, `perspective`, `pixelProjection`: matrices for Vulkan's clip space (Y down, depth 0..1).
- `ModelRenderer`: skinned models (`assets::ModelData`), uploaded once and drawn N times per frame with GPU
  skinning, the original's light and sorted semi-transparencies, with one drawer for opaque and another for
  semi-transparent. `SlotMap` keeps its models behind handles with a generation. See [models](../../architecture/models.md).
  `setTextureFilter` chooses how its textures are sampled (`TextureFilter`: `Nearest`, `Smooth`, `Upscaled`, the
  `texture_filter` setting). With `setUpscaleInBackground` (the status portrait and the field's characters) a model
  uploads its smoothed textures right away and doubles them in a thread of its own (`TextureUpscaleJob`,
  `texture_upscale_job.hpp`); `pollTextures` swaps them for the doubled ones when they are ready, and a region that
  `updateImage` changes in the meantime is uploaded again on top. Without it, `add` doubles them on the spot.
- `scenery_upscale.hpp`: the engine's ×2 scaler (`upscaleScenery` for the background in all its variants,
  `upscaleRgba` for textures with alpha) and `upscale_cache.hpp` (`UpscaleCache`, `UpscaleKey`): what it makes is kept
  compressed with zstd in `<user data>/cache/upscale`. See [Sharpness](../../architecture/remaster-pipeline.md#sharpness).
- `QuadBatch`: textured rectangles in pixels in a single draw (sprites, minimap, debug images).
- `ScreenFade`: subtracts a gray from the whole screen over what was drawn before (`Subtractive` blend), like the map-change
  fade of the original (`fade.c: renderFade`); `setWhiteness` adds the same gray (`Additive` blend,
  `FADE_MODE` 1) for the story's fade to white.

- `SceneGrade` (`<dwe/render/scene_grade.hpp>`): the field's light and color step (an improvement, an option): it draws the scene
  to the window with the time-of-day tint, point lights, vibrance, contrast and brightness, as `FrameDesc::post`
  (`Pass::Post`), before the UI. `scene_light.hpp`: the per-map light table (`parseMapLights`), the tint by hour
  (`daylightAt`), the sun for shadows (`sunAt`) and the intensity of each light (`lightStrength`). `ground_shadows.hpp`:
  the original's round shadow (`roundShadow`) and the sun's (`sunShadow`), billboards that `MapRenderer` draws with
  `groundDrawer()` between the background and the models. See [render.md](../../architecture/render.md#light-and-color-pass).

Its shaders are in `shaders/` and are compiled with `dwe_add_shaders` ([ADR 0018](../../adr/0018-slang-shaders-with-vcpkg.md)).

## Field maps

How the original draws a map is in [Maps: how they are drawn](../../formats/maps.md#how-it-is-drawn); the
policy for any resolution, in [ADR 0020](../../adr/0020-map-camera-and-depth-layers.md).

- `MapCamera` (`<dwe/render/map_camera.hpp>`, CPU only): the map's fixed camera and an off-center perspective
  projection with the focal length `H` in background pixels and the optical center at the center of the background. With
  `setViewport` and `follow` it gives the view, the projection (Vulkan depth, 0 at the near plane of 100 and 1 at
  65,536) and the window of the background that is seen: scale `min(width / 320, height / 240)`, centered on the player and clamped to
  the edges, with bands if the background is smaller than the window (`backgroundRect`). The helpers go from the world to the
  background and to the window (`projectToBackground`, `worldToWindow`), from the window to the ground (`pickGround`, for click
  to move) and from a background pixel with a depth to clip (`backgroundToClip`, for the background and the depth
  layers). `mapCameraDesc` converts the map's raw camera (doubling viewpoint and reference, like
  `loadMapSetup`). `setShake` moves the background window a few pixels in x after clamping it, for the story's screen
  shake (`map.c: tickMeramonShake`, which moves `DRAWING_OFFSET_X`).
- The tests compare the projections with the original's fixed-point calculation (4.12 rotation, RTPS, division by
  `SZ` and `cameraX` clamped to the edges) on real maps: they agree to a pixel and a quarter on the background.
- `loadMapSceneData` (`<dwe/render/map_scene.hpp>`) reads a map by its id through the VFS: the `MapDocument`, its
  camera and its images (`assets::loadMapImages`: the background as indices with one palette per variant and the
  already-resolved sprite sheet). The original's VRAM rules (pages, CLUT rows, ordering limits) live in `assets`,
  not here ([ADR 0020](../../adr/0020-map-camera-and-depth-layers.md)). `timeOfDayAt` and
  `backgroundVariant` choose the variant like `updateTimeOfDay`.
- `MapDepthLayers` (`<dwe/render/map_layers.hpp>`): the depth layers. Each sprite instance takes its
  entry from the order table (`assets::mapSpriteOrderValue` with the camera's depth) and is drawn as a
  plane at `orderValue · 16` distance; their animations run at 20 frames per second, like the original's
  field. The draw order is computed once at load; `quads()` only fills a buffer that it keeps.
- `MapRenderer` (`<dwe/render/map_renderer.hpp>`): draws a map with all of the above in the scene pass. It uploads
  the background as indices at its native resolution, a palette texture with one row per variant and another for freezing
  an interrupted fade, and one texture per texture of the sprite sheet (at load, with `createTexture`: so
  they are there on the map's first frame without going through the upload memory),
  shared by all the hours: sprites with `clut` −1 are also indices and look up the hour's palette in the
  shader. `update` advances the animations and the hour fade (`setVariant`, 25 frames of the original; a
  change in the middle of a fade continues from the colors that are seen) and keeps the focus; the background's `Drawer` adjusts the
  camera to the frame size in `prepare`. It gives two `Drawer`s: the background (without depth, first) and the layers (the
  sprites from far to near with `LessOrEqual` depth test and their blending, and the black bands at the end). What
  is between the two (the models) uses `camera().view()` and `camera().projection()` and is covered by the layers that
  are in front of it. `setMarkers` draws test rectangles standing on the ground. `setBillboards` draws textured quads (from `addBillboardTexture`, which last between maps) standing and facing the camera or lying on the ground, opaque or semi-transparent like the layer sprites: ground objects and their shadows (`item.c: renderDroppedItem`). Drawing does not allocate memory: the
  buffers of each frame are reused. `setGrade` (`MapGrade`, an improvement for the seasons of
  [packages/game/weather](../game/weather.md#seasons)) colors the background, the layers and the quads in the shader: greens
  turn toward another hue, then saturation, per-channel gain and `lift`; the bands never. Neutral by default: the
  colors come out as they are.
- `MapRenderer::setHdScenery` (on by default): the background and the layers are filtered in `map.slang` following
  the edges at the window's resolution (`SamplingSmooth`); the background and the sprites that follow the hour are read
  as RGBA textures with the palette that is seen (`resolveBackgroundColors`, redone only when the palette changes).
  `hdSceneryScene()` gives the background pass for `FrameDesc::textureScenes`: it keeps the filtered background in a texture
  the size of the window that wraps around as the camera scrolls (`<dwe/render/hd_scenery_wrap.hpp>`), so
  each frame filters only the strip that the scrolling uncovers, and the whole thing when the colors, the window size
  or the map change. The background (`SamplingScreen`) and the cutouts (`SamplingCutoutScreen`) read it with a single read
  per pixel; without the pass they filter on the fly, with the same result. Off, each quad uses `SamplingSharp` and the
  image is exactly the original's. See [High definition in the engine](../../architecture/remaster-pipeline.md#high-definition-in-the-engine).
  `setSceneryQuality` (`SceneryQuality::Upscaled` by default, the `scenery_quality` setting): the background pass
  resamples the background doubled by `upscaleScenery` (`SamplingUpscaledPrefilter`), made in a thread when the map loads
  and kept in the cache (`setUpscaleCache`); until it is uploaded, and if it cannot be made, the usual
  filter is used. See [Sharpness](../../architecture/remaster-pipeline.md#sharpness).
- `MapCutouts` (`<dwe/render/map_cutouts.hpp>`, CPU only): says, per sprite and position, whether a layer sprite is
  an exact cutout of the background (`clut` −1, whole pixel, each texel drawn equal to the background index underneath). The
  cutouts are drawn in `SamplingCutout` (or `SamplingCutoutScreen`): their mask and class are the sprite's, the
  color the filtered background's.
- `dwe-game --map <id>[,<id>...]` shows it without the field scene: `--time day|dusk|night`, `--focus x,z` and
  `--size WidthxHeight`; the arrows move a camera-relative marker, a click sends it to the ground point, the
  actions `debug_time_day`, `debug_time_dusk` and `debug_time_night` (1, 2 and 3) change the hour and `debug_next_map` (N)
  moves to the next map in the list and logs how long the change took.
