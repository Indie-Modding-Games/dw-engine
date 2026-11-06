#include "control_log.hpp"

#include <string>

namespace dwe::app {

ControlLog::ControlLog(std::size_t capacity) : capacity_{capacity} {}

void ControlLog::add(core::LogLevel level, std::string_view message) {
  if (level != core::LogLevel::Warning && level != core::LogLevel::Error) {
    return;
  }
  ControlLogEntry entry{
      .seq = 0, .error = level == core::LogLevel::Error, .text = std::string{message}};
  const std::scoped_lock lock{mutex_};
  entry.seq = ++newest_;
  if (capacity_ == 0) {
    return;
  }
  if (entries_.size() == capacity_) {
    entries_.pop_front();
  }
  entries_.push_back(std::move(entry));
}

ControlLogTail ControlLog::after(std::optional<std::uint64_t> since) const {
  const std::scoped_lock lock{mutex_};
  ControlLogTail tail{.next = newest_, .entries = {}};
  for (const ControlLogEntry& entry : entries_) {
    if (!since || entry.seq > *since) {
      tail.entries.push_back(entry);
    }
  }
  return tail;
}

std::uint64_t ControlLog::newest() const {
  const std::scoped_lock lock{mutex_};
  return newest_;
}

std::shared_ptr<ControlLog> keepControlLog(std::size_t capacity) {
  auto log = std::make_shared<ControlLog>(capacity);
  core::addLogSink(
      [log](core::LogLevel level, std::string_view message) { log->add(level, message); });
  return log;
}

} // namespace dwe::app
