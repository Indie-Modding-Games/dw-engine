#include "pack_logs.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>
#include <utility>

#include "messages.hpp"

namespace dwe::script::detail {

PackLogs::PackLogs(Now now) : now_{std::move(now)} {}

void PackLogs::info(std::string_view pack, std::string_view line) {
    PackLog& log = logOf(pack);
    if (admit(log, pack, line.size())) {
        core::logInfo("{}", line);
    }
}

void PackLogs::error(std::string_view pack, std::string_view line) {
    PackLog& log = logOf(pack);
    if (line == log.lastError) {
        ++log.repeats;
        if (now_() - log.repeatsCounted >= RepeatReportInterval) {
            countRepeats(log, pack);
        }
        return;
    }
    countRepeats(log, pack);
    log.lastError = line;
    if (admit(log, pack, line.size())) {
        core::logError("{}", line);
    }
}

void PackLogs::tick() {
    const Clock::time_point now = now_();
    for (auto& [pack, log] : logs_) {
        if (log.repeats != 0 && now - log.repeatsCounted >= RepeatReportInterval) {
            countRepeats(log, pack);
        }
    }
}

void PackLogs::flush() noexcept {
    for (auto& [pack, log] : logs_) {
        countRepeats(log, pack, Bucket::Bypassed);
    }
}

PackLogs::PackLog& PackLogs::logOf(std::string_view pack) {
    auto found = logs_.find(pack);
    if (found == logs_.end()) {
        const Clock::time_point now = now_();
        found = logs_
                    .emplace(std::string{pack}, PackLog{.bytes = LogBurstBytes,
                                                        .refilled = now,
                                                        .dropped = 0,
                                                        .lastError = {},
                                                        .repeats = 0,
                                                        .repeatsCounted = now})
                    .first;
    }
    return found->second;
}

bool PackLogs::admit(PackLog& log, std::string_view pack, std::size_t bytes) {
    const Clock::time_point now = now_();
    const std::chrono::duration<double> elapsed = now - log.refilled;
    log.refilled = now;
    log.bytes = std::min(LogBurstBytes, log.bytes + (elapsed.count() * LogBytesPerSecond));
    const auto size = static_cast<double>(bytes);
    if (log.bytes < size) {
        if (log.dropped == 0) {
            core::logWarning("{}", core::formatMessage(messages::LogFlooded, pack, LogBytesPerSecond));
        }
        ++log.dropped;
        return false;
    }
    log.bytes -= size;
    if (log.dropped != 0) {
        core::logWarning("{}", core::formatMessage(messages::LogLinesLeftOut, pack, log.dropped));
        log.dropped = 0;
    }
    return true;
}

void PackLogs::countRepeats(PackLog& log, std::string_view pack, Bucket bucket) {
    log.repeatsCounted = now_();
    if (log.repeats == 0) {
        return;
    }
    const std::string line = core::formatMessage(messages::ErrorRepeated, pack, log.repeats);
    log.repeats = 0;
    if (bucket == Bucket::Bypassed || admit(log, pack, line.size())) {
        core::logError("{}", line);
    }
}

} // namespace dwe::script::detail
