#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace dwe::core {

enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error };

// Receives every message written to the log. The default sink writes to stderr.
using LogSink = std::function<void(LogLevel level, std::string_view message)>;

// Adds a sink that receives every message from now on.
void addLogSink(LogSink sink);

// Writes a message to every sink.
void writeLog(LogLevel level, std::string_view message);

// Returns the name of a level, as it appears in the log.
std::string_view logLevelName(LogLevel level);

template <typename... Args>
void logDebug(std::format_string<Args...> format, Args&&... args) {
  writeLog(LogLevel::Debug, std::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void logInfo(std::format_string<Args...> format, Args&&... args) {
  writeLog(LogLevel::Info, std::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void logWarning(std::format_string<Args...> format, Args&&... args) {
  writeLog(LogLevel::Warning, std::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void logError(std::format_string<Args...> format, Args&&... args) {
  writeLog(LogLevel::Error, std::format(format, std::forward<Args>(args)...));
}

} // namespace dwe::core
