#include "software_mode.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <numeric>

namespace dwe::app {

namespace {

constexpr std::array<QualityStep, HighestQuality + 1> Ladder{{
    {.renderScale = 50,
     .shadows = ShadowMode::Off,
     .textures = TextureFilter::Nearest,
     .lighting = false,
     .vivid = false,
     .weather = false},
    {.renderScale = 75,
     .shadows = ShadowMode::Off,
     .textures = TextureFilter::Nearest,
     .lighting = false,
     .vivid = false,
     .weather = false},
    {.renderScale = 100,
     .shadows = ShadowMode::Round,
     .textures = TextureFilter::Nearest,
     .lighting = false,
     .vivid = false,
     .weather = false},
    {.renderScale = 100,
     .shadows = ShadowMode::Round,
     .textures = TextureFilter::Smooth,
     .lighting = false,
     .vivid = false,
     .weather = true},
    {.renderScale = 100,
     .shadows = ShadowMode::Sun,
     .textures = TextureFilter::Smooth,
     .lighting = true,
     .vivid = true,
     .weather = true},
}};

} // namespace

std::optional<SoftwareMode> parseSoftwareMode(std::string_view name) {
  if (name == "auto") {
    return SoftwareMode::Auto;
  }
  if (name == "on") {
    return SoftwareMode::On;
  }
  if (name == "off") {
    return SoftwareMode::Off;
  }
  return std::nullopt;
}

bool softwareModeActive(SoftwareMode mode, bool cpuDevice, bool autotest) {
  switch (mode) {
  case SoftwareMode::On:
    return true;
  case SoftwareMode::Off:
    return false;
  case SoftwareMode::Auto:
    return cpuDevice && !autotest;
  }
  return false;
}

unsigned softwareThreads(std::optional<std::string_view> lpNumThreads, unsigned hardware) {
  if (lpNumThreads) {
    unsigned threads = 0;
    const char* first = lpNumThreads->data();
    const char* last = first + lpNumThreads->size();
    const auto [end, error] = std::from_chars(first, last, threads);
    if (error == std::errc{} && end == last && threads > 0) {
      return threads;
    }
  }
  return std::max(hardware, 1U);
}

int startQuality(unsigned threads) {
  if (threads <= 2) {
    return 0;
  }
  if (threads <= 4) {
    return 1;
  }
  if (threads <= 8) {
    return 2;
  }
  return 3;
}

QualityStep qualityStep(int level) {
  return Ladder.at(static_cast<std::size_t>(std::clamp(level, LowestQuality, HighestQuality)));
}

GraphicsOptions withQuality(GraphicsOptions options, int level, GraphicsKnobs knobs) {
  const QualityStep step = qualityStep(level);
  const auto on = [&knobs](GraphicsKnob knob) { return knobs.test(knobIndex(knob)); };
  if (on(GraphicsKnob::RenderScale)) {
    options.renderScale = step.renderScale;
  }
  if (on(GraphicsKnob::Shadows)) {
    options.shadows = step.shadows;
  }
  if (on(GraphicsKnob::TextureFilter)) {
    options.textureFilter = step.textures;
  }
  if (on(GraphicsKnob::EnhancedLighting)) {
    options.enhancedLighting = step.lighting;
  }
  if (on(GraphicsKnob::VividColors)) {
    options.vividColors = step.vivid;
  }
  if (on(GraphicsKnob::Antialiasing)) {
    options.antialiasing = false;
  }
  if (on(GraphicsKnob::SceneryQuality)) {
    options.sceneryQuality = SceneryQuality::Smooth;
  }
  if (on(GraphicsKnob::FrameLimit)) {
    options.frameLimit = SoftwareFrameLimit;
  }
  return options;
}

QualityDecision decideQuality(std::span<const double> busySeconds, double targetFps, int level,
                              int ceiling) {
  QualityDecision decision{.level = level, .ceiling = ceiling};
  if (busySeconds.empty() || targetFps <= 0.0) {
    return decision;
  }
  const double budget = 1.0 / targetFps;
  const double mean = std::accumulate(busySeconds.begin(), busySeconds.end(), 0.0) /
                      static_cast<double>(busySeconds.size());
  if (mean > budget * DownMargin) {
    if (level > LowestQuality) {
      decision.level = level - 1;
      decision.ceiling = std::min(ceiling, level - 1);
    }
    return decision;
  }
  std::vector<double> sorted(busySeconds.begin(), busySeconds.end());
  const auto at =
      static_cast<std::size_t>(std::floor(0.9 * static_cast<double>(sorted.size() - 1)));
  std::ranges::nth_element(sorted, sorted.begin() + static_cast<std::ptrdiff_t>(at));
  if (sorted[at] < budget * UpHeadroom && level < std::min(ceiling, HighestQuality)) {
    decision.level = level + 1;
  }
  return decision;
}

QualityTuner::QualityTuner(int level, double targetFps)
    : decision_{.level = std::clamp(level, LowestQuality, HighestQuality),
                .ceiling = HighestQuality},
      targetFps_(targetFps) {}

std::optional<int> QualityTuner::frame(double frameSeconds, double busySeconds) {
  if (frameSeconds > HitchSeconds) {
    pause();
    return std::nullopt;
  }
  if (settleLeft_ > 0.0) {
    settleLeft_ -= frameSeconds;
    return std::nullopt;
  }
  busy_.push_back(std::max(busySeconds, 0.0));
  windowSeconds_ += frameSeconds;
  if (windowSeconds_ < WindowSeconds || busy_.size() < MinFrames) {
    return std::nullopt;
  }
  const QualityDecision next = decideQuality(busy_, targetFps_, decision_.level, decision_.ceiling);
  busy_.clear();
  windowSeconds_ = 0.0;
  const bool changed = next.level != decision_.level;
  decision_ = next;
  if (!changed) {
    return std::nullopt;
  }
  settleLeft_ = SettleSeconds;
  return decision_.level;
}

void QualityTuner::pause() {
  busy_.clear();
  windowSeconds_ = 0.0;
}

double FrameLimiter::wait(double now, int framesPerSecond) {
  if (framesPerSecond <= 0) {
    next_.reset();
    return 0.0;
  }
  const double period = 1.0 / static_cast<double>(framesPerSecond);
  if (!next_ || now - *next_ > period) {
    next_ = now;
  }
  const double waiting = std::max(0.0, *next_ - now);
  *next_ += period;
  return waiting;
}

} // namespace dwe::app
