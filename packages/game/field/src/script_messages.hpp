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
inline constexpr std::string_view MapObjects =
    "[port:field-objects] loadDoors (map_object.c:309): door set {}, {} objects";
inline constexpr std::string_view NoMapObject =
    "[port:field-objects] tickRotateDoor (map_object.c:932): no 3D object in slot {}, the turn "
    "ends at once";
inline constexpr std::string_view TurnNotPorted =
    "[port:field-objects] tickRotateDoor (map_object.c:932): slot {} model {} toward {}: the "
    "turns are not ported yet (no map of the opening or the first zone turns one), it ends at once";

} // namespace dwe::game::field::script_messages
