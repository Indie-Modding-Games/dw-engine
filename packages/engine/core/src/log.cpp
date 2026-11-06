#include <dwe/core/log.hpp>

#include <mutex>
#include <print>
#include <vector>

namespace dwe::core {
namespace {

struct Log {
  std::mutex mutex;
  std::vector<LogSink> sinks;
};

Log& theLog() {
  static Log log;
  return log;
}

} // namespace

void addLogSink(LogSink sink) {
  Log& log = theLog();
  std::scoped_lock lock{log.mutex};
  log.sinks.push_back(std::move(sink));
}

void writeLog(LogLevel level, std::string_view message) {
  Log& log = theLog();
  std::scoped_lock lock{log.mutex};
  std::println(stderr, "[{}] {}", logLevelName(level), message);
  for (const LogSink& sink : log.sinks) {
    sink(level, message);
  }
}

std::string_view logLevelName(LogLevel level) {
  switch (level) {
  case LogLevel::Debug:
    return "debug";
  case LogLevel::Info:
    return "info";
  case LogLevel::Warning:
    return "warning";
  case LogLevel::Error:
    return "error";
  }
  return "unknown";
}

} // namespace dwe::core
