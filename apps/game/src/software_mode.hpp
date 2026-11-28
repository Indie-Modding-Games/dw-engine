#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "graphics_options.hpp"

namespace dwe::app {

inline constexpr std::string_view SoftwareNoticeDocument = "software_notice";
inline constexpr std::string_view SoftwareModeSetting = "software_mode";
inline constexpr double SoftwareNoticeSeconds = 12.0;

// Why the game plays without the graphics card, or in the compatible mode
// (docs/architecture/graphics-diagnosis.md): the launcher passes its first reason in this variable
// as "code|key=value|...", and the notice names it with the values below.
inline constexpr std::string_view GraphicsReasonVariable = "DWE_GRAPHICS_REASON";
inline constexpr std::string_view NoticeKindSetting = "software_notice.kind";
inline constexpr std::string_view NoticeReasonSetting = "software_notice.reason";
inline constexpr std::array<std::string_view, 6> NoticeParams{"gpu",    "adapter", "dedicated",
                                                              "driver", "minimum", "api"};

struct NoticeReason {
  std::string code;
  std::vector<std::pair<std::string, std::string>> params;

  [[nodiscard]] std::string_view param(std::string_view key) const;
};

[[nodiscard]] std::optional<NoticeReason> parseNoticeReason(std::string_view text);

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
inline constexpr double FarDownMargin = 1.75;
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

// A weak graphics card's automatic quality steps one preset lighter after a window of mostly slow
// frames (below 40 fps), never heavier.
class SlowFrameWatch {
public:
  static constexpr double WindowSeconds = 5.0;
  static constexpr double SettleSeconds = 3.0;
  static constexpr double SlowFrameSeconds = 1.0 / 40.0;

  [[nodiscard]] bool frame(double frameSeconds);
  void pause();

private:
  double settleLeft_ = SettleSeconds;
  double windowSeconds_ = 0.0;
  std::vector<double> frames_;
};

class FrameLimiter {
public:
  double wait(double now, int framesPerSecond);

private:
  std::optional<double> next_;
};

} // namespace dwe::app
