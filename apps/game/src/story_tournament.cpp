#include "story_tournament.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/render/map_camera.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "place_command.hpp"

namespace dwe::app {

namespace {

// std_main.c: STD_placePlayer2: the rival starts across the field from the partner (a cup played
// without its arena).
constexpr std::int32_t RivalDistance = 1000;
// The id the battle holds the rival under (FieldScene::addStageFighter), past any script id.
constexpr std::int32_t RivalFighter = 0x1000;
// The arena's camera centres on its own background, wide enough that no bars show at any window
// size (the original draws the arena over its whole 320 x 240 screen).
constexpr float ArenaBackgroundScale = 100.0F;

game::battle::Point pointOf(glm::vec3 world) {
  return {.x = static_cast<std::int32_t>(std::lround(world.x)),
          .z = static_cast<std::int32_t>(std::lround(world.z))};
}

// The original's z points into the screen, the world's out of it (core::fromOriginalAxes).
game::battle::Point battlePointOf(game::tournament::ArenaPoint point) {
  return {.x = point.x, .z = -point.z};
}

game::tournament::ArenaPoint arenaPointOf(game::battle::Point point) {
  return {.x = point.x, .z = -point.z};
}

glm::vec3 worldOf(const std::array<std::int32_t, 3>& original) {
  return assets::toWorldPosition(glm::ivec3{original[0], original[1], original[2]});
}

void placeCharacter(FieldScene& scene, Placement::Who who, glm::vec2 ground, float yaw) {
  scene.place(Placement{.who = who, .npc = 0, .ground = ground, .yaw = yaw, .clip = {}});
}

} // namespace

bool StoryTournament::take(const session::StoryScreenRequest& request, session::StoryScenes& scenes,
                           FieldScene& scene) {
  if (request.screen != session::StoryScreen::Tournament) {
    return false;
  }
  auto started = tournament_ != nullptr && session_ != nullptr
                     ? tournament_->begin(*session_)
                     : core::Result<void>{core::fail("the app has no tournament")};
  if (!started) {
    core::logWarning("[port:tournament] scriptStartTournament (tournament.c:128): {}; the "
                     "tournament is over at once",
                     started.error().message);
    if (request.waits) {
      scenes.screenClosed();
    }
    return true;
  }
  running_ = true;
  matchDue_ = true;
  enterArena(scene);
  return true;
}

void StoryTournament::enterArena(FieldScene& scene) {
  arena_.reset();
  auto arena = tournament_->arenaStage(scene.field().mapId());
  if (!arena) {
    core::logWarning("[port:tournament] STD_initializeTournament (std_setup.c:243): {}; the cup "
                     "is fought on the map",
                     arena.error().message);
    return;
  }
  arena_ = std::move(*arena);
  playerBefore_ = scene.field().player();
  partnerBefore_ = scene.field().partner();
  auto model = assets::AssetId::parse(arena_->model);
  if (model) {
    scene.setProps(*model, {glm::mat4{1.0F}});
  }
  // removeMapEntities, ENTITY_TABLE[0]->isOnScreen = 0: the map's characters are gone and the
  // player hidden; it waits where the partner starts, so the partner does not wander off between
  // the matches.
  const game::battle::Point partner = battlePointOf(arena_->start.partner);
  placeCharacter(scene, Placement::Who::Player,
                 {static_cast<float>(partner.x), static_cast<float>(partner.z)},
                 assets::yawFromOriginal(arena_->partnerYaw));
  placeCharacter(scene, Placement::Who::Partner,
                 {static_cast<float>(partner.x), static_cast<float>(partner.z)},
                 assets::yawFromOriginal(arena_->partnerYaw));
  showArena(scene, arena_->start.partner, arena_->start.rival);
}

void StoryTournament::showArena(FieldScene& scene, game::tournament::ArenaPoint partner,
                                game::tournament::ArenaPoint rival) {
  const game::tournament::ArenaView view =
      game::tournament::arenaView(arena_->camera, partner, rival);
  FieldStage stage;
  stage.sceneryHidden = true;
  stage.view =
      render::MapCameraDesc{.eye = worldOf(view.eye),
                            .target = worldOf(view.target),
                            .focalLength = static_cast<float>(arena_->camera.projection),
                            .backgroundSize = render::OriginalScreenSize * ArenaBackgroundScale};
  stage.shownProps = {arena_->model};
  stage.ground = arena_->collision;
  scene.setStage(stage);
}

void StoryTournament::leaveArena(FieldScene& scene) {
  scene.clearStageFighters();
  if (!arena_) {
    return;
  }
  scene.setStage({});
  if (auto model = assets::AssetId::parse(arena_->model)) {
    scene.setProps(*model, {});
  }
  // STD_deinitializeTournament, reinitializeAfterTournament: the map as it was.
  placeCharacter(scene, Placement::Who::Player,
                 {playerBefore_.position.x, playerBefore_.position.z}, playerBefore_.yaw);
  placeCharacter(scene, Placement::Who::Partner,
                 {partnerBefore_.position.x, partnerBefore_.position.z}, partnerBefore_.yaw);
  arena_.reset();
}

void StoryTournament::matchOver(game::battle::Outcome outcome) {
  matchRunning_ = false;
  matchDue_ = tournament_->matchOver(*session_, outcome);
}

void StoryTournament::update(session::StoryScenes& scenes, BattleScreen& battle,
                             FieldScene& scene) {
  if (running_ && arena_) {
    // STD_setCameraOrbit follows the fighters while a match runs; between them it stays.
    if (const game::battle::Battle* fight = battle.battle();
        fight != nullptr && fight->state().fighters.size() > 1) {
      showArena(scene, arenaPointOf(fight->state().partner().position),
                arenaPointOf(fight->state().fighters.at(1).position));
    }
  }
  if (!running_ || matchRunning_ || battle.active()) {
    return;
  }
  // removeMapEntities after each match: the beaten (or winning) rival leaves the arena.
  scene.clearStageFighters();
  if (!matchDue_) {
    end(scenes, scene);
    return;
  }
  const game::field::Field& field = scene.field();
  game::battle::Point partner = pointOf(field.partner().position);
  game::battle::Point rival{.x = partner.x - RivalDistance, .z = partner.z};
  game::battle::Point tamer = pointOf(field.player().position);
  if (arena_) {
    // STD_runIntro ends with the fighters where STD_setPostIntroPosition puts them.
    partner = battlePointOf(arena_->start.partner);
    rival = battlePointOf(arena_->start.rival);
    tamer = partner;
    showArena(scene, arena_->start.partner, arena_->start.rival);
  }
  auto request = tournament_->matchRequest(*session_, partner, tamer, field.mapId(), rival);
  // STD_initializeOpponent's entity, placed by STD_placePlayer2: the battle holds it as it holds a
  // map's NPC.
  std::vector<std::int32_t> fighters;
  if (request && arena_ && !request->enemies.empty() &&
      scene.addStageFighter(RivalFighter, request->enemies.front().species,
                            {static_cast<float>(rival.x), static_cast<float>(rival.z)},
                            assets::yawFromOriginal(arena_->rivalYaw))) {
    fighters.push_back(RivalFighter);
  }
  auto started = request ? battle.start(std::move(*request), std::move(fighters), true)
                         : core::Result<void>{std::unexpected{request.error()}};
  if (!started) {
    core::logWarning("[port:tournament] STD_runMatch (std_setup.c:401): {}; the tournament ends",
                     started.error().message);
    end(scenes, scene);
    return;
  }
  matchDue_ = false;
  matchRunning_ = true;
}

void StoryTournament::end(session::StoryScenes& scenes, FieldScene& scene) {
  running_ = false;
  matchDue_ = false;
  leaveArena(scene);
  if (auto finished = tournament_->finish(*session_); !finished) {
    core::logWarning("[port:tournament] startTournament (utils2.c:662): {}",
                     finished.error().message);
  }
  scenes.screenClosed();
}

} // namespace dwe::app
