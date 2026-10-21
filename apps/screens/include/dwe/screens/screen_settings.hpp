#pragma once

#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::screens {

// One value a screen shows, as the UI setting its document reads (ui.setting(name)).
struct ScreenSetting {
    std::string name;
    ui::UiValue value;

    friend bool operator==(const ScreenSetting&, const ScreenSetting&) = default;
};

using ScreenSettings = std::vector<ScreenSetting>;

// The name of an entry of a list a screen shows: "<list>.<index>.<field>".
[[nodiscard]] std::string listEntryName(std::string_view list, std::size_t index, std::string_view field);

// Publishes a screen's values to the UI. When any of them changed it then counts up the screen's revision setting,
// the one its document refreshes on, so the document redraws once per change instead of once per value.
void publish(ui::Ui& ui, const ScreenSettings& settings, std::string_view revision);

} // namespace dwe::screens
