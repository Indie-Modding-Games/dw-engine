#pragma once

#include <dwe/assets/model_data.hpp>
#include <dwe/assets/partner_clips.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <map>
#include <vector>

namespace dwe::animation {

// What addPartnerClips did to a model.
struct PartnerClipsMade {
  // The clip ids made, in order.
  std::vector<std::uint32_t> made;
  // The clip ids of the set it could not make: the donor has no clip to move or to time it by.
  std::vector<std::uint32_t> unmade;
  // A donor's joint -> this model's joint, for every donor joint that is not the same joint here:
  // the techniques' effects name the joints of the donor's skeleton (their bone lists are the
  // original's, by species), and their points go on the joint the bone map ties to it.
  std::map<std::int32_t, std::int32_t> effectJoints;
};

// Gives a model the partner clips of a species' recipe (docs/modulos/engine/animation.md, «Clips de
// compañero»): the book's clip set and the donor's technique clips. A clip the model has is
// replaced unless the recipe keeps it. A retargeted clip turns each mapped joint as the donor's
// joint turns from its rest pose, in model space, so skeletons whose joints point other ways in
// their rest pose move alike; the joints the map leaves out keep their rest pose under their
// parent. The hips' bob is scaled by the ratio of the hips' heights. A procedural clip is the
// model's own idle with the recipe's motion added. Both take the donor clip's ticks, loops and
// root motion, so the battle's frames (its length, steps and effect points) line up with the
// donor's. The donor's species-bank sounds (bank 4) are left out: they are the donor's voice.
// Fails, changing nothing, when a joint of the recipe is not in the model's or the donor's skin.
core::Result<PartnerClipsMade> addPartnerClips(assets::ModelData& model,
                                               const assets::ModelData& donor,
                                               const assets::PartnerClipBook& book,
                                               const assets::PartnerClipRecipe& recipe);

} // namespace dwe::animation
