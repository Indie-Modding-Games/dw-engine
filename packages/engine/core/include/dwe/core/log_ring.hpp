#pragma once

#include <dwe/core/log.hpp>

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::core {

// The last lines of the log, kept in memory for a bug report: each line as the default sink writes
// it ("[info] message"), the oldest dropped once there are `capacity`. Safe from any thread.
class LogRing {
public:
  explicit LogRing(std::size_t capacity);

  void add(LogLevel level, std::string_view message);
  // The lines kept, oldest first.
  [[nodiscard]] std::vector<std::string> lines() const;

private:
  mutable std::mutex mutex_;
  std::size_t capacity_;
  std::deque<std::string> lines_;
};

// A ring that receives every message from now on (addLogSink); it lives as long as the log.
std::shared_ptr<LogRing> keepLogTail(std::size_t capacity);

} // namespace dwe::core
