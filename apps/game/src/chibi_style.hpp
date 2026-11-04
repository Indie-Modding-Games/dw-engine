#pragma once

#include <dwe/animation/animation_player.hpp>
#include <dwe/animation/chibi.hpp>
#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/math.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace dwe::app {

// The chibi style (an engine improvement and a prototype, off by default; the original has none):
// the player, the partner and the NPCs drawn with a big head and short limbs (animation/chibi.hpp).
// It changes the drawing only: animations, collisions and every size the game reads stay the
// original's. A UI setting saved with the user's settings, and the command with the same name that
// the options page sends; `--chibi` on the command line turns it on for a run (captures).
inline constexpr std::string_view ChibiStyleSetting = "chibi_style";

// The game pack's per-species corrections (models/chibi.yaml); none when the file is missing or
// wrong (logged).
[[nodiscard]] animation::ChibiOverrides loadChibiOverrides(const assets::VirtualFileSystem& files);

// The species folder of a species model id (<species folder>/model), or empty.
[[nodiscard]] std::string_view chibiSpeciesOf(const assets::AssetId& model);

// The rig of a model with its species' override, if any.
[[nodiscard]] animation::ChibiRig chibiRigOf(const assets::AssetId& id,
                                             const assets::ModelData& model,
                                             const animation::ChibiOverrides& overrides);

// Scratch for the chibi matrices of one instance at a time, so that drawing allocates nothing once
// warm.
class ChibiJoints {
public:
  // The player's joint matrices drawn chibi; the original's for an inactive rig.
  [[nodiscard]] std::span<const glm::mat4> of(const assets::ModelData& model,
                                              const animation::ChibiRig& rig,
                                              const animation::AnimationPlayer& player);

private:
  std::vector<glm::mat4> world_;
  std::vector<glm::mat4> joints_;
};

} // namespace dwe::app
