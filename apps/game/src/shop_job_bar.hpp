#pragma once

#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/story_scenes.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// The shop job's progress bar, one of the recruit bars (recruit_bar.hpp, the option
// RecruitBarSetting): while the canyon shop's job runs, the HUD shows the tally against what makes
// the shopkeeper join the city, with a mark where the better pay starts, and the customers served.
// It changes no rule: it reads the story's customer count and the shops' tally. With the improved
// job (game::shop_job) the bar runs up to the line's perfect score and shows a third mark, the
// bonus item's; the original job's runs up to the joining tally. A class per pay tier reached.
inline constexpr std::string_view ShopJobRevisionSetting = "shop_job.revision";

struct ShopJobBarView {
  bool shown = false;
  std::int64_t tally = 0;
  // The tally the shopkeeper joins from, and the one the better pay starts from.
  std::int64_t goal = 0;
  std::int64_t betterPay = 0;
  std::int64_t customer = 0;
  std::int64_t customers = 0;
  // The bar's end: the line's perfect score (improved job) or the joining tally.
  std::int64_t top = 0;
  // The bonus item's tally and icon, 0 and empty when the bar has no such mark.
  std::int64_t bonus = 0;
  std::string bonusIcon{};
  std::string bonusIconRect{};
  // The pay tier reached: lowest, better, joins or bonus.
  std::string_view tier{};
  bool improved = false;

  friend bool operator==(const ShopJobBarView&, const ShopJobBarView&) = default;
};

// What the bar reads from the game each frame.
struct ShopJobFrame {
  // The recruit bars' option (RecruitBarSetting).
  bool enabled = true;
  std::string_view map;
  std::int64_t customer = 0;
  std::int64_t tally = 0;
  // The improved job (ShopJobAdapter::improved): its line's perfect score and the bonus item's
  // tally and icon.
  bool improved = false;
  std::int64_t perfectScore = 0;
  std::int64_t bonus = 0;
  std::string bonusIcon{};
  std::string bonusIconRect{};
};

// Follows the shop job: it runs from the first customer (the count going up on the shop's map) to
// the moment the player leaves the map (the ending's warp). A count left over from an earlier job
// shows nothing until the next job counts its first customer.
class ShopJobBar {
public:
  explicit ShopJobBar(std::optional<session::ShopJobRules> rules);

  [[nodiscard]] ShopJobBarView update(const ShopJobFrame& frame);

private:
  std::optional<session::ShopJobRules> rules_;
  std::optional<std::int64_t> lastCustomer_;
  bool running_ = false;
};

[[nodiscard]] screens::ScreenSettings settingsOf(const ShopJobBarView& view);

} // namespace dwe::app
