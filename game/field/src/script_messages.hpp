#pragma once

#include <string_view>

// The log messages of the field's scripted characters and camera, as std::format templates.
namespace dwe::game::field::script_messages {

inline constexpr std::string_view Held =
    "[port:field-script] setMovementEnabled (overworld.c:1869): held={}";
inline constexpr std::string_view Clip =
    "[port:field-script] startAnimation (anim.c:201): entity {} clip {}";
inline constexpr std::string_view Pan =
    "[port:field-script] tickCameraMovement (map.c:4855): pan to ({}, {}) in {} s";

} // namespace dwe::game::field::script_messages
