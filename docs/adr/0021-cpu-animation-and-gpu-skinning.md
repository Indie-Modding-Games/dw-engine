# 0021. Animation sampled on the CPU, skinning on the GPU, in an `animation` module

- **Status:** Accepted
- **Date:** 2026-10-19

## Context

The extracted models are glTF with rigid skinning (each vertex follows one bone) and clips with one frame per tick of
the original, loops and root motion in `extras` ([models](../formats/models.md#gltf)). The game needs to play the
idle, walk and run of the player and the partner (R2.5), the field needs the root motion or the stride speed of those
clips without depending on `render` (`packages/game/*` does not use `render`), and `render` draws N instances per
frame.

## Decision

- Clip sampling and the computation of the bone matrices are done on the **CPU**, in a new module `animation`
  (systems layer, depends on `core` and `assets`), with pure functions and one `AnimationPlayer` per instance.
- **Skinning is done on the GPU**: vertices are uploaded once and the shader transforms them with the instance's
  matrices, which are uploaded every frame.
- A clip's time respects the extractor's `extras`: counted loops come unrolled and the last loop of a clip that ends
  in a loop repeats forever, with one tick of step from `lastTick` to `firstTick`. The root bone's translation is root
  motion: it does not enter the pose and is given separately.

## Consequences

- Pose tests run without a GPU (in CI): tick N = frame N, loops, root motion.
- `packages/game/field` can use `animation` for the stride speed (`repeatGroundSpeed`) without touching `render`.
- The CPU cost is one pose per instance and frame (dozens of bones per model): negligible for the few models in a
  map. If hundreds of instances are ever needed, sampling can move to a compute shader without changing the `render`
  API.

## Alternatives rejected

- **Skinning on the CPU** (uploading already transformed vertices every frame): more bandwidth per frame and different
  drawing code for models with and without animation.
- **Sampling on the GPU:** pose tests would need a GPU and the field could not read the root motion.
- **Putting the player inside `render`:** the field would depend on `render` to know how fast a model walks, which the
  dependency rules forbid for `packages/game/*`.
