#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/game/shop_job/shop_job.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::session {
class ShopJobAdapter;
class StoryAdapter;
} // namespace dwe::session

namespace dwe::app {

class ShopJobRulesPanel;

inline constexpr std::string_view ShopHaggleDocument = "shop_haggle";
inline constexpr std::string_view ShopHaggleRevisionSetting = "shop_haggle.revision";

// What the haggle screen names: the customer's species and the item (text keys) and the item's
// icon.
struct ShopHaggleNames {
  std::string species{};
  std::string item{};
  std::string itemIcon{};
  std::string itemIconRect{};
};

// One counter turn as the screen shows it.
struct ShopHaggleView {
  const game::shop_job::ShopJobRules* rules = nullptr;
  const game::shop_job::Haggle* haggle = nullptr;
  ShopHaggleNames names{};
  game::shop_job::PayTiers tiers{};
  std::int64_t tally = 0;
  // The line's perfect score, the customer at the counter (from 1) and the line's length.
  std::int64_t perfectScore = 0;
  std::size_t customer = 0;
  std::size_t customers = 0;
};

// The haggle screen's settings (ui/shop_haggle, _shopjob/ui-contract.md): the customer, the item,
// the nine price rows with their profit, chance and likely reaction, the offers so far and the
// tally against the pay tiers.
[[nodiscard]] screens::ScreenSettings shopHaggleSettings(const ShopHaggleView& view);

struct ShopHaggleScreenDesc {
  // All must outlive the screen; the tables may be null (the names stay blank).
  ui::Ui* ui = nullptr;
  session::ShopJobAdapter* job = nullptr;
  const session::StoryAdapter* story = nullptr;
  const assets::DigimonTable* species = nullptr;
  const assets::ItemTable* items = nullptr;
  ShopJobRulesPanel* rules = nullptr;
};

// The improved shop job's counter (engine routine 259, StoryScreen::ShopHaggle): one turn per
// opening. The player offers a price, talks or sends the customer away; the screen closes and the
// story's patch plays the turn's lines, then opens it again for the next turn. The rules panel
// opens by itself at the job's first customer.
class ShopHaggleScreen {
public:
  explicit ShopHaggleScreen(ShopHaggleScreenDesc desc);

  // Takes the story's ShopHaggle screen and shows the counter; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const session::StoryScenes& scenes);
  // The document's commands: an offer, a talk, a dismissal or the rules panel.
  bool handle(const ui::UiCommand& command);
  // Tells the story the turn played and closes its screen, once.
  void update(session::StoryScenes& scenes);
  // Shows the counter again (after the rules panel closed over it).
  void refocus();
  [[nodiscard]] bool open() const { return open_; }

private:
  [[nodiscard]] ShopHaggleView view() const;
  void publish() const;

  ShopHaggleScreenDesc desc_;
  game::shop_job::PayTiers tiers_;
  bool open_ = false;
  bool over_ = false;
};

} // namespace dwe::app
