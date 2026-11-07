#pragma once

#include <dwe/platform/events.hpp>

// Keyboard keys by position, as USB HID usage IDs (the values SDL scancodes use). Named after the
// key on a US layout; on other layouts the binding stays at the same physical place.
namespace dwe::input::keys {

using platform::KeyCode;

constexpr KeyCode A = 4;
constexpr KeyCode D = 7;
constexpr KeyCode E = 8;
constexpr KeyCode F = 9;
constexpr KeyCode M = 16;
constexpr KeyCode N = 17;
constexpr KeyCode P = 19;
constexpr KeyCode Q = 20;
constexpr KeyCode R = 21;
constexpr KeyCode S = 22;
constexpr KeyCode W = 26;
constexpr KeyCode Digit1 = 30;
constexpr KeyCode Digit2 = 31;
constexpr KeyCode Digit3 = 32;
constexpr KeyCode Return = 40;
constexpr KeyCode Escape = 41;
constexpr KeyCode Backspace = 42;
constexpr KeyCode Tab = 43;
constexpr KeyCode Space = 44;
constexpr KeyCode Grave = 53;
constexpr KeyCode F1 = 58;
constexpr KeyCode F3 = 60;
constexpr KeyCode F10 = 67;
constexpr KeyCode F12 = 69;
constexpr KeyCode Pause = 72;
constexpr KeyCode Home = 74;
constexpr KeyCode Delete = 76;
constexpr KeyCode Right = 79;
constexpr KeyCode Left = 80;
constexpr KeyCode Down = 81;
constexpr KeyCode Up = 82;
constexpr KeyCode LeftCtrl = 224;
constexpr KeyCode LeftShift = 225;
constexpr KeyCode RightCtrl = 228;
constexpr KeyCode RightShift = 229;

} // namespace dwe::input::keys
