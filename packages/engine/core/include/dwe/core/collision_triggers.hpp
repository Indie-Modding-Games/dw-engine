#pragma once

#include <cstdint>

// The collision values of a map cell that make the player's cell do something (tamer.c:
// checkMapInteraction), shared by the extraction (formats) and the field (game) so the two read the
// cells the same way.
namespace dwe::core::collision {

// 51-79: stepping on the cell runs the section of that number of the map's script.
inline constexpr std::int32_t FirstStepTrigger = 51;
// 80-109: pressing confirm on the cell runs the section of that number.
inline constexpr std::int32_t FirstConfirmTrigger = 80;
// 110-119: the cell takes the map's table warp of exit (value - 110).
inline constexpr std::int32_t FirstWarpTrigger = 110;
inline constexpr std::int32_t WarpTriggerCount = 10;
// 120: the toilet; the partner that needs it goes (map section 0x4e2).
inline constexpr std::int32_t ToiletTrigger = 120;

} // namespace dwe::core::collision
