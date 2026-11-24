#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "graphics_options.hpp"

namespace dwe::app {

inline constexpr std::string_view SoftwareNoticeDocument = "software_notice";
inline constexpr std::string_view SoftwareModeSetting = "software_mode";
inline constexpr double SoftwareNoticeSeconds = 12.0;

enum class SoftwareMode : std::uint8_t { Auto, On, Off };

[[nodiscard]] std::optional<SoftwareMode> parseSoftwareMode(std::string_view name);

[[nodiscard]] bool softwareModeActive(SoftwareMode mode, bool cpuDevice, bool autotest);

[[nodiscard]] unsigned softwareThreads(std::optional<std::string_view> lpNumThreads,
                                       unsigned hardware);

inline constexpr int LowestQuality = 0;
inline constexpr int HighestQuality = 4;
inline constexpr int SoftwareFrameLimit = 30;

[[nodiscard]] int startQuality(unsigned threads);

struct QualityStep {
  int renderScale = 100;
  ShadowMode shadows = ShadowMode::Off;
  TextureFilter textures = TextureFilter::Nearest;
  bool lighting = false;
  bool vivid = false;
  bool weather = false;
};

[[nodiscard]] QualityStep qualityStep(int level);

[[nodiscard]] GraphicsOptions withQuality(GraphicsOptions options, int level, GraphicsKnobs knobs);

struct QualityDecision {
  int level = 0;
  int ceiling = HighestQuality;

  friend bool operator==(const QualityDecision&, const QualityDecision&) = default;
};

inline constexpr double DownMargin = 1.08;
inline constexpr double UpHeadroom = 0.6;

[[nodiscard]] QualityDecision decideQuality(std::span<const double> busySeconds, double targetFps,
                                            int level, int ceiling);

class QualityTuner {
public:
  static constexpr double WindowSeconds = 3.0;
  static constexpr double SettleSeconds = 2.0;
  static constexpr double HitchSeconds = 1.0;
  static constexpr std::size_t MinFrames = 20;

  QualityTuner(int level, double targetFps);

  std::optional<int> frame(double frameSeconds, double busySeconds);
  void pause();
  void setTargetFps(double targetFps) { targetFps_ = targetFps; }

  [[nodiscard]] int level() const { return decision_.level; }
  [[nodiscard]] int ceiling() const { return decision_.ceiling; }

private:
  QualityDecision decision_;
  double targetFps_;
  double settleLeft_ = SettleSeconds;
  double windowSeconds_ = 0.0;
  std::vector<double> busy_;
};

class FrameLimiter {
public:
  double wait(double now, int framesPerSecond);

private:
  std::optional<double> next_;
};

} // namespace dwe::app
