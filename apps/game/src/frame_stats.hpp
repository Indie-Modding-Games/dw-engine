#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace dwe::app {

// Frame times of a run, for the performance check of R2.13 (>= 60 frames per second on the route). Frames that are
// slow on purpose (a map load, a window resize, a screenshot that waits for the GPU) can be left out of the steady
// figures with skipNext; they still count in the overall ones.
class FrameStats {
public:
    struct Summary {
        std::size_t frames = 0;
        double averageFps = 0.0;
        // The slowest frame, as frames per second.
        double minimumFps = 0.0;
        // 99 % of the frames were at least this fast.
        double percentile99Fps = 0.0;
        double worstMilliseconds = 0.0;
    };

    // Adds a frame; true when it counts in the steady figures.
    bool add(double frameSeconds);
    // Leaves the next frames out of the steady figures.
    void skipNext(int frames);

    [[nodiscard]] Summary overall() const { return summarize(all_); }
    [[nodiscard]] Summary steady() const { return summarize(steady_); }

    // "N frames, avg A fps, min M fps, p99 P fps (worst W ms)".
    static std::string describe(const Summary& summary);

private:
    static Summary summarize(std::vector<double> seconds);

    std::vector<double> all_;
    std::vector<double> steady_;
    int skip_ = 0;
};

} // namespace dwe::app
