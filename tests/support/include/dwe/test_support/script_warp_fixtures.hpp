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

// The `field/script_warps` of a synthetic map script with the transport menu and an NPC: map 5, named "house", loads
// script 1, whose confirm section 81 calls the transport routine over a two-entry table (to exit 9 of "farm", map 8,
// behind trigger 300 and 1000 of money; to exit 0 of "town", map 7, behind 301 and 1500), and whose section 3, which
// an NPC runs, changes the map to exit 2 of "town". The formats tests check that the walk gives this text; the field
// tests, that the field reads the talk warp and the transport records.
inline constexpr std::string_view TransportWarpHouse = "house";
inline constexpr std::string_view TransportWarpTown = "town";
inline constexpr std::string_view TransportWarpFarm = "farm";
inline constexpr std::string_view TransportWarpYaml = R"(warps:
- map: house
  via: talk
  section: 3
  to:
    map: town
    exit: 2
  source:
    script: 1
    section: 3
    offset: 16
- map: house
  via: confirm
  section: 81
  to:
    map: farm
    exit: 9
  guard:
    terms:
    - if:
      - test: trigger
        trigger: 300
      holds: true
    - if:
      - test: money
        compare: ge
        value: 1000
      holds: true
    - selection: 0
  transport:
    entry: 0
    cost: 1000
    hand_off: 1251
  source:
    script: 1
    section: 81
    offset: 12
- map: house
  via: confirm
  section: 81
  to:
    map: town
    exit: 0
  guard:
    terms:
    - if:
      - test: trigger
        trigger: 301
      holds: true
    - if:
      - test: money
        compare: ge
        value: 1500
      holds: true
    - selection: 1
  transport:
    entry: 1
    cost: 1500
    hand_off: 1251
  source:
    script: 1
    section: 81
    offset: 12
)";

} // namespace dwe::test_support
