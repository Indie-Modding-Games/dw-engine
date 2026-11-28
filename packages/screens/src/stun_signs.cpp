#include <dwe/screens/stun_signs.hpp>

#include <format>
#include <iterator>
#include <string>

namespace dwe::screens {
namespace {

constexpr double ScreenLines = 240.0;
constexpr double Width = 40.0;
constexpr double Height = 44.0;
constexpr double Percent = 100.0;

} // namespace

ScreenSettings stunSignSettingsOf(std::span<const WindowFraction> feet, double aspect) {
  const double across = ScreenLines * aspect;
  std::string text;
  for (const WindowFraction& foot : feet) {
    if (!text.empty()) {
      text += ' ';
    }
    std::format_to(std::back_inserter(text), "{:.2f} {:.2f} {:.2f} {:.2f}",
                   (foot.x - (Width / 2.0 / across)) * Percent,
                   (foot.y - (Height / ScreenLines)) * Percent, Width / across * Percent,
                   Height / ScreenLines * Percent);
  }
  return {{.name = std::string{StunSignsSetting}, .value = std::move(text)}};
}

} // namespace dwe::screens
