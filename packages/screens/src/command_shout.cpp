#include <dwe/localization/catalog.hpp>
#include <dwe/screens/command_shout.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace dwe::screens {
namespace {

namespace battle = game::battle;

// The original's shouts, one per plain command from run to change (main: BTL_SHOUTS).
constexpr std::string_view ShoutsBlock = "shouts";
// The original's screen is 240 lines tall; getEntityScreenPos counts from its middle.
constexpr double ScreenLines = 240.0;
constexpr double Middle = 0.5;
// BTL_drawCommandShout: the shout's line stays within 100 lines of the middle.
constexpr std::int32_t LowestLine = 100;
constexpr std::int32_t HighestLine = -100;
// BTL_renderCommandShout: a shout over this line drops instead of hopping.
constexpr std::int32_t DropOver = -40;
// command_menu.c: BTL_SHOUT_HOP_OFFSETS and BTL_SHOUT_DROP_OFFSETS.
constexpr std::array<std::int32_t, CommandShout::Frames> HopOffsets{
    0, -8, -14, -20, -25, -30, -34, -36, -38, -39, -40, -39, -38, -36, -34, -30, -34, -36, -38, -39,
};
constexpr std::array<std::int32_t, CommandShout::Frames> DropOffsets{
    0, 1, 2, 4, 6, 10, 15, 20, 26, 32, 40, 36, 34, 32, 31, 30, 31, 32, 34, 36,
};
// BTL_renderCommandShout: ((frame * 2) + 2) / 10 for the first 4 frames, then 1.
constexpr std::int32_t GrowingFrames = 4;
constexpr std::int32_t FullScale = 10;
constexpr double Percent = 100.0;
constexpr double Tenths = 10.0;

double rounded(double value) {
  return std::round(value * Tenths) / Tenths;
}

} // namespace

std::string shoutTextKey(battle::Command command, const battle::Fighter& partner,
                         const NameKeys& names) {
  const auto number = static_cast<std::size_t>(command);
  constexpr auto First = static_cast<std::size_t>(battle::Command::Flee);
  constexpr auto FirstTechnique = static_cast<std::size_t>(battle::Command::Technique1);
  if (number < First) {
    return {};
  }
  if (number < FirstTechnique) {
    return localization::gameTextKey(BattleText, ShoutsBlock, number - First);
  }
  const std::size_t slot =
      command == battle::Command::Finisher ? battle::FinisherSlot : number - FirstTechnique;
  const auto& move = partner.moves.at(slot);
  return move ? names.move(*move) : std::string{};
}

void CommandShout::shout(std::string textKey, std::optional<WindowFraction> node, double aspect) {
  textKey_ = std::move(textKey);
  anchor_ = node.value_or(WindowFraction{.x = Middle, .y = Middle});
  nodeX_ = static_cast<std::int32_t>(std::lround((anchor_.x - Middle) * ScreenLines * aspect));
  nodeY_ = static_cast<std::int32_t>(std::lround((anchor_.y - Middle) * ScreenLines));
  y_ = std::clamp(nodeY_, HighestLine, LowestLine);
  frame_ = 0;
}

void CommandShout::step() {
  if (!frame_) {
    return;
  }
  if (++*frame_ >= Frames) {
    frame_.reset();
  }
}

std::int32_t CommandShout::scaleTenths() const {
  const std::int32_t frame = this->frame();
  return frame < GrowingFrames ? (frame * 2) + 2 : FullScale;
}

std::int32_t CommandShout::hop() const {
  const auto frame = static_cast<std::size_t>(this->frame());
  return y_ < DropOver ? DropOffsets.at(frame) : HopOffsets.at(frame);
}

ScreenSettings CommandShout::settings() const {
  return {
      {.name = std::string{ShoutShownSetting}, .value = shown()},
      {.name = std::string{ShoutTextSetting}, .value = textKey_},
      {.name = std::string{ShoutAnchorXSetting}, .value = rounded(anchor_.x * Percent)},
      {.name = std::string{ShoutAnchorYSetting}, .value = rounded(anchor_.y * Percent)},
      {.name = std::string{ShoutNodeXSetting}, .value = static_cast<double>(nodeX_)},
      {.name = std::string{ShoutNodeYSetting}, .value = static_cast<double>(nodeY_)},
      {.name = std::string{ShoutYSetting}, .value = static_cast<double>(y_)},
      {.name = std::string{ShoutHopSetting}, .value = static_cast<double>(hop())},
      {.name = std::string{ShoutScaleSetting}, .value = scaleTenths() / Tenths},
  };
}

} // namespace dwe::screens
