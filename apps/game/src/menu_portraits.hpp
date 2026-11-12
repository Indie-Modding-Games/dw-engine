#pragma once

#include <dwe/ui/ui.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace dwe::app {

// What the menus ask the game to draw in 3D over them (an addition): the partner on the partner
// menu's status page, the player on the pause menu's player page and the medal the medals page
// holds up. The documents report it as their page changes (ui.report, the UI contract's
// partner_menu.portrait, pause_menu.portrait and pause_menu.medal); the game draws it while the
// document shows, whatever the document's markup.
inline constexpr std::string_view PartnerPortraitReport = "partner_menu.portrait";
inline constexpr std::string_view PlayerPortraitReport = "pause_menu.portrait";
inline constexpr std::string_view MedalPortraitReport = "pause_menu.medal";

struct MenuPortraits {
  bool partner = false;
  bool player = false;
  std::optional<std::int32_t> medal;

  // The portraits from the reported values: a toggle without a yes is off, a medal outside 0 to
  // `medals` - 1 or not a whole number is none.
  static MenuPortraits of(const ui::UiValue& partner, const ui::UiValue& player,
                          const ui::UiValue& medal, std::int32_t medals) {
    MenuPortraits portraits;
    const auto yes = [](const ui::UiValue& value) {
      const bool* on = std::get_if<bool>(&value);
      return on != nullptr && *on;
    };
    portraits.partner = yes(partner);
    portraits.player = yes(player);
    if (const double* value = std::get_if<double>(&medal); value != nullptr && *value >= 0.0 &&
                                                           *value < static_cast<double>(medals) &&
                                                           std::floor(*value) == *value) {
      portraits.medal = static_cast<std::int32_t>(*value);
    }
    return portraits;
  }

  // What the UI's documents report now.
  static MenuPortraits read(const ui::Ui& ui, std::int32_t medals) {
    return of(ui.reported(PartnerPortraitReport), ui.reported(PlayerPortraitReport),
              ui.reported(MedalPortraitReport), medals);
  }
};

} // namespace dwe::app
