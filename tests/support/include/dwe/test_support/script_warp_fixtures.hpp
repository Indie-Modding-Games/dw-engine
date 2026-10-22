#pragma once

#include <string_view>

namespace dwe::test_support {

// The `field/script_warps` that the extraction gives for a synthetic map script, and that the field reads: map 5,
// named "house", loads script 1, whose step section 51 calls a routine that calls itself before it changes the map to
// exit 2 of screen 7, named "town". The formats tests check that the walk gives this text; the field tests, that the
// field takes the warp under the vanilla policy and logs it once.
inline constexpr std::string_view RecursionWarpHouse = "house";
inline constexpr std::string_view RecursionWarpTown = "town";
inline constexpr std::string_view RecursionWarpSite = "script 1, offset 0x000e";
inline constexpr std::string_view RecursionWarpYaml = R"(warps:
- map: house
  via: step
  section: 51
  to:
    map: town
    exit: 2
  guard:
    terms:
    - recursion:
        script: 1
        offset: 14
  source:
    script: 1
    section: 51
    offset: 18
)";

} // namespace dwe::test_support
