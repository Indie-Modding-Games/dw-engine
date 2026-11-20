#include "coop_training.hpp"

#include <dwe/core/log.hpp>

#include <cmath>
#include <cstdint>
#include <utility>
#include <variant>

namespace dwe::app {

namespace {

constexpr double Thousand = 1000.0;

// A course id as the wire carries it (MaxCourseText at most).
std::string wireCourse(std::string_view course) {
  return std::string{course.substr(0, game::coop::MaxCourseText)};
}

} // namespace

void CoopTraining::own(game::coop::PlayerId self, std::string_view course) {
  if (!game::coop::validPlayer(self)) {
    return;
  }
  std::string now = wireCourse(course);
  if (!now.empty() && courses_.at(self).empty() && !noticeShown_) {
    noticeDue_ = true;
  }
  courses_.at(self) = std::move(now);
}

void CoopTraining::hostTick(const session::GameSession& session, std::string_view ownCourse,
                            std::span<const CoopTrainingPeer> guests, double realSeconds) {
  own(game::coop::HostPlayerId, ownCourse);
  for (const CoopTrainingPeer& guest : guests) {
    for (const game::coop::Message& message : guest.bridge->takeTrainingMessages()) {
      // A guest speaks for its own partner only: the link's player, whatever it named.
      if (const auto* status = std::get_if<game::coop::TrainingStatus>(&message)) {
        courses_.at(guest.id) = status->course;
      }
    }
  }
  const session::WorldTime now = session.worldTime();
  const bool due = teller_.due(now, realSeconds, session.clock().rules().framesPerSecond);
  for (const CoopTrainingPeer& guest : guests) {
    if (due || !clockTold_.contains(guest.id)) {
      const game::coop::WorldClock clock{
          .elapsedFrames = now.elapsedFrames,
          .running = now.running,
          .speedThousandths = static_cast<std::uint32_t>(std::lround(now.speed * Thousand))};
      if (auto sent = guest.bridge->send(clock); sent) {
        clockTold_.insert(guest.id);
      } else {
        core::logWarning("coop: could not tell player {} the clock: {}", guest.id,
                         sent.error().message);
      }
    }
    // Each guest hears the others' trainings, the host's among them, as they change.
    std::array<std::string, game::coop::MaxPlayers>& told = told_.at(guest.id);
    for (game::coop::PlayerId player = 0; player < game::coop::MaxPlayers; ++player) {
      if (player == guest.id || told.at(player) == courses_.at(player)) {
        continue;
      }
      if (guest.bridge->send(
              game::coop::TrainingStatus{.player = player, .course = courses_.at(player)})) {
        told.at(player) = courses_.at(player);
      }
    }
  }
}

void CoopTraining::guestTick(session::GameSession& session, std::string_view ownCourse,
                             game::coop::PlayerId self, CoopGameplayBridge& host) {
  for (const game::coop::Message& message : host.takeTrainingMessages()) {
    if (const auto* clock = std::get_if<game::coop::WorldClock>(&message)) {
      session.hearWorldClock({.elapsedFrames = clock->elapsedFrames,
                              .running = clock->running,
                              .speed = static_cast<double>(clock->speedThousandths) / Thousand});
    } else if (const auto* status = std::get_if<game::coop::TrainingStatus>(&message);
               status != nullptr && status->player != self) {
      courses_.at(status->player) = status->course;
    }
  }
  own(self, ownCourse);
  const std::string course = wireCourse(ownCourse);
  if (sentOnce_ && course == sent_) {
    return;
  }
  if (auto sent = host.send(game::coop::TrainingStatus{.player = self, .course = course}); sent) {
    sent_ = course;
    sentOnce_ = true;
  }
}

bool CoopTraining::training(game::coop::PlayerId id) const {
  return game::coop::validPlayer(id) && !courses_.at(id).empty();
}

bool CoopTraining::takeClockNotice() {
  if (!noticeDue_) {
    return false;
  }
  noticeDue_ = false;
  noticeShown_ = true;
  return true;
}

void CoopTraining::playerLeft(game::coop::PlayerId id) {
  if (!game::coop::validPlayer(id)) {
    return;
  }
  courses_.at(id).clear();
  clockTold_.erase(id);
  told_.at(id) = {};
}

void CoopTraining::reset() {
  *this = CoopTraining{};
}

} // namespace dwe::app
