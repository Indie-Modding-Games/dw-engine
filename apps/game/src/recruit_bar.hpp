#pragma once

#include <dwe/ui/ui.hpp>

#include <string_view>
#include <variant>

namespace dwe::app {

// The recruit bars (an engine improvement, one option, on unless turned off): while a job or a
// match decides whether someone joins the city, a bar shows how close the player is (the canyon
// shop's job: shop_job_bar.hpp; the stones game: curling_recruit_bar.hpp). They change no rule.
// The option keeps the key it was added with.
inline constexpr std::string_view RecruitBarSetting = "shop_job_bar";

// Whether the option is on, as the game published it to the UI (on until it is published).
[[nodiscard]] inline bool recruitBarOn(const ui::Ui& ui) {
  const ui::UiValue value = ui.setting(RecruitBarSetting);
  const bool* on = std::get_if<bool>(&value);
  return on == nullptr || *on;
}

} // namespace dwe::app
