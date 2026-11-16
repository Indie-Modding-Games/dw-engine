# animation

Plays the animations of the models that `assets` loads (`ModelData`): `clip_sampling.hpp` samples a clip at an
instant (with the loops and the root motion that the extractor brings in `extras`), blends poses and computes the bone
matrices for skinning; `AnimationPlayer` carries the time, the clip and the crossfade of an instance and returns
the root motion of each step. `finished()` says whether a clip without a loop stayed on its last pose; `playedThrough()`
says whether any clip, also one with a loop, reached the end of its first pass (`anim.c`: `animFrame` reaches
`frameCount`), which is what the field's care scenes expect. `clipSounds` gives the clip's sounds
(`extras.sounds`, instruction 0x4000 of `anim.c: tickAnimation`) of the ticks that a step leaves behind, on each pass
of the loop, and `AnimationPlayer::takeSounds()` those of the last `advance` so the owner can play them or not. It does not use the GPU: `render` only receives the matrices. It depends on `core` and `assets`.
Details in [models and animation in the engine](../../architecture/models.md) and in
[ADR 0021](../../adr/0021-cpu-animation-and-gpu-skinning.md).

## Partner clips

The eleven character models that enter as a partner in the [extra evolutions](../game/extra-evolutions.md)
bring only the few field clips that their scenes use, and none for combat. `clip_synthesis.hpp` gives them, when loading
the partner's model, all the ones the field and combat ask for (`addPartnerClips`), from the models that the
user extracted from their disc: in git there are only the recipes, `animations/partner_clips.yaml` in the game pack
(`partner_clips.hpp` reads it; schema `partner_clips`). The extractor does not change (`ExtractorVersion` stays at 15) and the
cooked `base.pak` carries nothing new: the clips are made in memory and the recipe goes in the game pack.

- **The book.** `clips` is the partner's set of clips (care, gestures, poses and combat reactions) and
  `first_technique` the first technique clip (46, one per slot of its technique row); each clip of the donor
  from there on is also made. `motions` gives the motion made for each clip and `technique` that of the techniques.
- **Retarget** (`mode: retarget`): the donor's clips are passed to the model with the bone map (`bones`: model
  bone → donor bone, or `same` if the hierarchy is the same). Each bone rotates, in model space, what the donor's
  rotates relative to its rest, so it does not matter that the bones of the two point elsewhere at rest;
  the ones the map leaves out stay at rest under their parent. The hip's up and down is scaled with the height of
  each model's hip.
- **Procedural** (`mode: procedural`): for the model with no donor that resembles it, the clip is its own idle
  (clip 0) plus a motion (`breathe`, `stride`, `bounce`, `shake`, `nod`, `lie`, `squat`, `droop`, `guard`,
  `recoil`, `fall`, `rise`, `lunge`) on the bone groups of the recipe (`groups`: body, head, arms,
  legs, `spin` for the ones that turn on their own, like some gears, and `upright` for a body that does not lean: what
  the motion would tilt forward or back it displaces instead). The species can change the motion of a
  clip (`motions`).
- **Combat timing.** Both modes copy from the donor's clip with the same id the ticks, the loops and the root
  motion, so the duration, the steps and the frames of the hit and the effects are the donor's; the
  lunge (`lunge`) hits at `strike` of the clip. The sounds of the donor's own bank (bank 4, its voice) are not
  copied.
- **Effects.** The techniques use the donor's effect points (the species' `effect_type`), which name bones of
  its skeleton: the field takes them to the model bone that the map ties to it, or to the nearest mapped ancestor
  (`PartnerClipsMade::effectJoints`, `FieldScene::fighterEffectJoint`).
- **Partner only.** The field makes them in `FieldScene::loadActor` when it loads the partner model of
  a species with a recipe, before the stand-in clips (`MissingClips::StandIn`); the characters in scenes keep
  their model and their clips. `keep` leaves the own clips that the partner keeps (its idle, its walk).

Tests: `packages/engine/animation/tests/clip_synthesis_tests.cpp`, with made-up models (the retargeting rotates
the same in model space with another rest, copies the timings and the root motion, leaves the voice; `keep` and the ones that
cannot be made; the effect map; the lunge at its hit; the book's errors) and, with `DWE_CONTENT`, that each
species in the book receives all the partner and combat clips with the donor's duration and root motion.

## Chibi style

An engine improvement and a prototype (the original does not have it): `chibi.hpp` draws a model with a big head, a short
torso and short limbs without touching its animations. `classifyChibi` recognizes the parts in the rest pose, with the
skeleton and the mesh of each bone:

- the trunk goes from the body root to the last bone where two limbs split (two branches with two or more
  bones with mesh and at least 4% of the vertices each); what hangs from it are branches;
- a leg reaches the ground (its lowest vertex within 1/16 of the model's height), hangs from the lower part of the
  body and goes down more than it opens sideways;
- the head is the center branch, with at least 5% of the vertices, higher and more forward than the trunk; the
  bones between its root and the bone with the most vertices are the neck;
- the other branches of two or more bones are arms (to one side, hanging), wings (to one side, not hanging or toward the back) or
  tails (in the center, behind); a one-bone branch is left as it is;
- four legs make a quadruped, none and a tail a serpent, wings without arms a winged body and two legs or
  two arms a biped.

The original's models are already chibi and are the reference. `measureBody` measures a model in its rest pose (with the
mesh already attached to the bones) and gives eleven proportions: the head relative to the height and the torso relative to the height
(only for deciding), the head relative to the torso (`head_size`), the head's width relative to the trunk's
(`head_width`), the reach of arms, legs, neck, tail and wings and the size of hands and feet (the width of the last
30% of the limb), all relative to the torso. In a serpent, with no trunk, the tail acts as the torso. `profileOf` gathers
the measurements of the originals by shape and keeps, per proportion, the minimum, the median and the maximum (a repeated measurement
counts once); the profile lives as data in `models/chibi_profile.yaml` of the game pack (40 bipeds, 13
quadrupeds, 6 winged and 11 serpents) and is read with `parseChibiProfile`. The test with content generates it:

```
DWE_CONTENT=<extracted content> DWE_CHIBI_PROFILE_OUT=<game pack>/models/chibi_profile.yaml \
  dwe_animation_tests --test-case='*chibi profile*'
```

Without `DWE_CHIBI_PROFILE_OUT` the same test compares the saved profile with the one that comes from the user's disc. Species
with `off: true` in `models/chibi.yaml` do not enter the profile. `dwx validate` checks the two documents with
[`chibi_profile.schema.json`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/chibi_profile.schema.json)
and [`chibi_overrides.schema.json`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/chibi_overrides.schema.json).

`buildChibiRig(model, profile)` is a detector: if all the measured proportions fall within the range of their shape
(with a tolerance of 0.1% for the file's rounding), the model is already chibi (`ChibiRig::alreadyChibi`) and is
drawn exactly the same as without the style (the same matrices, bit for bit). If not, it computes how far it falls outside (the
largest deviation, on a logarithmic scale, up to double the range's end) and each proportion that is adjusted moves toward the
median: from the nearest end of the range to the median, closer the further outside it is. A few rounds of adjustment
look for the factors (head, arms, legs, neck, tail, wings, hands and feet; the torso does not change and acts as the unit)
that give those proportions, with limits so the model does not deform: between ×0.35 and ×3, hands and feet up to ×2 and the
neck never grows. `ChibiRig` keeps the measurement, the range, which proportions were outside and how they end up drawn. A
body of unknown shape, or without a profile for its shape, is left unchanged. The factors are applied on the animated
pose in `computeChibiJointMatrices`: it brings the bones of each limb closer, squashes each segment lengthwise and
widens it a little, scales the whole head from its bone and shortens the trunk along its axis (upward in an upright body,
forward in one on four legs). On each frame it raises or lowers the model so the lowest point of the legs
lands where the original's does; a model that flies (at rest, above 1/16 of its height) is moved once
and for all. If an arm, the tail, a wing or the head end up below the original's lowest point, that part shrinks
a little until it no longer does. The logic's matrices (`AnimationPlayer::jointMatrices`) do not change.

Cost: one more pass over the nodes and, per frame, the legs' support points (between 86 and 296, without
repeats) of each model; with the debug build, the animation advance takes one and a half times longer.
Isolated tests in `tests/chibi_tests.cpp`: box models made in the test (a tall thin humanoid and a
long-legged quadruped fall within the range without lifting the feet off the ground; one already chibi stays the same bit for bit
during a step), the detector's edges, the profile written and read back and, with the extracted content, the disc's
profile and that no original changes.
