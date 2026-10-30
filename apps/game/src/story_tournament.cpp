#include "story_tournament.hpp"

#include <dwe/core/log.hpp>

#include <cmath>

namespace dwe::app {

namespace {

// std_main.c: STD_placePlayer2: the rival starts across the arena from the partner.
constexpr std::int32_t RivalDistance = 1000;

game::battle::Point pointOf(glm::vec3 world) {
  return {.x = static_cast<std::int32_t>(std::lround(world.x)),
          .z = static_cast<std::int32_t>(std::lround(world.z))};
}

} // namespace

bool StoryTournament::take(const session::StoryScreenRequest& request,
                           session::StoryScenes& scenes) {
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
  return true;
}

void StoryTournament::matchOver(game::battle::Outcome outcome) {
  matchRunning_ = false;
  matchDue_ = tournament_->matchOver(*session_, outcome);
}

void StoryTournament::update(session::StoryScenes& scenes, BattleScreen& battle,
                             const FieldScene& scene) {
  if (!running_ || matchRunning_ || battle.active()) {
    return;
  }
  if (!matchDue_) {
    end(scenes);
    return;
  }
  const game::field::Field& field = scene.field();
  const game::battle::Point partner = pointOf(field.partner().position);
  const game::battle::Point rival{.x = partner.x - RivalDistance, .z = partner.z};
  auto request = tournament_->matchRequest(*session_, partner, pointOf(field.player().position),
                                           field.mapId(), rival);
  auto started = request ? battle.start(std::move(*request), {}, true)
                         : core::Result<void>{std::unexpected{request.error()}};
  if (!started) {
    core::logWarning("[port:tournament] STD_runMatch (std_setup.c:401): {}; the tournament ends",
                     started.error().message);
    end(scenes);
    return;
  }
  matchDue_ = false;
  matchRunning_ = true;
}

void StoryTournament::end(session::StoryScenes& scenes) {
  running_ = false;
  matchDue_ = false;
  if (auto finished = tournament_->finish(*session_); !finished) {
    core::logWarning("[port:tournament] startTournament (utils2.c:662): {}",
                     finished.error().message);
  }
  scenes.screenClosed();
}

} // namespace dwe::app
