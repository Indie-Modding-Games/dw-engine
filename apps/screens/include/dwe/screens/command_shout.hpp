#pragma once

#include <dwe/game/battle/state.hpp>
#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::screens {

// The settings of the player's shout on the battle document, which redraws it on
// ShoutRevisionSetting: whether it shows, its text key, the anchor (where the player's node 4
// showed as it was shouted) in percent of the window, the anchor in the original's pixels from the
// middle of its screen (x, y), the shout's centre line in those pixels once held on the screen (y,
// before its hop), the hop and the scale (1 is its full size).
inline constexpr std::string_view ShoutRevisionSetting = "battle.shout.revision";
inline constexpr std::string_view ShoutShownSetting = "battle.shout.shown";
inline constexpr std::string_view ShoutTextSetting = "battle.shout.text";
inline constexpr std::string_view ShoutAnchorXSetting = "battle.shout.anchor_x";
inline constexpr std::string_view ShoutAnchorYSetting = "battle.shout.anchor_y";
inline constexpr std::string_view ShoutNodeXSetting = "battle.shout.node_x";
inline constexpr std::string_view ShoutNodeYSetting = "battle.shout.node_y";
inline constexpr std::string_view ShoutYSetting = "battle.shout.y";
inline constexpr std::string_view ShoutHopSetting = "battle.shout.hop";
inline constexpr std::string_view ShoutScaleSetting = "battle.shout.scale";

// The text the player shouts with a command (command_shout.c: BTL_drawCommandShout): the
// original's shout for a plain command ("btl.shouts.<n>", run to change), the move's name for a
// technique or the finisher; empty when the partner has no move in that slot.
[[nodiscard]] std::string shoutTextKey(game::battle::Command command,
                                       const game::battle::Fighter& partner, const NameKeys& names);

// The player's shout as a command is given (command_shout.c: BTL_drawCommandShout,
// BTL_renderCommandShout). It hangs where the player's model node 4 showed, held 100 lines from the
// middle of the screen at most; over its 20 frames it grows from 0.2 to its full size in the first
// 4 (0.2, 0.4, 0.6, 0.8), and hops up 40 pixels and settles (BTL_SHOUT_HOP_OFFSETS), or drops 40
// when it is high on the screen (over -40, BTL_SHOUT_DROP_OFFSETS). A new shout takes the old one's
// place. Frame by frame at the battle's rate.
class CommandShout {
public:
  static constexpr std::int32_t Frames = 20;

  // BTL_drawCommandShout: `node` is where the player's node 4 shows, as a fraction of the window,
  // and `aspect` the window's width over its height; without a node the shout hangs in the middle.
  void shout(std::string textKey, std::optional<WindowFraction> node, double aspect);
  // BTL_renderCommandShout's count: one battle frame on; the 20th takes it away.
  void step();
  void clear() { frame_.reset(); }

  [[nodiscard]] bool shown() const { return frame_.has_value(); }
  [[nodiscard]] std::int32_t frame() const { return frame_.value_or(0); }
  // The scale in 1/10ths: 2, 4, 6, 8, then 10.
  [[nodiscard]] std::int32_t scaleTenths() const;
  // The hop from the shout's line, in the original's pixels (up is negative).
  [[nodiscard]] std::int32_t hop() const;
  [[nodiscard]] std::int32_t nodeX() const { return nodeX_; }
  [[nodiscard]] std::int32_t nodeY() const { return nodeY_; }
  // The shout's line, held between -100 and 100.
  [[nodiscard]] std::int32_t y() const { return y_; }

  [[nodiscard]] ScreenSettings settings() const;

private:
  std::optional<std::int32_t> frame_;
  std::string textKey_;
  WindowFraction anchor_{.x = 0.5, .y = 0.5};
  std::int32_t nodeX_ = 0;
  std::int32_t nodeY_ = 0;
  std::int32_t y_ = 0;
};

} // namespace dwe::screens
