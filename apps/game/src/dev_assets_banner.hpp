#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ui/ui.hpp>

#include <string_view>

namespace dwe::app {

// The UI setting that says whether the dev assets banner is shown, for the autotest's checks (route-dev-assets).
inline constexpr std::string_view DevAssetsBannerSetting = "dev_assets_banner";

// Loads the dev assets banner and shows it over every other document, and logs its text in the player's language: the
// game never uses dev assets in silence. Publishes whether it shows (DevAssetsBannerSetting). Call it before showing a
// modal document, so the banner does not take focus.
core::Result<void> showDevAssetsBanner(ui::Ui& ui);

} // namespace dwe::app
