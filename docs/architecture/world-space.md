# World space

The coordinates shared by the field, the renderer, the models and the UI. The math is GLM, through
`<dwe/core/math.hpp>`: no module includes GLM directly, so that all of them use the same configuration (angles in
radians and clip depth from 0 to 1, Vulkan's).

## Axes and units

- **Right-handed, +Y up.** This is glTF's system, the one the extracted models use.
- **The original's units, unscaled.** 100 units are one cell of a map's collision grid.
- **From the original to the world:** the original has Y pointing down and Z pointing into the screen. A point or a
  direction `(x, y, z)` of the original is `(x, -y, -z)` in the world (`core::fromOriginalAxes`). It is a 180° turn
  about X, so it does not change the orientation of faces or the direction of rotations.
- **The ground** is the XZ plane. The field's logic moves on it; the height (Y) is set by the map data.
- **Angles:** in radians in the engine. The original uses 4096 per turn (`core::OriginalAnglePerTurn`); data that comes
  from the disc is converted when it is loaded.

The extracted files (`map/<id>/data.yaml` and the others) store the numbers as they are on the disc: the conversion is
done when loading them, in a single place.
