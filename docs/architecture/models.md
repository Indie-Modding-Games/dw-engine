# Models and animation in the engine

How the engine loads the models that `dwx` extracts (one `.glb` per model, see [models](../formats/models.md#gltf)),
how it plays their animations, and which model and which clips the player and the partner use. The underlying
decisions are in [ADR 0021](../adr/0021-cpu-animation-and-gpu-skinning.md).

| Piece | Module | What it does |
|---|---|---|
| `assets::ModelData`, `parseModel`, `loadModel` | `assets` | Reads a `.glb` by asset id into validated CPU data |
| `assets::CharacterModel`, `loadCharacterModel` | `assets` | Reads `models/<name>.yaml` from the `game` package: the model and the clip for each animation |
| `animation::AnimationPlayer` and `clip_sampling.hpp` | `animation` | Samples clips, separates root motion and computes the bone matrices |
| `ModelRenderer` | `render` | Uploads the models once and draws N instances per frame with the map's camera |

`animation` is a module of the systems layer that only depends on `core` and `assets`: the field
(`packages/game/field`) can ask it for step speeds or root motion without depending on `render`, and `render` only
receives matrices.

## `ModelData`

`parseModel` accepts a binary glTF with the buffer and the images inside, as `dwx` writes them, and checks it with
`cgltf_validate` and with its own rules. What it does not know how to draw is an error with the reason, never a
half-loaded model:

- **Nodes:** name, parent and rest pose (translation, rotation, scale). A node with a matrix, or with a number that is
  not finite, is an error. `nodeOrder` lists the nodes with each parent before its children.
- **Primitives:** triangle lists only. Each vertex (`ModelVertex`) carries position, normal, uv, color, four bones and
  four weights; what the primitive does not have takes a neutral value and `hasNormals`, `hasUvs` and `hasColors` say
  what was there. Indices are checked against the vertices, bones against the skin, and the weights have to sum to 1
  (they are normalized).
- **Vertex color:** stored as is. With a texture, the extractor has already converted the original's 128 to 1.0, so
  the color multiplies the texel and can reach 2; without a texture it is the color.
- **Materials:** base color, image (index into `images`, PNG decoded to RGBA8), `alphaMode` (`Opaque`, `Mask` with its
  `alphaCutoff`, `Blend`), `unlit` (`KHR_materials_unlit`), `doubleSided` and `blending`, the original's
  semitransparency mode (`extras.blending`: average, add, subtract or add a quarter). A primitive without a material
  uses a default one.
- **Skin:** at most one, with its bones and its inverse bind matrices. A model with a skin can only have skinned
  meshes (the mesh node's transform does not count, as glTF says); one without a skin carries each mesh already placed
  with its node's transform.
- **Clips (`AnimationClip`):** channels per node (translation, rotation as a quaternion `x y z w`, scale), with `LINEAR`
  or `STEP` interpolation (`CUBICSPLINE` is an error and morph weights are ignored), and the `extras` from `dwx`:
  `id`, `ticksPerSecond`, `tickCount`, `frameCount`, `endsInLoop`, `loops` and `rootMotionNode`. A clip that ends in a
  loop carries `repeat`: the last loop in seconds (`start` = `firstTick`, `end` = `lastTick`, `gap` = one tick, the
  return step). A loop outside the clip's ticks, or a looping clip whose last loop does not end on its last tick, is
  an error. `AnimationLoop::count` says how many times the loop runs in total, or is empty if it is forever: the
  original's `0xFF` only exists in the `extras` that `dwx` writes, and is translated on reading. A loop can start at
  tick 0 (the whole clip); then there is no tick before its start and the return step does not move the root.
- **Limits:** 2^20 vertices, 2^22 indices, 1024 nodes, 2^22 keyframes and 2^24 image pixels (64 MiB in RGBA8, counted
  with the PNG headers before decoding any) per model. Every accessor that is read (attributes, indices, bind
  matrices, keyframes) has to be in a buffer view: glTF lets an accessor without one be worth `count` zeros, and a
  hostile file would thus ask for endless memory. Mods are untrusted content.

`findClip(id)` looks up by the original's id (the one in the scripts and the YAML) and `findClip(name)` by name
(`anim_NN`).

## Playing clips

The functions in `clip_sampling.hpp` are pure and the `AnimationPlayer` uses them per instance:

- **Time.** `locate` turns the time since the clip started into a moment among its frames. A clip without `repeat`
  stays in its last pose. One with `repeat` goes from 0 to `end` and then loops: after `end` comes `start`, and on the
  tick between the two the pose blends the one at `end` with the one at `start`, like the original, which jumps from
  the loop's last frame to the first in one tick. Loops with a count already come unrolled in the frames
  ([models](../formats/models.md#loops-and-limits)).
- **Sampling.** A clip sampled at tick N gives exactly glTF frame N; between frames it interpolates linearly
  (rotations with slerp along the short path).
- **Root motion.** The translation of the `rootMotionNode` node (bone 0) does not enter the pose: it is the entity's
  movement. `rootMotion(t)` gives how far it has moved since the start, adding each loop lap (one lap moves what goes
  from the tick before `firstTick` to `lastTick`), in model space as if the entity were looking in its rest
  direction. `AnimationPlayer::advance` returns that step's movement; the field decides whether to use it or to move
  the entity with its own speeds.
- **Step speed.** `repeatGroundSpeed` divides how far a lap advances by how long it lasts: the ground speed at which
  the feet do not slide. If the field moves at another speed, it plays the clip with
  `speed = field speed / repeatGroundSpeed`.
- **Fade.** `play(clip, fadeSeconds)` blends from the current pose for that time. The original does not fade; it
  defaults to 0, and the field does not fade either (player, partner and NPCs switch clips abruptly, like the
  original). A clip from another model is rejected (`play` returns `false`).
- **Matrices.** `computeJointMatrices` gives, per bone of the skin, its transform in model space times its inverse
  bind matrix. This is what `render` uploads per instance for GPU skinning. It uses a workspace supplied by the caller
  (the player keeps its own), so playing does not allocate memory every frame.

## Player and partner

The data is in the `game` package (`content/packs/game/models/`, schema
[`character_model.schema.json`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/character_model.schema.json)):

```yaml
model: digimon/boys/model
animations:
  idle: 0
  walk: 2
  run: 3
```

The field asks for animations by name (`idle`, `walk`, `run`) and `CharacterModel::clipOf` gives the clip id.
`checkCharacterModel` checks that the model has all of them.

| Character | Model | Animation → clip | Where it comes from (decomp) |
|---|---|---|---|
| Player (`models/tamer`) | `digimon/boys/model` (type 0) | `idle` 0, `walk` 2, `run` 3 | `tamer.c: tamerTickWalkingState`: with a direction pressed it sets 3; if R1 or cancel is also held, 2; with no direction, 0 |
| Partner (`models/partner`) | `digimon/agum/model` (type 3) | `idle` 0, `idle_tired` 1, `walk` 2, `walk_tired` 3, `run` 4 | `main.c`: a new game creates the type 3 partner (or 0x11 with a flag); `partner.c: partnerTickWalking` uses 4 when it falls behind, `setPartnerSlowWalking` 2 (3 if tired or sick) and `setPartnerIdle` 0 (1 if tired or sick) |

The partner's ids are the same for any Digimon: changing partner is changing `model`. This is what the game does when
the partner changes species (evolution, virus form or rebirth, and when loading a save):
`FieldScene::showPartnerSpecies` draws it with `digimon/<species>/model` from the base package and the animations of
`models/partner` (see the [apps/game page](../modules/apps/game.md)).

In the original, movement comes from the animation (root motion). Measured on the extracted models (checked by the
`animation` test with `DWE_CONTENT`), in units per second (100 = one tile):

| Clip | Lap | Speed |
|---|---|---|
| Player `walk` (2) | 400 units in 20 ticks | 400 |
| Player `run` (3) | 600 units in 12 ticks | 1000 |
| Partner `walk` (2) | 360 units in 16 ticks | 450 |
| Partner `run` (4) | 540 units in 12 ticks | 900 |

The player runs by default and walks with a held button: this is the original's behavior.

**With the field.** `packages/game/field` moves the entities with their own speeds
(`content/packs/game/field/rules.yaml`, the same models: `digimon/boys/model` and `digimon/agum/model`) and leaves a
`scene::AnimationIntent` on each one. `scene::motionName(intent.motion)` gives `idle`, `walk` or `run`, the names in
these YAML files, so the clip is `clipOf(motionName(motion))`: `models/tamer` for the entity with `PlayerTag` and
`models/partner` for the one with `PartnerTag`. So that the feet do not slide, the clip goes at
`speed = intent.speedScale × speed from rules.yaml / repeatGroundSpeed(clip)`. The player's speeds are the same (400
and 1000); the partner's in `rules.yaml` (452 and 870) are the average root motion of the whole clip, and those in the
table are for one loop lap (450 and 900), so the partner runs at 0.97.

The characters that some maps draw as prerendered sprites can be drawn with their species' model (the "3D characters"
option, `model_characters`; the list is in `field/sprite_characters.yaml` of the `game` package): see
[3D characters](../modules/apps/game.md#3d-characters).

## Drawing: `ModelRenderer`

`render::ModelRenderer` draws in the scene pass ([render](render.md)) with two drawers, `opaque()` and
`translucent()`. `render` depends on `assets` (it reads `ModelData`) but not on `animation`: it receives matrices.

```cpp
auto models = render::ModelRenderer::create(renderer);           // 8 pipelines, once
auto handle = (*models)->add(modelData);                         // buffers and textures; ModelData can be dropped
animation::AnimationPlayer player{modelData};
player.play(*character.clipOf("walk"));
// Every frame:
(*models)->clear();                                              // before submitting the frame's instances
player.advance(seconds);
(*models)->setCamera(camera);                                    // render::Camera (the map's)
(*models)->setLighting(lighting);                                // ambient + 3 directional lights
(*models)->submit({.model = *handle, .world = transform, .joints = player.jointMatrices()});
// FrameDesc::scene = {background, &(*models)->opaque(), map layers, &(*models)->translucent()}
```

- **Upload.** `add` gathers the vertices (`ModelVertex` as is) and 32-bit indices of all the primitives into **one**
  buffer (vertices and the indices after them; a single `createWithData`, which waits for the GPU once: it is meant
  for loading) and queues one texture per image (`queueTexture`, without waiting). Called between frames, the model is
  drawn on the next one.
- **Handles.** `ModelHandle` is `{index, generation}` (a `SlotMap`, like `TextureId`): `remove` releases everything and
  bumps the slot's generation, so an old handle is rejected even if the slot already has another model (`submit`
  returns `false`, `jointCount` 0).
- **Instances.** `submit` copies the bone matrices already multiplied by the instance's transform; when empty, the
  model comes out in its rest pose (the nodes' rest pose times the inverse bind matrices). The opaque drawer's
  `prepare` takes what was submitted (and empties it), uploads one block per frame (camera and lights) and the
  matrices of **each instance separately** to the ring, and passes their addresses in the push constants (48 bytes).
  If the ring fills up, the instances that fit are drawn and the rest are not: it logs this as an error with the
  counts when it changes (not every frame), never silently. A frame that is not drawn (minimized window) does not call
  `prepare`, so the game calls `clear()` before submitting each frame. One draw per primitive and instance; the
  frame's lists are reused without allocating new memory.
- **Skinning.** In the vertex shader (`shaders/skinned_model.slang`): sum of the four bones' matrices times their
  weights. The extracted models are rigid (one bone with weight 1), but the shader does not assume it.
- **Light.** Texel × vertex color × base color, times `ambient + Σ max(n·−dir, 0) × color` of three directional
  lights, clamped to [0, 2] like the original (1 leaves the texel unchanged). `unlit` skips the light. A light with
  zero direction does not light and a zero-length normal (a bone scaled to zero) only receives the ambient, without
  NaN. `defaultModelLighting()` is a neutral light for tools; the field passes the map's lights, scaled by the time of
  day.
- **Transparency.** `Mask` discards below `alphaCutoff`. `Blend` is drawn by `translucent()`, after everything opaque
  (models and map layers), back to front **per instance**: the key is the view-space depth of the instance's origin
  (`(view × world[3]).z`, smaller = farther), and within an instance the primitives go in their order. With depth test
  and without writing it; the mode comes from `blending` (`materialBlend`: average = `Alpha` with alpha 0.5;
  add = `Additive`; subtract = `Subtractive`; add a quarter = `Additive` with alpha 0.25). Texels with alpha 0 are also
  discarded here.
- **Faces.** `doubleSided` draws without culling faces; otherwise back faces are culled (glTF is counterclockwise).

## Model viewer

`scripts/dev.sh run --content <extracted content> --model-viewer [id...]` mounts the content's packages plus the
`game` package and lines up each character (by default `models/tamer` and `models/partner`), once per animation, in
place (without root motion), with a rotating camera. It adapts to any window size. It puts the opaque drawer and then
the semitransparent one in `FrameDesc::scene`, calls `clear()` before submitting each frame and logs how long each
character's `add` takes.

## Tests

- `packages/engine/assets/tests/model_data_tests.cpp` builds a `.glb` by hand (`glb_builder.hpp`) with a skin, a
  texture, an unlit material, a clip like those from `dwx` and another with `STEP`, and checks each field and the
  errors (indices, bones, weights, loops, semitransparency). With `DWE_CONTENT` it loads **all** the extracted `.glb`
  files: 337 models, 4843 clips (2070 in an infinite loop), all with an id, 20 ticks per second and one frame per
  tick.
- `packages/engine/animation/tests/animation_tests.cpp` checks that tick N is frame N, that the loop returns correctly
  (including the return tick), that root motion keeps a constant pace through all the laps, the bone matrices, the
  fade, and that the `game` package's YAML files are read. With `DWE_CONTENT` it mounts the content with the `game`
  package and checks the table's speeds and one minute of each animation of the two characters.
- `packages/engine/render/tests/model_renderer_tests.cpp` checks the translation of materials to blend modes and the
  default light, and `slot_map_tests.cpp` the life of the handles (remove → add rejects the old handle): the
  `ModelRenderer` needs an `rhi::Device` and there is no GPU in Docker, so its add/remove/draw cycle is tested through
  the `SlotMap` that holds its models. Drawing is checked by hand with the viewer (without a GPU in Docker there are no
  reference captures yet).
