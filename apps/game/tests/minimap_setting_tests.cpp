#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "minimap_setting.hpp"

TEST_CASE("the minimap mode keeps the old boolean setting's meaning and leaves an unset one to the skin") {
    using dwe::app::minimapModeOf;
    using dwe::ui::UiValue;
    CHECK(minimapModeOf(UiValue{true}) == dwe::app::MinimapOn);
    CHECK(minimapModeOf(UiValue{false}) == dwe::app::MinimapOff);
    CHECK(minimapModeOf(UiValue{}) == dwe::app::MinimapAuto);
    CHECK(minimapModeOf(UiValue{std::string{"on"}}) == dwe::app::MinimapOn);
    CHECK(minimapModeOf(UiValue{std::string{"off"}}) == dwe::app::MinimapOff);
    CHECK(minimapModeOf(UiValue{std::string{"auto"}}) == dwe::app::MinimapAuto);
    CHECK(minimapModeOf(UiValue{std::string{"sometimes"}}) == dwe::app::MinimapAuto);
    CHECK(minimapModeOf(UiValue{1.0}) == dwe::app::MinimapAuto);
}
