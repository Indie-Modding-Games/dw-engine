#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/npcs.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "battle_screen.hpp"
#include "field_scene.hpp"
#include "messages.hpp"

namespace dwe::app {
namespace {

// battle_main.c: BTL_handleVictorySequence: the beaten enemies get up, and in a fight that could be
// fled they walk off; the stance a fixed fight's fighters stand in instead.
constexpr std::uint32_t GetUpClip = 0x2c;
constexpr std::uint32_t WalkClip = 0x24;
constexpr std::uint32_t StanceClip = 0x21;
// The original walks them until they are off screen, with no limit; a leaver that cannot get there
// (its walk clip has no motion) stops the exit after this long.
constexpr double ExitGuardSeconds = 10.0;

const assets::MapNpc* recordOf(const FieldScene& scene, std::int32_t npc) {
  const auto& records = scene.map().npcs;
  const auto found = std::ranges::find(records, npc, &assets::MapNpc::scriptId);
  return found != records.end() ? &*found : nullptr;
}

} // namespace

// map.c: entityIsOffScreen(entity, 0x140, 0xf0): no corner of the box its species' radius and
// height make around it shows in the window.
bool BattleScreen::offScreen(std::int32_t npc, float x, float z) const {
  const FieldScene& scene = *desc_.scene;
  const std::optional<glm::vec3> position = scene.field().npcs().positionOf(npc);
  const assets::MapNpc* record = recordOf(scene, npc);
  if (!position || record == nullptr) {
    return true;
  }
  const assets::DigimonRow* row =
      desc_.species != nullptr && record->digimon ? desc_.species->find(*record->digimon) : nullptr;
  const float radius = row != nullptr ? static_cast<float>(row->radius) : 0.0F;
  const float height = row != nullptr ? static_cast<float>(row->height) : 0.0F;
  constexpr std::array<std::pair<float, float>, 4> Quadrants{
      {{1.0F, 1.0F}, {1.0F, -1.0F}, {-1.0F, 1.0F}, {-1.0F, -1.0F}}};
  for (const float up : {0.0F, height}) {
    for (const auto& [sideX, sideZ] : Quadrants) {
      const std::optional<glm::vec2> fraction = scene.worldToWindowFraction(
          {x + (radius * sideX), position->y + up, z + (radius * sideZ)});
      if (fraction && fraction->x >= 0.0F && fraction->x <= 1.0F && fraction->y >= 0.0F &&
          fraction->y <= 1.0F) {
        return false;
      }
    }
  }
  return true;
}

std::vector<std::int32_t> BattleScreen::npcsOnScreen() const {
  std::vector<std::int32_t> shown;
  if (desc_.scene == nullptr) {
    return shown;
  }
  for (const assets::MapNpc& record : desc_.scene->map().npcs) {
    const std::optional<glm::vec3> position =
        desc_.scene->field().npcs().positionOf(record.scriptId);
    if (position && !offScreen(record.scriptId, position->x, position->z)) {
      shown.push_back(record.scriptId);
    }
  }
  return shown;
}

// The walk clip's own ground speed (its root motion moves the entity, animFlag 4); the battle's
// walk when the clip carries none.
float BattleScreen::walkSpeed(std::int32_t npc) const {
  if (const auto entity = desc_.scene->field().npcs().entityOf(npc)) {
    if (const float speed = desc_.scene->clipGroundSpeed(*entity, WalkClip); speed > 0.0F) {
      return speed;
    }
  }
  return static_cast<float>(desc_.battle->rules().timing.walkSpeed * game::battle::FramesPerSecond);
}

// battle_main.c:944-949: the leaver plays its walk, which moves it (animFlag 4), facing its flee
// point.
void BattleScreen::startWalk(Leaver& leaver) const {
  const assets::MapNpc* record = recordOf(*desc_.scene, leaver.npc);
  if (record != nullptr) {
    const glm::vec3 flee = assets::toWorldPosition(record->flee);
    leaver.figure.yaw =
        assets::yawOfDirection({flee.x - leaver.figure.x, 0.0F, flee.z - leaver.figure.z});
  }
  leaver.figure.clip = WalkClip;
  ++leaver.figure.starts;
  leaver.figure.frozen = false;
  leaver.walking = true;
  core::logDebug("[port:battle] BTL_handleVictorySequence (battle_main.c:944): NPC {} walks to its "
                 "flee point",
                 leaver.npc);
}

// battle_main.c: BTL_handleVictorySequence after the end box. A fixed fight's enemies stand and
// stay; otherwise they get up and walk off. The NPCs on screen that are not fighting fled when it
// started in the original (battle_setup.c:225: they walk to their flee points until off screen);
// that start is not ported, so they walk off here with the enemies.
void BattleScreen::startExit() {
  if (desc_.scene == nullptr || !won_) {
    return;
  }
  Exit exit;
  for (std::size_t index = 1; index < figures_.size() && index <= enemyNpcs_.size(); ++index) {
    FighterFigure& figure = figures_[index];
    figure.clip = fixedFight_ ? StanceClip : GetUpClip;
    ++figure.starts;
    figure.frozen = false;
    const std::int32_t npc = enemyNpcs_[index - 1];
    if (!fixedFight_ && desc_.scene->field().npcs().positionOf(npc)) {
      exit.leavers.push_back({.npc = npc, .figure = figure, .walking = false});
    }
  }
  core::logDebug("[port:battle] BTL_handleVictorySequence (battle_main.c:921): the enemies "
                 "startAnimation({:#x}){}",
                 GetUpClip, fixedFight_ ? "; a fixed fight's stand (0x21) and stay" : "");
  if (!fixedFight_) {
    figures_.resize(std::min<std::size_t>(figures_.size(), 1));
  }
  for (const std::int32_t npc : onScreenAtStart_) {
    const auto position = desc_.scene->field().npcs().positionOf(npc);
    if (!position || std::ranges::contains(enemyNpcs_, npc)) {
      continue;
    }
    Leaver leaver{.npc = npc,
                  .figure = {.x = position->x,
                             .z = position->z,
                             .yaw = 0.0F,
                             .clip = WalkClip,
                             .starts = 0,
                             .frozen = false},
                  .walking = false};
    startWalk(leaver);
    exit.leavers.push_back(leaver);
  }
  if (exit.leavers.empty()) {
    return;
  }
  for (Leaver& leaver : exit.leavers) {
    desc_.scene->holdFighter(leaver.npc, leaver.figure);
  }
  exit_ = std::move(exit);
}

void BattleScreen::tickExit(double seconds) {
  FieldScene& scene = *desc_.scene;
  exit_->seconds += seconds;
  bool allOff = true;
  for (Leaver& leaver : exit_->leavers) {
    if (!leaver.walking) {
      // battle_main.c:943: the get-up has played (animFlag 1 off) before the walk starts.
      if (scene.fighterPlayedThrough(leaver.npc, leaver.figure)) {
        startWalk(leaver);
      }
    } else {
      const float step = walkSpeed(leaver.npc) * static_cast<float>(seconds);
      const glm::vec3 forward = assets::forwardOfYaw(leaver.figure.yaw);
      leaver.figure.x += forward.x * step;
      leaver.figure.z += forward.z * step;
    }
    if (!offScreen(leaver.npc, leaver.figure.x, leaver.figure.z)) {
      allOff = false;
    }
    scene.holdFighter(leaver.npc, leaver.figure);
  }
  if (!allOff && exit_->seconds < ExitGuardSeconds) {
    return;
  }
  if (!allOff) {
    core::logWarning("{}", core::formatMessage(messages::ExitTimedOut, ExitGuardSeconds));
  }
  core::logDebug("[port:battle] BTL_handleVictorySequence (battle_main.c:956): every leaver is off "
                 "screen");
  exit_.reset();
  endExit();
}

// battle_main.c:783-808: the field takes the fighters back; of the NPCs on screen when the battle
// started, a fight that could be fled takes every one off the map, a fixed fight all but its
// fighters (they stay for the scene that follows, as mayo00's Agumon does).
void BattleScreen::endExit() {
  if (desc_.scene != nullptr) {
    desc_.scene->releaseFighters();
    for (const std::int32_t npc : std::exchange(onScreenAtStart_, {})) {
      if (fixedFight_ && std::ranges::contains(enemyNpcs_, npc)) {
        continue;
      }
      if (desc_.scene->npcs().unload(npc)) {
        core::logDebug("[port:battle] BTL_battleMain (battle_main.c:{}): NPC {} off the map",
                       fixedFight_ ? 794 : 806, npc);
      }
    }
  }
  figures_.clear();
  if (session::SessionMusic* music = desc_.session->music(); music != nullptr) {
    music->leaveBattle(std::exchange(won_, false));
  }
}

} // namespace dwe::app
