#include "minimap_setting.hpp"

#include <string>
#include <variant>

namespace dwe::app {

std::string_view minimapModeOf(const ui::UiValue& value) {
    if (const bool* visible = std::get_if<bool>(&value)) {
        return *visible ? MinimapOn : MinimapOff;
    }
    if (const std::string* mode = std::get_if<std::string>(&value)) {
        if (*mode == MinimapOn) {
            return MinimapOn;
        }
        if (*mode == MinimapOff) {
            return MinimapOff;
        }
    }
    return MinimapAuto;
}

} // namespace dwe::app
