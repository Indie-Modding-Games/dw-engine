#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::screens {

// The small charge gauge over the partner while its finisher's aura plays (command_menu.c:
// BTL_initializeFinisherChargeup, BTL_renderFinisherChargeup): a 46 x 12 frame with a 4 x 6 bar for
// every 8 of the charge. FinisherGaugeSetting packs where its top-left corner stands, x and y in
// percent of the window, empty while it does not show. It goes with the HP bars' revision
// (HpBarsRevisionSetting).
inline constexpr std::string_view FinisherGaugeSetting = "battle.finisher.gauge";

// The point the gauge hangs from: over the partner's place, its species' height and this much
// more (BTL_initializeFinisherChargeup: vy = -height - 0x64).
inline constexpr std::int32_t FinisherGaugeLift = 100;

// Where the gauge's top-left corner stands for that point as it showed when the aura started (the
// original reads it once): 23 pixels left of it and 20 up (BTL_FINISHER_CHARGEUP_POS = the point
// less 0xb7 and 0x8c, with the drawing offset's 160 and 120 back), held within 100 pixels of the
// original's screen's middle each way.
[[nodiscard]] WindowFraction finisherGaugeCornerOf(WindowFraction point, double aspect);

// The gauge as the settings the battle document reads.
[[nodiscard]] ScreenSettings finisherGaugeSettingsOf(const std::optional<WindowFraction>& corner);

} // namespace dwe::screens
