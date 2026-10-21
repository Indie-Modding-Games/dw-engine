#include <dwe/screens/screen_settings.hpp>

#include <format>
#include <variant>

namespace dwe::screens {

std::string listEntryName(std::string_view list, std::size_t index, std::string_view field) {
    return std::format("{}.{}.{}", list, index, field);
}

void publish(ui::Ui& ui, const ScreenSettings& settings, std::string_view revision) {
    bool changed = false;
    for (const ScreenSetting& setting : settings) {
        if (ui.setting(setting.name) != setting.value) {
            ui.setSetting(setting.name, setting.value);
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    const ui::UiValue current = ui.setting(revision);
    const double* count = std::get_if<double>(&current);
    ui.setSetting(revision, (count == nullptr ? 0.0 : *count) + 1.0);
}

} // namespace dwe::screens
