#pragma once

#include <dwe/game/modes/game_mode.hpp>
#include <dwe/ui/ui.hpp>

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "user_settings.hpp"

namespace dwe::app {

inline constexpr std::string_view OptionsVariantSetting = "options.variant";
inline constexpr std::string_view OptionsLockedPrefix = "options.locked.";

inline constexpr std::array<std::string_view, 19> FixableOptions{"difficulty",
                                                                 "rare_items",
                                                                 "free_exploration",
                                                                 "extra_toilets",
                                                                 "choose_egg_after_lives_lost",
                                                                 "fair_sickness_chance",
                                                                 "fair_evolution_average",
                                                                 "fair_move_forgetting",
                                                                 "fair_card_stock",
                                                                 "generous_talk",
                                                                 "quick_battle_start",
                                                                 "skip_evolution",
                                                                 "choice_cancel_closes",
                                                                 "angemon_event_zone",
                                                                 "whamon_in_water",
                                                                 "shop_job_mode",
                                                                 "coop_difficulty",
                                                                 "boosted_techniques",
                                                                 "roaming_rare_chance"};

[[nodiscard]] ui::UiValue pinnedUiValue(std::string_view text);

class ModeOptions {
public:
  ModeOptions(const game::modes::GameMode* mode, ui::Ui* ui);
  ~ModeOptions();
  ModeOptions(const ModeOptions&) = delete;
  ModeOptions& operator=(const ModeOptions&) = delete;
  ModeOptions(ModeOptions&&) = delete;
  ModeOptions& operator=(ModeOptions&&) = delete;

  [[nodiscard]] bool fixes(std::string_view option) const;
  [[nodiscard]] const std::vector<game::modes::OptionPin>& pins() const { return pins_; }

  [[nodiscard]] UserSettings over(const UserSettings& settings) const;

  bool refuse(std::string_view command);

private:
  std::vector<game::modes::OptionPin> pins_;
  ui::Ui* ui_ = nullptr;
  std::vector<std::pair<std::string, ui::UiValue>> before_;
};

} // namespace dwe::app
