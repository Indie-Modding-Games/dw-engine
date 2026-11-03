#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/battle/player_retreat.hpp>
#include <dwe/game/field/field.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "battle_screen.hpp"
#include "field_scene.hpp"

namespace dwe::app {
namespace {

// battle_setup.c: BTL_handleBattleIntro: the player runs back with its animation 3 and stands with
// its animation 1, its battle stance.
constexpr std::uint32_t PlayerRunClip = 3;
constexpr std::uint32_t PlayerStandClip = 1;

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
    standPlayer();
    intro_->playerBack();
  }
  placePlayer(retreat_->position(), retreat_->facing());
}

// After the banner (battle_setup.c:301-312): a player still running stops and turns to the partner,
// the way back keeps the cells it did not reach, and a flee runs to where it stands.
void BattleScreen::endRetreat() {
  if (!retreat_) {
    return;
  }
  if (retreat_->running()) {
    retreat_->stop(battle_->state().partner().position);
    standPlayer();
    placePlayer(retreat_->position(), retreat_->facing());
  }
  desc_.scene->field().playerTrail().keepOldest(static_cast<std::int32_t>(retreat_->wayLeft()));
  battle_->movePlayer(retreat_->position());
  retreat_.reset();
}

// startAnimation(ENTITY_TABLE[0], 1): the player stands in its battle stance, which plays on
// through the fight (the original's battle frame ticks the player's animation as well) while the
// field stands still; the field takes it back when the battle ends (endExit).
void BattleScreen::standPlayer() const {
  desc_.scene->playPlayerSceneClip(PlayerStandClip);
}

void BattleScreen::placePlayer(game::battle::Point at, game::battle::Facing facing) const {
  game::field::Field& field = desc_.scene->field();
  glm::vec3 position = field.player().position;
  position.x = static_cast<float>(at.x);
  position.z = static_cast<float>(at.z);
  field.placeByScript(field.playerEntity(), position);
  field.turnByScript(
      field.playerEntity(),
      assets::yawOfDirection({static_cast<float>(facing.x), 0.0F, static_cast<float>(facing.z)}));
}

// One battle frame of a flee (battle_tick.c: handleCommands with currentCommand 1): the player's
// run starts on the first frame after the call back, then steps every frame; the fade follows.
void BattleScreen::stepEscape() {
  if (desc_.scene == nullptr || !battle_->state().fleeFrames) {
    return;
  }
  if (!escape_) {
    startEscape();
    return;
  }
  escape_->step();
  placePlayer(escape_->position(), escape_->facing());
  fadeEscape();
}

// handleCommands, command 1 confirmed: startAnimation(3), and on the entry cell (TAMER_START_TILE)
// the player turns to the way out of the map at once: MAP_WARPS' spawn of the exit it came in by
// (CURRENT_EXIT), 3000 units on against the way it faced as it arrived.
void BattleScreen::startEscape() {
  game::field::Field& field = desc_.scene->field();
  const game::field::PlayerTrail& trail = field.playerTrail();
  const game::field::CharacterState player = field.player();
  const game::battle::BattleRules& rules = battle_->rules();
  std::optional<game::battle::Point> away;
  if (const std::optional<std::int32_t> exit = desc_.scene->mapArrival().exit) {
    if (const assets::MapWarp* warp = desc_.scene->map().warp(*exit)) {
      const assets::SpawnPoint spawn = assets::warpSpawn(*warp);
      away = game::battle::wayOut(battlePointOf(spawn.position), facingOfYaw(spawn.yaw),
                                  rules.ending.fleeWayOut);
    }
  }
  escape_.emplace(trail.newestFirst(), trail.start(), away, battlePointOf(player.position),
                  facingOfYaw(player.yaw), rules.timing);
  escapeShadeFrom_.reset();
  desc_.scene->playPlayerSceneClip(PlayerRunClip);
  placePlayer(escape_->position(), escape_->facing());
  core::logDebug("[port:battle] handleCommands (battle_tick.c:54): the partner is called back; the "
                 "player runs over {} kept cells, startAnimation(3){}",
                 trail.newestFirst().size(), away ? "" : "; no exit taken, no way out to face");
}

// handleCommands: on flee frame 20, with the partner standing, fadeToBlack(0x14): fade.c's fade
// out adds shadeMax / (frames + 1) to the shade the screen held (FADE_PROGRESS) every frame. The
// field keeps the shade until the map change after the battle fades on from it.
void BattleScreen::fadeEscape() {
  const game::battle::EndingRules& ending = battle_->rules().ending;
  const std::int32_t frame = *battle_->state().fleeFrames - ending.fleeFadeFrom;
  game::field::Field& field = desc_.scene->field();
  if (frame <= 0 || (!escapeShadeFrom_ && (frame != 1 || battle_->state().partner().defeated()))) {
    return;
  }
  const game::field::TransitionRules& transition = field.rules().transition;
  if (!escapeShadeFrom_) {
    escapeShadeFrom_ =
        field.shadeNow().value_or(game::field::fadeInShade(transition, transition.fadeInFrames));
    core::logDebug("[port:battle] handleCommands (battle_tick.c:72): flee frame {}: "
                   "fadeToBlack({}) from shade {}",
                   ending.fleeFadeFrom, ending.fleeFadeFrames, *escapeShadeFrom_);
  }
  const std::int32_t step = transition.shadeMax / (ending.fleeFadeFrames + 1);
  field.holdShade(std::min(transition.shadeMax, *escapeShadeFrom_ + (step * frame)));
}

// The battle is over: the way back keeps the cells the run did not reach.
void BattleScreen::endEscape() {
  if (!escape_) {
    return;
  }
  if (desc_.scene != nullptr) {
    desc_.scene->field().playerTrail().keepOldest(static_cast<std::int32_t>(escape_->wayLeft()));
  }
  escape_.reset();
  escapeShadeFrom_.reset();
}

// battle_tick.c: tamerTickBattle: through the fight the player looks at the partner every frame,
// except while the partner is called back.
void BattleScreen::facePlayerToPartner(game::battle::Point partner) const {
  if (desc_.scene == nullptr || battle_->state().commands.current == game::battle::Command::Flee) {
    return;
  }
  game::field::Field& field = desc_.scene->field();
  const glm::vec3 at = field.player().position;
  const glm::vec3 toward{static_cast<float>(partner.x) - at.x, 0.0F,
                         static_cast<float>(partner.z) - at.z};
  if (toward.x == 0.0F && toward.z == 0.0F) {
    return;
  }
  field.turnByScript(field.playerEntity(), assets::yawOfDirection(toward));
}

} // namespace dwe::app
