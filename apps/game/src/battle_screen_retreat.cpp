#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/battle/player_retreat.hpp>
#include <dwe/game/field/field.hpp>

#include <cmath>
#include <cstdint>
#include <utility>

#include "battle_screen.hpp"
#include "field_scene.hpp"

namespace dwe::app {
namespace {

// battle_setup.c: BTL_handleBattleIntro: the player runs back with its animation 3 and stands with
// its animation 1, the field's standing clip.
constexpr std::uint32_t PlayerRunClip = 3;

game::battle::Point battlePointOf(glm::vec3 world) {
  return {.x = static_cast<std::int32_t>(std::lround(world.x)),
          .z = static_cast<std::int32_t>(std::lround(world.z))};
}

game::battle::Facing facingOfYaw(float yaw) {
  const glm::vec3 forward = assets::forwardOfYaw(yaw);
  return {.x = static_cast<std::int32_t>(std::lround(forward.x * game::battle::FacingScale)),
          .z = static_cast<std::int32_t>(std::lround(forward.z * game::battle::FacingScale))};
}

} // namespace

// BTL_handleBattleIntro: as the banner gathers, the player runs back along the way the field kept
// (map.c: TAMER_WAYPOINT_*) toward the cell it entered the map on (TAMER_START_TILE).
void BattleScreen::startRetreat() {
  retreat_.reset();
  if (desc_.scene == nullptr) {
    return;
  }
  const game::field::Field& field = std::as_const(*desc_.scene).field();
  const game::field::PlayerTrail& trail = field.playerTrail();
  const game::field::CharacterState player = field.player();
  retreat_.emplace(trail.newestFirst(), trail.start(), battlePointOf(player.position),
                   facingOfYaw(player.yaw), battle_->rules().timing);
  desc_.scene->playPlayerSceneClip(PlayerRunClip);
  core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:254): the player runs back "
                 "over {} kept cells; startAnimation(3)",
                 trail.newestFirst().size());
}

// One frame of the banner: the player's step, and once it stops the banner's timer jumps to 198.
void BattleScreen::stepRetreat() {
  if (!retreat_ || !retreat_->running()) {
    return;
  }
  if (retreat_->step(desc_.ground, battle_->state().partner().position)) {
    core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:281): the player is back; "
                   "startAnimation(1), the timer jumps to 198");
    desc_.scene->playPlayerSceneClip(std::nullopt);
    intro_->playerBack();
  }
  placePlayer();
}

// After the banner (battle_setup.c:301-312): a player still running stops and turns to the partner,
// the way back keeps the cells it did not reach, and a flee runs to where it stands.
void BattleScreen::endRetreat() {
  if (!retreat_) {
    return;
  }
  if (retreat_->running()) {
    retreat_->stop(battle_->state().partner().position);
    desc_.scene->playPlayerSceneClip(std::nullopt);
    placePlayer();
  }
  desc_.scene->field().playerTrail().keepOldest(static_cast<std::int32_t>(retreat_->wayLeft()));
  battle_->movePlayer(retreat_->position());
  retreat_.reset();
}

void BattleScreen::placePlayer() const {
  game::field::Field& field = desc_.scene->field();
  glm::vec3 position = field.player().position;
  const game::battle::Point at = retreat_->position();
  position.x = static_cast<float>(at.x);
  position.z = static_cast<float>(at.z);
  const game::battle::Facing facing = retreat_->facing();
  field.placeByScript(field.playerEntity(), position);
  field.turnByScript(
      field.playerEntity(),
      assets::yawOfDirection({static_cast<float>(facing.x), 0.0F, static_cast<float>(facing.z)}));
}

// battle_tick.c: tamerTickBattle: through the fight the player looks at the partner every frame,
// except while the partner is called back.
void BattleScreen::facePlayerToPartner() const {
  if (desc_.scene == nullptr || battle_->state().commands.current == game::battle::Command::Flee) {
    return;
  }
  game::field::Field& field = desc_.scene->field();
  const glm::vec3 at = field.player().position;
  const game::battle::Point partner = battle_->state().partner().position;
  const glm::vec3 toward{static_cast<float>(partner.x) - at.x, 0.0F,
                         static_cast<float>(partner.z) - at.z};
  if (toward.x == 0.0F && toward.z == 0.0F) {
    return;
  }
  field.turnByScript(field.playerEntity(), assets::yawOfDirection(toward));
}

} // namespace dwe::app
