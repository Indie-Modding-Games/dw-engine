#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/joining.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/game/field/player_trail.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "battle_screen.hpp"
#include "battle_trigger.hpp"
#include "field_scene.hpp"
#include "messages.hpp"

namespace dwe::app {
namespace {

// battle_main.c: BTL_handleVictorySequence: the beaten enemies get up, and in a fight that could be
// fled they walk off; the stance a fixed fight's fighters stand in instead.
constexpr std::uint32_t GetUpClip = 0x2c;
constexpr std::uint32_t WalkClip = 0x24;
constexpr std::uint32_t StanceClip = 0x21;
// battle_main.c:948 and battle_setup.c:238 set animFlag 4 on an NPC that walks off: battle_tick.c:
// NPCEntityTickBattle then ticks its animation twice a frame, so its clip, and the root motion
// that moves it, run at twice their pace.
constexpr float RunOffPace = 2.0F;
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
  // A stage that hides the map (a tournament arena: removeMapEntities) shows none of its NPCs.
  if (desc_.scene == nullptr || desc_.scene->stage().sceneryHidden) {
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

// btl.c: handleBattleStart, in a battle with no predefined enemies: the other NPCs on screen, in
// the map's order, roll by the flee table (game::battle::rollJoiningEnemies) whether they join the
// one the player touched. On the maps isScreenConcave names, one with a wall on the straight line
// of cells (map.c: isLinearPathBlocked) from the player or from the partner does not roll. Only
// NPCs with a species that can fight are asked: the engine needs their stats to make an enemy.
std::vector<std::int32_t> BattleScreen::joiningNpcs(std::int32_t touched, bool fixedFight) {
  std::vector<std::int32_t> joined;
  if (desc_.scene == nullptr || desc_.species == nullptr || desc_.battle == nullptr) {
    return joined;
  }
  const FieldScene& scene = *desc_.scene;
  const game::field::Field& field = scene.field();
  const game::battle::BattleRules& rules = desc_.battle->rules();
  const bool concave = std::ranges::contains(rules.joining.concaveMaps, field.mapId());
  const assets::CollisionCell player = assets::collisionCellAt(field.player().position);
  const assets::CollisionCell partner = assets::collisionCellAt(field.partner().position);
  std::vector<std::int32_t> askedNpcs;
  std::vector<game::battle::DigimonType> asked;
  for (const std::int32_t npc : npcsOnScreen()) {
    const assets::MapNpc* record = recordOf(scene, npc);
    const std::optional<glm::vec3> position = field.npcs().positionOf(npc);
    if (npc == touched || record == nullptr || !position || !canFight(*record, *desc_.species)) {
      continue;
    }
    const assets::CollisionCell cell = assets::collisionCellAt(*position);
    if (concave && (game::field::isCellLineBlocked(field.collision(), player, cell) ||
                    game::field::isCellLineBlocked(field.collision(), partner, cell))) {
      continue;
    }
    askedNpcs.push_back(npc);
    asked.push_back(game::battle::combatantOf(*desc_.species->find(*record->digimon)).type);
  }
  if (asked.empty()) {
    return joined;
  }
  const assets::DigimonRow* partnerRow = desc_.species->find(desc_.session->partnerView().species);
  std::vector<std::string> carried;
  if (desc_.inventory != nullptr) {
    for (const std::optional<game::inventory::Stack>& stack :
         desc_.inventory->inventory().slots()) {
      if (stack) {
        carried.push_back(desc_.inventory->items().at(stack->item).id);
      }
    }
  }
  const game::battle::JoinContext context{
      .partnerType = partnerRow != nullptr ? game::battle::combatantOf(*partnerRow).type
                                           : game::battle::DigimonType::None,
      .inventory = carried,
      .scripted = fixedFight};
  const std::vector<bool> joins =
      game::battle::rollJoiningEnemies(rules, context, asked, desc_.session->random());
  for (std::size_t index = 0; index < joins.size(); ++index) {
    if (joins[index]) {
      joined.push_back(askedNpcs[index]);
    }
  }
  core::logDebug("[port:battle] handleBattleStart (btl.c:264): isScreenConcave={}, {} of {} "
                 "NPCs on screen join",
                 concave, joined.size(), asked.size());
  return joined;
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
  leaver.figure.pace = RunOffPace;
  leaver.walking = true;
  core::logDebug("[port:battle] BTL_handleVictorySequence (battle_main.c:944): NPC {} walks to its "
                 "flee point",
                 leaver.npc);
}

// battle_main.c: BTL_handleVictorySequence after the end box. A fixed fight's enemies stand and
// stay; otherwise they get up and walk off. The NPCs on screen that were not fighting ran off as
// it started (startFlight).
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
  if (exit.leavers.empty()) {
    return;
  }
  for (Leaver& leaver : exit.leavers) {
    desc_.scene->holdFighter(leaver.npc, leaver.figure);
  }
  exit_ = std::move(exit);
}

// battle_setup.c: BTL_handleBattleIntro:228-245: every NPC on screen that is not an enemy plays
// its walk at twice its pace (animFlag 4), facing its flee point, and the opening waits until all
// of them are off screen.
void BattleScreen::startFlight() {
  fleers_.clear();
  fleeSeconds_ = 0.0;
  if (desc_.scene == nullptr) {
    return;
  }
  // btl.c: handleBattleStart:251/277: an NPC off the screen as the battle starts leaves the map
  // (isOnMap = 0) until the map loads again. A stage that hides the map shows none of its NPCs
  // and keeps them.
  if (!desc_.scene->stage().sceneryHidden) {
    std::int32_t gone = 0;
    for (const assets::MapNpc& record : desc_.scene->map().npcs) {
      const std::int32_t npc = record.scriptId;
      if (std::ranges::contains(onScreenAtStart_, npc) || std::ranges::contains(enemyNpcs_, npc)) {
        continue;
      }
      if (desc_.scene->field().npcs().positionOf(npc) && desc_.scene->npcs().unload(npc)) {
        ++gone;
      }
    }
    core::logDebug("[port:battle] handleBattleStart (btl.c:277): {} NPCs off the screen leave "
                   "the map",
                   gone);
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
    const assets::MapNpc* record = recordOf(*desc_.scene, npc);
    if (record != nullptr) {
      const glm::vec3 flee = assets::toWorldPosition(record->flee);
      leaver.figure.yaw =
          assets::yawOfDirection({flee.x - leaver.figure.x, 0.0F, flee.z - leaver.figure.z});
    }
    leaver.figure.starts = 1;
    leaver.figure.pace = RunOffPace;
    leaver.walking = true;
    desc_.scene->holdFighter(npc, leaver.figure);
    fleers_.push_back(leaver);
  }
  core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:237): {} NPCs not fighting "
                 "run to their flee points; NPCEntityTickBattle (battle_tick.c:308) ticks their "
                 "animation twice",
                 fleers_.size());
}

// btl.c: handleBattleStart: setFleeBubble(i, 1) over every NPC on screen that does not fight; its
// mark hangs where the anger mark does (renderFleeBubble), `lift` over its species' height.
std::vector<screens::AngerAnchor> BattleScreen::runOffAnchors(float lift) const {
  std::vector<screens::AngerAnchor> anchors;
  if (desc_.scene == nullptr) {
    return anchors;
  }
  for (const std::int32_t npc : onScreenAtStart_) {
    if (std::ranges::contains(enemyNpcs_, npc) || anchors.size() >= screens::FleeMarkSlots) {
      continue;
    }
    const std::optional<glm::vec3> position = desc_.scene->field().npcs().positionOf(npc);
    const assets::MapNpc* record = recordOf(*desc_.scene, npc);
    if (!position || record == nullptr) {
      continue;
    }
    const assets::DigimonRow* row = desc_.species != nullptr && record->digimon
                                        ? desc_.species->find(*record->digimon)
                                        : nullptr;
    glm::vec3 at = *position;
    at.y += (row != nullptr ? static_cast<float>(row->height) : 0.0F) + lift;
    const std::optional<glm::vec2> fraction = desc_.scene->worldToWindowFraction(at);
    anchors.push_back(
        fraction ? screens::AngerAnchor{screens::WindowFraction{.x = fraction->x, .y = fraction->y}}
                 : std::nullopt);
  }
  return anchors;
}

// One frame of the opening for the NPCs running off; true once none shows (battle_setup.c:289-296).
bool BattleScreen::stepFlight(double seconds) {
  if (fleers_.empty()) {
    return true;
  }
  fleeSeconds_ += seconds;
  bool allOff = true;
  for (Leaver& leaver : fleers_) {
    const float step = walkSpeed(leaver.npc) * leaver.figure.pace * static_cast<float>(seconds);
    const glm::vec3 forward = assets::forwardOfYaw(leaver.figure.yaw);
    leaver.figure.x += forward.x * step;
    leaver.figure.z += forward.z * step;
    if (!offScreen(leaver.npc, leaver.figure.x, leaver.figure.z)) {
      allOff = false;
    }
    desc_.scene->holdFighter(leaver.npc, leaver.figure);
  }
  if (!allOff && fleeSeconds_ >= ExitGuardSeconds) {
    core::logWarning("{}", core::formatMessage(messages::ExitTimedOut, ExitGuardSeconds));
    return true;
  }
  return allOff;
}

// battle_setup.c:315-318: the NPCs that ran off leave the map (isOnMap 0) for the rest of the
// battle; the end of it keeps them off (BTL_battleMain:783-808: they were on screen as it began).
void BattleScreen::endFlight() {
  if (desc_.scene == nullptr) {
    fleers_.clear();
    return;
  }
  for (const Leaver& leaver : std::exchange(fleers_, {})) {
    if (desc_.scene->npcs().unload(leaver.npc)) {
      core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:316): NPC {} off the map",
                     leaver.npc);
    }
  }
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
  closeMenu();
}

// battle_main.c:783-808: the field takes the fighters back; of the NPCs on screen when the battle
// started, a fight that could be fled takes every one off the map, a fixed fight all but its
// fighters (they stay for the scene that follows, as mayo00's Agumon does).
void BattleScreen::endExit() {
  if (desc_.scene != nullptr) {
    // The player's battle stance gives way to the field's own clips.
    desc_.scene->playPlayerSceneClip(std::nullopt);
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
