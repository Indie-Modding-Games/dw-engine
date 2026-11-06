#pragma once

#include <dwe/core/log.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

#include "remote_control.hpp"

namespace dwe::app {

// The warnings and errors the control channel's log_tail gives (remote_control.hpp): the newest
// `capacity`, each numbered from 1 in the order written, so a client asks for those after the last
// one it saw. The log's sinks may write from any thread, so it keeps them under a lock.
class ControlLog {
public:
  // The entries kept: an editor that polls a few times a second never misses one in practice.
  static constexpr std::size_t DefaultCapacity = 256;

  explicit ControlLog(std::size_t capacity = DefaultCapacity);

  // Keeps a warning or an error; other levels are left out.
  void add(core::LogLevel level, std::string_view message);

  // The entries kept after `since` (all of them without it), oldest first.
  [[nodiscard]] ControlLogTail after(std::optional<std::uint64_t> since) const;
  // The newest entry's number; 0 before the first.
  [[nodiscard]] std::uint64_t newest() const;

private:
  mutable std::mutex mutex_;
  std::size_t capacity_;
  std::uint64_t newest_ = 0;
  std::deque<ControlLogEntry> entries_;
};

// A log that receives every warning and error from now on (core::addLogSink); it lives as long as
// the log.
std::shared_ptr<ControlLog> keepControlLog(std::size_t capacity = ControlLog::DefaultCapacity);

} // namespace dwe::app
