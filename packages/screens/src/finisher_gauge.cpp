#include <dwe/screens/finisher_gauge.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <string>

namespace dwe::screens {
namespace {

// The original's screen: 240 lines, its middle the drawing offset.
constexpr double ScreenLines = 240.0;
constexpr double Middle = 0.5;
// BTL_initializeFinisherChargeup: the point less (0xb7 - 160, 0x8c - 120), within 100 each way.
constexpr std::int32_t Left = 0xb7 - 160;
constexpr std::int32_t Up = 0x8c - 120;
constexpr std::int32_t Bound = 100;

constexpr double Percent = 100.0;

} // namespace

WindowFraction finisherGaugeCornerOf(WindowFraction point, double aspect) {
  const double across = ScreenLines * aspect;
  const auto x = std::clamp(
      static_cast<std::int32_t>(std::lround((point.x - Middle) * across)) - Left, -Bound, Bound);
  const auto y = std::clamp(
      static_cast<std::int32_t>(std::lround((point.y - Middle) * ScreenLines)) - Up, -Bound, Bound);
  return {.x = Middle + (static_cast<double>(x) / across),
          .y = Middle + (static_cast<double>(y) / ScreenLines)};
}

ScreenSettings finisherGaugeSettingsOf(const std::optional<WindowFraction>& corner) {
  std::string text;
  if (corner) {
    std::format_to(std::back_inserter(text), "{:.2f} {:.2f}", corner->x * Percent,
                   corner->y * Percent);
  }
  return {{.name = std::string{FinisherGaugeSetting}, .value = std::move(text)}};
}

} // namespace dwe::screens
