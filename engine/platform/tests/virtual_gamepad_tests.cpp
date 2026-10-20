#include <dwe/platform/platform.hpp>

#include <SDL3/SDL_hints.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <variant>
#include <vector>

namespace dwe::platform {
namespace {

// Polls until an event matches, a few times, because SDL may need more than one pump to report a change.
template <typename T, typename Predicate>
bool pollFor(Platform& platform, Predicate predicate) {
    constexpr int Attempts = 20;
    std::vector<Event> events;
    for (int attempt = 0; attempt < Attempts; ++attempt) {
        events.clear();
        platform.pollEvents(events);
        const bool found = std::ranges::any_of(events, [&](const Event& event) {
            const auto* typed = std::get_if<T>(&event);
            return typed != nullptr && predicate(*typed);
        });
        if (found) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("a virtual gamepad reports its family, buttons and axes like a real one") {
    // No window is needed, so the tests run without a display.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    auto platform = Platform::create();
    REQUIRE(platform.has_value());

    auto gamepad = platform->attachVirtualGamepad(GamepadFamily::PlayStation);
    REQUIRE(gamepad.has_value());
    CHECK(pollFor<GamepadConnected>(*platform, [&](const GamepadConnected& connected) {
        return connected.id == *gamepad && connected.family == GamepadFamily::PlayStation;
    }));

    platform->setVirtualGamepadButton(*gamepad, GamepadButton::DpadDown, true);
    CHECK(pollFor<GamepadButtonChanged>(*platform, [&](const GamepadButtonChanged& changed) {
        return changed.id == *gamepad && changed.button == GamepadButton::DpadDown && changed.down;
    }));
    platform->setVirtualGamepadButton(*gamepad, GamepadButton::DpadDown, false);
    CHECK(pollFor<GamepadButtonChanged>(*platform, [&](const GamepadButtonChanged& changed) {
        return changed.button == GamepadButton::DpadDown && !changed.down;
    }));

    platform->setVirtualGamepadAxis(*gamepad, GamepadAxis::LeftY, -1.0F);
    CHECK(pollFor<GamepadAxisMoved>(*platform, [&](const GamepadAxisMoved& moved) {
        return moved.axis == GamepadAxis::LeftY && moved.value < -0.99F;
    }));

    platform->detachVirtualGamepad(*gamepad);
    CHECK(pollFor<GamepadDisconnected>(
        *platform, [&](const GamepadDisconnected& disconnected) { return disconnected.id == *gamepad; }));
}

} // namespace dwe::platform
