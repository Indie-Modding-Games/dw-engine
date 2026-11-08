#include "gym_batch.hpp"

#include <glm/geometric.hpp>

#include <algorithm>

namespace dwe::app {

void GymBatch::setSessions(int sessions) {
  sessions_ = std::clamp(sessions, GymSessionsMin, GymSessionsMax);
}

void GymBatch::begin(std::string_view course, std::string_view map) {
  course_ = course;
  map_ = map;
  left_ = sessions_ - 1;
  anchor_.reset();
  waitingForCare_ = false;
  stopped_ = GymBatchStop::None;
}

void GymBatch::stop(GymBatchStop why) {
  if (left_ > 0) {
    stopped_ = why;
  }
  left_ = 0;
  waitingForCare_ = false;
}

std::optional<std::string> GymBatch::next(const GymBatchMoment& moment) {
  if (left_ == 0) {
    return std::nullopt;
  }
  if (moment.map != map_) {
    stop(GymBatchStop::MapChanged);
    return std::nullopt;
  }
  if (moment.partnerUnfit) {
    stop(GymBatchStop::PartnerUnfit);
    return std::nullopt;
  }
  if (!anchor_) {
    anchor_ = moment.player;
  } else if (glm::distance(*anchor_, moment.player) > GymBatchLeash) {
    stop(GymBatchStop::WalkedAway);
    return std::nullopt;
  }
  waitingForCare_ = moment.partnerNeedsCare;
  if (!moment.fieldIdle || moment.partnerNeedsCare) {
    return std::nullopt;
  }
  --left_;
  anchor_.reset();
  return course_;
}

} // namespace dwe::app
