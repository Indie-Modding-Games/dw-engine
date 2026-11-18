#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/game/shop_job/shop_job.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::session {
class ShopJobAdapter;
class StoryScenes;
} // namespace dwe::session

namespace dwe::app {

inline constexpr std::string_view ShopJobRulesDocument = "shop_job_rules";
inline constexpr std::string_view ShopJobRulesRevisionSetting = "shop_job_rules.revision";
// The command that opens the panel: the HUD bar's "?" and the haggle screen's menu button.
inline constexpr std::string_view ShopJobRulesCommand = "shop_job_rules";
// The action that opens and closes the panel while the job runs (the "?" key cap).
inline constexpr std::string_view HelpAction = "help";
// What closes the open panel besides confirm and cancel: Escape and the pad's Back.
inline constexpr std::string_view SettingsAction = "settings";

// What the rules panel tells: the pay tiers, the bonus item (text key and icon, empty when the
// job gives none), the line's length and whether the improved job runs.
struct ShopJobRulesView {
  game::shop_job::PayTiers tiers{};
  std::string bonusItem{};
  std::string bonusIcon{};
  std::string bonusIconRect{};
  std::int64_t customers = 0;
  std::int32_t talkNeeds = 0;
  bool improved = false;
};

[[nodiscard]] screens::ScreenSettings shopJobRulesSettings(const ShopJobRulesView& view);

struct ShopJobRulesPanelDesc {
  // All must outlive the panel; the job, the scenes and the items may be null (no tiers to tell).
  ui::Ui* ui = nullptr;
  const session::ShopJobAdapter* job = nullptr;
  const session::StoryScenes* scenes = nullptr;
  const assets::ItemTable* items = nullptr;
};

// The shop job's rules panel (ui/shop_job_rules): how the job pays and how the haggle works. It
// opens from the HUD's "?" or the counter's menu button, and by itself at each job's first
// customer; confirm or cancel closes it.
class ShopJobRulesPanel {
public:
  explicit ShopJobRulesPanel(ShopJobRulesPanelDesc desc) : desc_(desc) {}

  // `closed` runs when the panel closes (the counter takes the focus back).
  void setClosed(std::function<void()> closed) { closed_ = std::move(closed); }
  // What the panel tells now: the story's tiers (session::ShopJobRules) and the job's bonus.
  [[nodiscard]] ShopJobRulesView view() const;
  void show();
  void close();
  bool handle(const ui::UiCommand& command);
  // The keys the document does not get: help opens the panel while `jobRuns` (the HUD bar shows)
  // and nothing else holds the screen but `counterOpen` (the haggle screen); help, Escape and the
  // pad's Back (settings) close it. `canOpen` false: the panel only closes.
  void press(bool helpPressed, bool settingsPressed, bool canOpen);
  [[nodiscard]] bool shown() const { return shown_; }

private:
  ShopJobRulesPanelDesc desc_;
  std::function<void()> closed_;
  bool shown_ = false;
};

} // namespace dwe::app
