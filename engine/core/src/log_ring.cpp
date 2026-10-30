#include <dwe/core/log_ring.hpp>

#include <format>
#include <utility>

namespace dwe::core {

LogRing::LogRing(std::size_t capacity) : capacity_{capacity} {}

void LogRing::add(LogLevel level, std::string_view message) {
  std::string line = std::format("[{}] {}", logLevelName(level), message);
  const std::scoped_lock lock{mutex_};
  if (capacity_ == 0) {
    return;
  }
  if (lines_.size() == capacity_) {
    lines_.pop_front();
  }
  lines_.push_back(std::move(line));
}

std::vector<std::string> LogRing::lines() const {
  const std::scoped_lock lock{mutex_};
  return {lines_.begin(), lines_.end()};
}

std::shared_ptr<LogRing> keepLogTail(std::size_t capacity) {
  auto ring = std::make_shared<LogRing>(capacity);
  addLogSink([ring](LogLevel level, std::string_view message) { ring->add(level, message); });
  return ring;
}

} // namespace dwe::core
