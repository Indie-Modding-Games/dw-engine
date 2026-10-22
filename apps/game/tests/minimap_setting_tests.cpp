#include <dwe/core/log.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "minimap_setting.hpp"
#include "user_settings.hpp"

namespace {

// The warnings written to the log since it was made.
class Warnings {
public:
    Warnings() {
        static const bool added = [] {
            dwe::core::addLogSink([](dwe::core::LogLevel level, std::string_view message) {
                if (level == dwe::core::LogLevel::Warning) {
                    lines().emplace_back(message);
                }
            });
            return true;
        }();
        static_cast<void>(added);
        lines().clear();
    }

    [[nodiscard]] static bool any(std::string_view text) {
        return std::ranges::any_of(lines(), [text](const std::string& line) { return line.contains(text); });
    }

    [[nodiscard]] static bool none() { return lines().empty(); }

private:
    static std::vector<std::string>& lines() {
        static std::vector<std::string> written;
        return written;
    }
};

} // namespace

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

TEST_CASE("the run starts on the saved minimap mode, on auto when none is saved or the saved one is unknown") {
    using dwe::app::startMinimapMode;
    const Warnings warnings;
    CHECK(startMinimapMode(std::nullopt) == dwe::app::MinimapAuto);
    CHECK(startMinimapMode(std::string{"off"}) == dwe::app::MinimapOff);
    CHECK(startMinimapMode(std::string{"on"}) == dwe::app::MinimapOn);
    CHECK(startMinimapMode(std::string{"auto"}) == dwe::app::MinimapAuto);
    CHECK(Warnings::none());
    CHECK(startMinimapMode(std::string{"sometimes"}) == dwe::app::MinimapAuto);
    CHECK(Warnings::any("sometimes"));
}

TEST_CASE("a minimap mode the player picks is saved and the next run starts on it") {
    const dwe::test_support::TempFolder folder;
    const std::filesystem::path file = folder.path() / "settings.yaml";
    dwe::app::UserSettings settings{
        .language = std::string{"es"}, .textFont = std::nullopt, .uiSkin = std::nullopt, .minimap = std::nullopt};
    dwe::app::MinimapChanger changer{settings, file};
    CHECK(changer.change(dwe::ui::UiValue{std::string{"off"}}) == dwe::app::MinimapOff);
    const dwe::app::UserSettings read = dwe::app::loadUserSettings(file);
    CHECK(read.minimap == std::optional<std::string>{"off"});
    // The other choices stay as they were.
    CHECK(read.language == std::optional<std::string>{"es"});
    CHECK(dwe::app::startMinimapMode(read.minimap) == dwe::app::MinimapOff);
    // The old boolean command saves the mode it means.
    CHECK(changer.change(dwe::ui::UiValue{true}) == dwe::app::MinimapOn);
    CHECK(dwe::app::loadUserSettings(file).minimap == std::optional<std::string>{"on"});

    // A run that must not touch the user's settings changes the mode without saving it.
    dwe::app::UserSettings unsaved;
    dwe::app::MinimapChanger autotest{unsaved, std::nullopt};
    CHECK(autotest.change(dwe::ui::UiValue{std::string{"off"}}) == dwe::app::MinimapOff);
    CHECK(dwe::app::loadUserSettings(file).minimap == std::optional<std::string>{"on"});
}
