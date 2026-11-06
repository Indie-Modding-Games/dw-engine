#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

// The messages of the routines, the scripted battle and the partner's routines.
namespace dwe::game::story::messages {

using core::formatMessage;

inline constexpr std::string_view RoutineTaken = "routine {} already has a handler";
inline constexpr std::string_view RoutineEmpty = "routine {} has an empty handler";
inline constexpr std::string_view PortCallRoutine =
    "[port:story] call_routine (script_interp.c:1658): routine {}";
inline constexpr std::string_view PortRoutineUnknown =
    "[port:story] call_routine (script_interp.c:1658): routine {} has no handler, goes on";

// The scripted battle.
inline constexpr std::string_view PortStartBattle =
    "[port:battle] start_battle (script_interp.c:1904): battles started {}, predefined={}";
inline constexpr std::string_view PortBattleEnemy =
    "[port:battle] start_battle (script_interp.c:1912): var {} script id {} is entity {}";
inline constexpr std::string_view PortBattleTalkedTo =
    "[port:battle] handleBattleStart (btl.c:262): no predefined battle, the NPC talked to "
    "(script id {}) is entity {}";
inline constexpr std::string_view PortBattleOutcome =
    "[port:battle] start_battle (script_interp.c:1918): outcome {} into var {}";
inline constexpr std::string_view PortBattleDefeat =
    "[port:battle] start_battle defeat (script_interp.c:1919): the script goes on at script {} "
    "section {:#x}";
inline constexpr std::string_view PortBattleFled =
    "[port:battle] start_battle fled (script_interp.c:1928): battles fled {}, back to map {} exit "
    "{}";
inline constexpr std::string_view PortBattleWon =
    "[port:battle] start_battle won (script_interp.c:1944): trigger {} set={}, partner walks "
    "back={}";

// The previous screen.
inline constexpr std::string_view PortEnterScreen =
    "[port:story] changeMap (map.c:5331): screen {} exit {} (previous screen {} exit {})";
inline constexpr std::string_view PortScreenStart =
    "[port:story] changeMap (map.c:5331): on screen {} with no place kept: it is the previous "
    "screen too, at exit {}";
inline constexpr std::string_view PortScreenOverride =
    "[port:story] changeMap (map.c:5339): from {} to {}, the previous screen is {} exit {}";

// The partner's routines.
inline constexpr std::string_view PortPartnerRoutine =
    "[port:partner] callDigimonRoutine (partner.c:1139): routine {}, target species {}";
inline constexpr std::string_view PartnerRoutineUnknown =
    "[port:partner] callDigimonRoutine (partner.c:1139): routine {} does nothing";

} // namespace dwe::game::story::messages
