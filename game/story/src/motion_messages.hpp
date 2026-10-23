#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

// The log messages of the story's scripted moves, as std::format templates for
// core::formatMessage.
namespace dwe::game::story::motion_messages {

using core::formatMessage;

inline constexpr std::string_view NotOnMap =
    "[port:story-motion] {}: character {} is not on the map, the order does nothing "
    "(script_value.c:131 scriptIdToEntityId gives 0xff)";
inline constexpr std::string_view SlotFilled =
    "[port:story-motion] {} (script_interp.c:1297): slot {} character {}";
inline constexpr std::string_view SlotDone =
    "[port:story-motion] tickScriptedMovement (script_interp.c:2275): slot {} character {} done";
inline constexpr std::string_view SlotsCleared =
    "[port:story-motion] callScriptSection (script_interp.c:2244): {} move slots cleared";
inline constexpr std::string_view TalkFacing =
    "[port:story-motion] scriptPauseGame (script_interp.c:2741): player and {} face each other";
inline constexpr std::string_view Animate =
    "[port:story-motion] play_animation (script_interp.c:893 scriptStartAnimation): character {} "
    "animation {}";
inline constexpr std::string_view WalkClip =
    "[port:story-motion] scriptSetEntityWalking (script_interp.c:879): character {} walk "
    "animation {} plays clip {} at {} a second";
inline constexpr std::string_view NoFrames =
    "[port:story-motion] {}: {} frames is not a move; the original divides by it, the engine "
    "takes 1";
inline constexpr std::string_view WaitFor =
    "[port:story-motion] wait_for_entity (script_interp.c:1332): id {} waits on {}";
inline constexpr std::string_view WaitNoCharacter =
    "[port:story-motion] wait_for_entity: character {} is not on the map; the original reads "
    "past its slots, the engine does not wait";
inline constexpr std::string_view SyncUnconnected =
    "[port:story-motion] wait_for_entity: the partner's sync has no owner yet (partner routines), "
    "so a cleared sync counts as raised";
inline constexpr std::string_view TargetGone =
    "[port:story-motion] slot {}: target {} is not on the map, the move ends";
inline constexpr std::string_view MoveToCharacter =
    "[port:story-motion] {} (script_interp.c:2018, tamer.c:1125 tickEntityMoveTo): the original "
    "ends each axis on 0 instead of the target; the engine ends on the target";
inline constexpr std::string_view MoveToWithCamera =
    "[port:story-motion] entity_move_to_with_camera (script_interp.c:2032): the original files it "
    "as a move to its slot's last target; the engine moves to (x, z)";
inline constexpr std::string_view ResetOrigin =
    "[port:story-motion] reset_entity_origin (script_interp.c:1516, overworld.c:1829 "
    "resetEntityOrigin): character {}";

} // namespace dwe::game::story::motion_messages
