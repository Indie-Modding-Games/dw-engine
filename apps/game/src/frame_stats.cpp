#include "frame_stats.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace dwe::app {

bool FrameStats::add(double frameSeconds) {
    all_.push_back(frameSeconds);
    if (skip_ > 0) {
        --skip_;
        return false;
    }
    steady_.push_back(frameSeconds);
    return true;
}

void FrameStats::skipNext(int frames) {
    skip_ = std::max(skip_, frames);
}

FrameStats::Summary FrameStats::summarize(std::vector<double> seconds) {
    Summary summary{.frames = seconds.size()};
    if (seconds.empty()) {
        return summary;
    }
    const double total = std::accumulate(seconds.begin(), seconds.end(), 0.0);
    std::ranges::sort(seconds);
    const double worst = seconds.back();
    // The frame time that 99 % of the frames do not exceed.
    constexpr double Percentile = 0.99;
    const auto index = static_cast<std::size_t>(std::ceil(Percentile * static_cast<double>(seconds.size()))) - 1;
    const double p99 = seconds.at(std::min(index, seconds.size() - 1));
    const auto fps = [](double frame) { return frame > 0.0 ? 1.0 / frame : 0.0; };
    summary.averageFps = total > 0.0 ? static_cast<double>(seconds.size()) / total : 0.0;
    summary.minimumFps = fps(worst);
    summary.percentile99Fps = fps(p99);
    constexpr double Milliseconds = 1000.0;
    summary.worstMilliseconds = worst * Milliseconds;
    return summary;
}

std::string FrameStats::describe(const Summary& summary) {
    return std::format("{} frames, avg {:.1f} fps, min {:.1f} fps, p99 {:.1f} fps (worst {:.2f} ms)", summary.frames,
                       summary.averageFps, summary.minimumFps, summary.percentile99Fps, summary.worstMilliseconds);
}

} // namespace dwe::app
