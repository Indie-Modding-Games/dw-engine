#include "gym_batch.hpp"

#include <algorithm>

namespace dwe::app {

void GymBatch::setSessions(int sessions) {
  sessions_ = std::clamp(sessions, GymSessionsMin, GymSessionsMax);
}

void GymBatch::begin(std::string_view course, std::string_view map) {
  course_ = course;
  map_ = map;
  left_ = sessions_ - 1;
}

std::optional<std::string> GymBatch::next(const GymBatchMoment& moment) {
  if (left_ == 0) {
    return std::nullopt;
  }
  if (moment.map != map_ || moment.partnerNeedsCare) {
    left_ = 0;
    return std::nullopt;
  }
  if (!moment.fieldIdle) {
    return std::nullopt;
  }
  --left_;
  return course_;
}

} // namespace dwe::app
