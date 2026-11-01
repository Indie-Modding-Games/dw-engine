#pragma once

#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/shop_counter.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include "field_scene.hpp"

namespace dwe::app {

inline constexpr std::string_view MoneyBoxDocument = "money_box";
inline constexpr std::string_view MoneyBoxRevisionSetting = "money_box.revision";

// What the money box says: the keeper's money line and the money when the story wrote it
// (tickShopBitsBox writes the line once, on each createShopBitsBox).
[[nodiscard]] screens::ScreenSettings moneyBoxSettings(std::string bitsKey, std::int64_t money);

struct MoneyBoxScreenDesc {
  // They must outlive the screen; without a shop the box shows the money alone.
  session::ShopCounter* shop = nullptr;
  ui::Ui* ui = nullptr;
};

// The money box the story opens over its talk (call_routine 15 -> script_common.c:
// createShopBitsBox(1), tickShopBitsBox) and closes (call_routine 16 -> triggerBoxCloseFlag(2)),
// on the money_box document: the keeper's "Bits" line with the money. It takes no input and the
// story does not wait on it.
class MoneyBoxScreen {
public:
  explicit MoneyBoxScreen(MoneyBoxScreenDesc desc);

  // Takes the story's MoneyBox and CloseTextBox screens; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const FieldScene& scene);
  [[nodiscard]] bool shown() const { return shown_; }
  void close();

private:
  MoneyBoxScreenDesc desc_;
  bool shown_ = false;
};

} // namespace dwe::app
