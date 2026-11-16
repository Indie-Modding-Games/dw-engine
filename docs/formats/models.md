# Models and animations

How `formats/model` reads the original's models (TMD), the model packages with animations (MMD), the skeletons
and the animations, and how it writes them to glTF. References are to files and functions of the decomp (`dw_decomp`).

All readers receive a `std::span<const std::byte>` and read with `ByteReader`
([Disc](disc.md#reading-bytes)): every offset, count and index is checked against the buffer, and a malformed
file gives an error with the reason.

## TMD

The PsyQ model format (`TMDModel` in `include/dw/model.h`). `readTmd` returns a `Tmd`.

| Offset | Field | Notes |
|---|---|---|
| 0x0 | `u32 id` | Always `0x41` |
| 0x4 | `u32 flags` | Bit 0: absolute offsets. All the original's TMDs have 0 (relative); with 1 it is an error |
| 0x8 | `u32 nobj` | Number of objects |
| 0xC | objects | `nobj` headers of 28 bytes |

Each object: `u32 vert_top, n_vert, normal_top, n_normal, primitive_top, n_primitive; s32 scale`. The `*_top` are
offsets from the start of the object table (0xC). The original turns them into pointers with `GsMapModelingData`.
`scale` is stored but not used: libgs ignores it. Vertices and normals are `SVECTOR` (3 × `s16` and padding, 8 bytes);
normals have a length of 4096.

### Primitives

Each primitive starts with a word `olen | ilen << 8 | flag << 16 | mode << 24`, followed by `ilen` words.

- `mode` = `001 IIP QUAD TME ABE TGE` for polygons: IIP = one normal or color per vertex, QUAD = four vertices,
  TME = textured, ABE = semi-transparent, TGE = no texture shading.
- `flag`: bit 0 = no light (LGT), bit 1 = two-sided (FCE), bit 2 = one color per vertex in lit untextured polygons
  (GRD).

The packet has, in this order:

1. With a texture, one word per vertex: `u v` and in the high half `CBA` (first vertex), `TSB` (second) or padding.
2. Colors `r g b`: none if it is lit and textured; one, or one per vertex if it has GRD (lit) or IIP (unlit).
3. Indices: lit and gouraud, one word per vertex `normal | vertex << 16`; lit and flat, `normal | v0 << 16`
   and then the other vertices two at a time; unlit, the vertices two at a time, without normals.

The reader decodes all polygon combinations (which is what `graphics.c: initializeGsTMDMap` registers and a bit
more). A line, a sprite or a packet whose `ilen` does not match its mode is skipped and noted in `Tmd::issues`, with
the object, the primitive, the mode, the flag and the length.

`CBA` is the palette position in VRAM (x / 16 in bits 0-5, y in bits 6-14). `TSB` is the texture page
(x / 64 in bits 0-3, y / 256 in bit 4, semi-transparency mode in bits 5-6, color depth in bits 7-8).

### Normals outside the object

libgs indexes normals without looking at `n_normal`: `GsSortObject4J` passes each primitive to its `GsTMDfast*`
function (`graphics.c` registers them in `GsFCALL4`, for example `GsTMDfastTG3L` for textured, gouraud, lit triangles),
and that function reads the normal at `normal_top + 8 · index`. A vertex index outside `n_vert` is an error.

One of the original's models, `CHDAT/MMD1/TANE.MMD`, has lit triangles (mode `0x34`, flag 0, so the console does read
their normals) that use more normals than their object says: object 0 says 22 and uses 49, and 1 says 22 and uses
29. The file stores the normals of all objects one after another, so the extra ones are normals of objects 1
and 2: well-formed unit vectors, but for other faces. Compared with the normal of the face that uses them, the
object's own point almost the same way and the borrowed ones have no relation to it. It is a flaw of the original model,
and the console draws it with that light.

The reader does the same as the console: it reads the extra normals from where they fall, as long as they are inside
the file.

- If each one is a whole normal of another object, it notes this in `Tmd::notes` (the extractor passes it to `notes`):
  the model is understood and converted whole, with the original's light.
- If any falls on bytes that are not normals of any object, it notes this in `Tmd::issues` (and the file is left
  partial).

### Front face

The original's front faces go clockwise (checked with the normals of all the MMDs on the disc). glTF
uses counterclockwise, so the writer reverses the order. A quad is the triangles (0, 1, 2) and (1, 3, 2).

## MMD

`CHDAT\MMDn\XXXX.MMD`: a model with its animations (`model.c: loadMMD`). The folder is `type / 30` and the
four-letter name comes from `PTR_DIGIMON_FILE_NAMES`. `readMmd` returns an `Mmd`.

| Offset | Field |
|---|---|
| 0x0 | `u32 tmdOffset` (8 in all) |
| 0x4 | `u32 animTableOffset` |
| `tmdOffset` | The TMD, up to `animTableOffset` |
| `animTableOffset` | Table of `u32`: offset of each animation from the start of the table; 0 = no animation |

The table does not say how many entries it has: it ends where the first stream starts, so the count is the smallest
non-zero offset divided by 4. The index in the table is the animation id that scripts use (`startAnimation`).
Each offset has to fall inside the file, after the table and at an even address; if all entries are 0
up to the end of the file, it is an error.

The texture is not in the MMD: it comes from `CHDAT\ALLTIM.TIM`, a 4-bit TIM per type
([formats](../architecture/formats.md#the-disc)). The original uploads it to a different place in VRAM depending on the slot
(player, partner or NPC) and rewrites the page and palette of each primitive when loading it
(`graphics.c: updateTMDTextureData`), but the files already point to the TIM's own position: page 5, which is its
`(320, 0)`, and palettes from `(0, 488)`. That is why the extractor does not have to correct anything: it uploads the TIM to its place and
each primitive finds its texels and its palette in it (see [glTF](#textures)).

## Skeletons

Skeletons are in the executable (`DIGIMON_SKELETONS`), an array of `SkeletonBone { s8 objIndex; s8 parentIndex; }`
per type, with as many bones as `DIGIMON_DATA[type].boneCount` and no terminator. `objIndex = -1` is a bone without a
mesh; `parentIndex = -1`, a root. Bone 0 is the entity's root and bone 1 the body's
(`model.c: initializeDigimonObject`).

`formats/model` does not read the executable: it receives the skeleton as a parameter. `validateSkeleton` checks that there is some
bone, that each parent comes before its children and that each object exists in the TMD.

## Animations

`simulateAnimation` reproduces an animation's stream exactly as the original does (`anim.c: startAnimation`,
`tickAnimation`, `tickMomentum`, `animHandleKeyFrameInstruction`, `animReadMomentumInstruction`, `animApplyMomentum`,
`applyRootMomentum`) and stores each bone's pose after every tick. It does not interpolate on its own: it simulates.

### Stream

Little-endian `s16` words:

1. Header: bit 15 = has scale, bits 0-14 = `frameCount`.
2. Initial pose of bones 1 to `boneCount - 1` (not 0): XYZ scale (only if there is scale; otherwise, 4096), XYZ
   rotation and XYZ position.
3. Instructions: the type in bits 12-15 and the frame at which it runs in bits 0-11.

| Type | Words | What it does |
|---|---|---|
| `0x0` keyframe | entries while the word has bit 15 | Each entry: `bone` (bits 0-5) and channel mask (bit 14 = channel 0 … bit 6 = channel 8), a divisor and a value per marked channel |
| `0x1` loop start | — | Count in bits 0-7 (`0xFF` = forever); the loop returns to the next word |
| `0x2` loop end | target frame | Subtracts one from the count (except `0xFF`); if it reaches 0 it continues, otherwise it jumps to the target frame and to the loop start |
| `0x3` move texture | source, size, destination | `x << 8 \| y` and `width << 8 \| height` from the origin of the model's texture; x and width in VRAM halfwords |
| `0x4` sound | `bank << 8 \| sound` | Bank 4 is the entity's own (`vabId`); it only sounds if the entity is on screen |

The channels are: 0-2 scale, 3-5 rotation and 6-8 position. A value is not a final position but a velocity:
`delta = value / divisor` per tick, and the remainder (`value % divisor`, with the sign of the value) is distributed
with a counter that adds one unit each time it runs out. The velocity lasts until another instruction changes it.

### One tick

1. Each channel with a velocity takes its step. Rotation is an `s16` and wraps around.
2. If the current word has bit 12, it is taken as a loop start. The original looks at a single bit: a `0x3` in that
   position would also be taken for one; in the disc's streams this never happens.
3. Instructions are executed while the current frame is that of the next one. The first one is executed on frame 1
   whatever its frame says.
4. If the frame is `frameCount`, the animation ends; otherwise, it advances.

A keyframe on frame N moves the pose from the next tick. The original gives one tick per turn of the main loop,
which waits three VSyncs: 20 ticks per second (`AnimationTicksPerSecond`).

### Bones and root motion

Bone 0 starts at identity (in the game it is the entity's position and orientation). Its scale and rotation channels
affect it; the position ones are root motion: each step is rotated by bone 0's rotation and scale and added to the
entity's position. In animations `0x23` and `0x24` the X axis is ignored. The original does this in fixed point with
the GTE; here it is done in `double`, so it may vary by fractions of a unit. The result goes in
`Animation::rootMotion`.

### Loops and limits

- A loop with a count is played whole, as many times as it says.
- A forever loop (`0xFF`, or a count of 0 that gives `0xFF` when subtracting) is played once: the simulation ends
  at its first jump back and marks `endsInLoop`.
- Each loop that jumps is left in `Animation::loops` with `firstTick` (the tick that repeats the jump) and `lastTick`.
- Sounds and texture moves are left as events with their tick.

These are errors: a bone outside the skeleton, a divisor of 0, an instruction type that does not exist (the original would
hang), a loop end without a start, reading outside the file, more than `MaxAnimationTicks` (32768) ticks or more than 65536
instructions in a tick. The bytes that follow the last executed instruction are not part of the format.

### Units

| What | Unit |
|---|---|
| Scale | 4096 = 1.0 |
| Rotation | 4096 = 360°; matrix `Rx·Ry·Rz` (`RotMatrix`) |
| Position | Model units (100 = one collision tile) |
| Local matrix | `[R·S \| position]`; the world one is the parent's times the local |

The code-animated texture of some types (`anim.c: animateEntityTexture`) is not in the stream: the flames of `species_b`, `species_c`, `species_d`, `species_e`, `species_f`, `species_g`, `species_h` and the twin enemy species (mera, emer, blmr, bird, sabd, ebir, dkrz, flar, puti) are four strips of 32 rows at the left of their texture, and the game copies one of the bottom three over the top one on even frames (6: the 2nd; otherwise, 4: the 1st; otherwise, the 0th). The extracted texture already carries all four; the engine does the copy in `apps/game/src/flame_texture.cpp` (width 120, 160, 64 or 176 texels depending on the species).

## glTF

`writeModelGlb` writes a single `.glb` per model (the virtual file system associates one file with an asset, so
there is no separate `.bin`). It writes it with [cgltf](https://github.com/jkuhlmann/cgltf) and builds the GLB container by hand.
The same input always gives the same bytes.

- **Axes:** the original has Y pointing down and Z pointing into the screen. It is rotated 180° about X (`(x, y, z)` →
  `(x, -y, -z)`) to have glTF's Y pointing up. It is a rotation, so the orientation of faces and of
  rotations does not change.
- **Units:** the original's, not scaled. Scale goes to 1.0 = 4096.
- **Nodes:** one per bone (`bone_NN`) with its hierarchy and, at the scene root, the `model` node with the mesh.
- **Mesh and skin:** one mesh with a rigid skin: each vertex has `JOINTS_0` = the bone that draws its object and
  `WEIGHTS_0` = 1. This way a runtime animates the whole model with a single mesh and a single skin, without rearranging nodes. The
  rest pose is that of the first tick of the first animation, without scale so that it can always be inverted; the
  vertices are stored already placed in it, with their `inverseBindMatrices`. The nodes carry that pose with its scale.
- **Primitives:** one per material. Attributes: `POSITION`, `NORMAL` (if lit), `TEXCOORD_0` (if textured and
  the texture is given), `COLOR_0` (if untextured or unlit; with a texture, 128 = 1.0 because it modulates the texel) and
  those of the skin.
- **Materials:** by texture and palette, semi-transparency, two-sided and light. Unlit uses `KHR_materials_unlit`. A
  semi-transparent one uses `BLEND` and stores the console's mode in `extras.blending` (`BLEND` only resembles it).
  Otherwise, with a texture it uses `MASK`, to cut out the texels the console does not draw (those the PNG carries with alpha 0).
- **Per-texel semi-transparency:** the console only blends, in a textured semi-transparent primitive, the texels
  whose color has the STP bit (15); the others it draws opaque. That is why `TexturePalette` carries the kind of each texel
  (`kinds`: transparent, `Plain` or `Stp`) and, if the image has both, the split image (`plainPng` with only the
  `Plain`, `stpPng` with only the `Stp`). A semi-transparent primitive whose texels (the box of its `u v`) have no
  STP is written opaque (`MASK`, `blending` 0); one with both kinds is written twice, opaque with `plainPng` and
  semi-transparent with `stpPng`; one with only `Stp` stays as before. Without `kinds` all its texels are blended.
  Example: the partner's poop (`ETCNA/UNTI.TMD`, `partner_impl.c: initializePoop`) is semi-transparent in subtract
  mode, but its palette (96, 501) has no STP: on the console it is brown and opaque (measured: texels 148,99,41 /
  132,74,24 / 115,57,8 in `twna01`), and before, the engine subtracted it from the floor and it came out black. Its shadow (palette (96, 502),
  colors 5-14 with STP) does subtract.
- **Textures:** see [below](#textures).
- **Animations:** one per animation (`anim_NN`, with NN the id), with one frame per tick, linear interpolation,
  rotation in quaternions and translation of bone 0 = root motion. A channel is omitted if the bone does not move away from the
  node's pose during the whole animation. In `extras`: id, `frameCount`, `hasScale`, `ticksPerSecond`, `tickCount`,
  `endsInLoop`, the loops, the texture moves and the sounds (all with their tick).

### Textures

The console knows nothing about texture files: each textured primitive says where it reads in VRAM. `TSB` gives the page
(x = `(tsb & 0xF) * 64` halfwords, y = `((tsb >> 4) & 1) * 256` rows) and the depth (`(tsb >> 7) & 3`: 0 = 4 bits,
1 = 8 bits, 2 = 16 bits), the `u v` are texels within the page, and `CBA` gives the palette (x = `(cba & 0x3F) * 16`,
y = `(cba >> 6) & 0x1FF`). `TmdPrimitive` has those fields already computed (`pageX`, `pageY`, `depth`, `clutX`,
`clutY`, `blending`).

`writeModelGlb` receives a list of textures (`ModelTexture`), each with its rectangle in VRAM (in halfwords x,
rows y), its depth, its width and height in texels, and a PNG per palette (`TexturePalette`, with its position in VRAM).
A texture without palettes has no image: it serves to give a primitive the `u v` of its page without saying what is seen.

- Each textured primitive takes the **first** texture of its depth that contains all its texels (the `u v` of the
  far edge may end up one past the last one) and, if it has palettes, the one at its `CBA`. The caller puts
  first what was uploaded later, because that is what covers the earlier one in VRAM.
- If no texture works for it, the write fails with the reason. The extractor then writes the model without textures
  and notes it in `unknowns`.
- The `u v` are converted to the chosen texture's coordinates: they are divided by its width and height.
- Only the texture and palette combinations that some primitive uses go into the GLB, with a `NEAREST` filter.
- Each material carries in `extras` where it reads in VRAM: `{"image": [x, y], "depth": d, "clut": [x, y], "blending": b}`,
  with `image` = the corner of its texture and `clut` = its palette. `clut` is `null` for 16-bit ones and for textures without
  an image, whose corner is that of the page. A material without a texture only carries `{"blending": b}`.

## In the engine

The engine reads none of the above: it loads the `.glb` with `assets::loadModel` and plays its clips with
`animation::AnimationPlayer`, respecting the loops and the root motion from `extras`. Which clips the player and the
partner use and how fast they walk is in [models and animation in the engine](../architecture/models.md).

## Tests

- Synthetic fixtures in `packages/formats/tests/model_fixtures.hpp`: a TMD with two objects (triangle and quadrilateral, with and
  without a texture, with and without light, and a line that is skipped), and an MMD with two animations that use all the instructions,
  a loop with a count, a forever one and per-bone scale.
- The GLBs are read back with cgltf (`cgltf_parse`, `cgltf_validate`) and nodes, accessors, channels and
  bounds are checked. Two writes give the same bytes.
- `model_disc_tests.cpp` only runs on the machine of whoever has the disc: with `DWE_MMD_SAMPLE` = the path of an MMD
  it reads it and, with `DWE_MMD_SKELETON` = its skeleton as `object:parent,...`, plays all its animations, writes
  the GLB and reads it back (`DWE_GLB_OUT` saves it). Without those variables, as in CI, it does nothing.
  Run over the 178 MMDs of the US disc: all are read, all the animations (4843) end or enter their infinite
  loop, and all the GLBs pass `cgltf_validate`.
