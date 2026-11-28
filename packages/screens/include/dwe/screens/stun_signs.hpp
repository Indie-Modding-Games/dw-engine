#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <span>
#include <string_view>

namespace dwe::screens {

// The stun signs over the fighters (battle_effect.c: the stun mark and its countdown, ETCDAT/
// BI_IN.TIM), which the field draws in the world: StunSignsSetting packs each one's box on the
// window, left, top, width and height in percent, so the battle document keeps its bars off them.
// It goes with the HP bars' revision (HpBarsRevisionSetting).
inline constexpr std::string_view StunSignsSetting = "battle.stun_signs";

// The box of a sign standing at `foot` (the foot of its post): its board, 40 pixels of the
// original's 240-line screen wide over a 22-pixel post, 44 tall in all.
[[nodiscard]] ScreenSettings stunSignSettingsOf(std::span<const WindowFraction> feet,
                                                double aspect);

} // namespace dwe::screens
