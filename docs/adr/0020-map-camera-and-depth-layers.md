# 0020. Map camera with off-center projection and depth layers as planes

- **Status:** Accepted
- **Date:** 2026-10-19

## Context

Field maps are a prerendered background (the `.TFS` tiles) with the 3D models on top. The original draws it on a
320 × 240 screen and makes the two match exactly: the map camera is fixed, the whole background is the image from that
camera with its optical center at the center of the background, and the screen is a window that follows the player and
stops at the edges ([Maps: how it is drawn](../formats/maps.md#how-it-is-drawn)). The background objects that must
cover the characters are sprites that the original puts in the ordering table at a depth computed when the map loads.

The engine is a PC game ([ADR 0013](0013-native-pc-game.md)): any resolution and aspect ratio, without stretching
anything, with a normal depth buffer and models that are not sorted per polygon.

## Decision

- **Projection:** a single camera per map with the *look-at* from `vp` to `vr` (both doubled, like the original) and an
  **off-center** perspective projection: focal length `H` in background pixels and optical center at the center of the
  background. What you see is a window of the background; the projection matrix is built from that window, so the
  model and the background match by construction at any size.
- **Scale:** `s = min(width / 320, height / 240)` window pixels per background pixel, without rounding to an integer.
  You see `width / s × height / s` background pixels: at 4:3 exactly what the original shows; wider (16:9, 21:9) or
  taller (16:10, 5:4), you see more background on that side. It is never stretched and you never see less than the
  original.
- **Scrolling:** the window is centered on the point being followed (the player) and clamped to the edges of the
  background, like the original's `cameraX = clamp(...)`. If the background is smaller than the window on one side, it
  is centered and what is left over are **black bars**: the renderer draws them at the end of the layers
  (`drawLayers`), on top of everything, models included; it does not use scissor. The window's corner is rounded to
  whole window pixels so the background does not shake when moving.
- **Depth layers:** each sprite instance is a screen-aligned rectangle, at its position in background pixels, drawn
  **with depth** as a plane at the distance `orderValue · 16` from the camera, with `orderValue` computed on load as
  in `calcMapObjectOrder`. Models are drawn with the same camera and the normal depth buffer (`LessOrEqual` test, 0
  near and 1 far, near = 100, far = 65,536, the end of the ordering table); the layers use the same test. That way a
  model fragment is covered when its distance is at least `orderValue · 16`, which is the original's rule (`mean SZ
  >> 4 >= orderValue`) applied per pixel instead of per polygon.
- **Draw order:** background (no depth), models, sprites from far to near. A semi-transparent sprite is drawn twice,
  as the console does per texel: its colors without STP opaque (they write depth) and those with STP with their blend
  mode (they do not write it).
- **Original data outside the renderer:** how the extracted map files refer to each other (the texture X that chooses
  a page starting at VRAM X 384 with widths of 64 or 128, the CLUT rows 480, 481 + i and `clut + 468`, the sentinel
  `y == 10000` and the ordering limits 0x23, 0xFF6 and 0xFFE) are properties of that data, not of drawing. They are
  resolved on load in `assets` (`loadMapImages`, `MapSpriteSheet`, `mapSpriteOrderValue`), with named constants; the
  renderer only draws textured quads and an order value. The planned follow-up is to resolve it at extraction, as a
  new and additive output of `dwx` (without changing the existing ones), and have the engine read the result
  directly.

## Consequences

- Any resolution and aspect ratio works without special cases, and the 4:3 framing is the original's.
- The `MapCamera` helpers (world ↔ background ↔ window, ground picking) are the same math as the projection, so the
  click to move lands where you see it.
- With per-pixel depth, a big polygon that crosses a sprite's plane is covered only in the part behind it; the
  original covered it entirely or not at all depending on its mean. It is better this way and changes no case in the
  walkthrough.
- The original's ties (polygon at the same index as the sprite) depended on insertion order; here the sprite wins.
- With non-integer scales the background's pixels are not all the same size: the renderer filters the background so
  it is not noticeable (see `packages/engine/render`).

## Alternatives rejected

- **Integer scale with bars:** pixel-perfect, but at 1080p it leaves 120 pixels of bars at the top and bottom and at
  many resolutions it wastes half the screen.
- **Always scale by height (`height / 240`):** the same on wide screens, but in a window taller than 4:3 it would see
  less width than the original.
- **Stretch the background to the window:** it deforms the image and the 3D would stop matching without changing the
  projection.
- **Reproduce the ordering table (sort polygons):** faithful, but it forces the model path to sort triangles on the
  CPU and loses the depth buffer.
