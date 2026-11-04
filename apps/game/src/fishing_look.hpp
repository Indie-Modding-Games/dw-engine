#pragma once

#include <dwe/game/fishing/fishing.hpp>
#include <dwe/game/fishing/look.hpp>

#include <optional>
#include <vector>

#include "field_scene.hpp"
#include "scene_prims.hpp"

namespace dwe::app {

// FISH_updateRodPose with the player's model as it is drawn now: the hand that holds the rod is
// its node 9. Nothing when the model has no such node.
[[nodiscard]] std::optional<game::fishing::RodPose>
fishingRodPose(const FieldScene& scene, const game::fishing::Fishing& water);

// FISH_renderFishingWorld's first part: the fish's sprites (FISH_drawFishPool,
// FISH_drawHookedFish), drawn before the ripples and the marks. Nothing while the fishing is
// stopped (setFishingDisabled).
void addFishingFish(FieldScene& scene, const game::fishing::Fishing& water,
                    const ScenePrimFrame& frame, std::vector<ScenePrim>& prims);
// Its rod part, after the marks: the rod's model (FISH_drawRodSegments) and the bobber's sprite
// (FISH_bobberCommand 1), while the rod is out.
void addFishingRod(FieldScene& scene, const game::fishing::Fishing& water,
                   const ScenePrimFrame& frame, std::vector<ScenePrim>& prims);

} // namespace dwe::app
