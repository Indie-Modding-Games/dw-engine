#include <dwe/game/battle/effect_runner.hpp>

#include <span>

#include "effect_math.hpp"

namespace dwe::game::battle {

namespace fx = effect_math;

EffectMatrix boneMatrix(const EffectBody& place, std::span<const BonePose> chain) {
  // The root's coordinate: RotMatrix, ScaleMatrix (the fighters stand at scale 1) and TransMatrix.
  EffectMatrix out = fx::rotXYZ({.x = 0, .y = place.rotationY, .z = 0});
  out.t = place.location;
  for (const BonePose& bone : chain) {
    // calculatePosMatrix: the node's own coordinate, RotMatrix then ScaleMatrix.
    const EffectMatrix local = fx::scaled(fx::rotXYZ(bone.rotation), bone.scale);
    // GsMulCoord3: the node's place turned by the parent's matrix, then the matrices multiplied.
    const EffectVector moved = fx::apply(out, bone.location);
    const EffectVector at{.x = out.t.x + moved.x, .y = out.t.y + moved.y, .z = out.t.z + moved.z};
    out = fx::multiply(out, local);
    out.t = at;
  }
  return out;
}

} // namespace dwe::game::battle
