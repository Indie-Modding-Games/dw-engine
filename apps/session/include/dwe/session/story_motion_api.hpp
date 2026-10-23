#pragma once

#include <array>
#include <string_view>

// The names of the story's scripted moves in the `story` namespace
// (StoryLuaHost::publishMotionApi), as the Lua emitter writes them (story_api_names.hpp).
namespace dwe::session::story_motion_api {

inline constexpr std::string_view Player = "PLAYER";
inline constexpr std::string_view Partner = "PARTNER";
inline constexpr std::string_view LookAt = "entity_look_at_entity";
inline constexpr std::string_view SetRotation = "entity_set_rotation";
inline constexpr std::string_view WalkTo = "entity_walk_to";
inline constexpr std::string_view WalkToEntity = "entity_walk_to_entity";
inline constexpr std::string_view MoveTo = "entity_move_to";
inline constexpr std::string_view MoveToAxis = "entity_move_to_axis";
inline constexpr std::string_view CameraTo = "move_camera_to";
inline constexpr std::string_view CameraToEntity = "move_camera_to_entity";
inline constexpr std::string_view PlayAnimation = "play_animation";
inline constexpr std::string_view WaitForEntity = "wait_for_entity";

inline constexpr std::array Published{Player,   Partner,        LookAt,        SetRotation,
                                      WalkTo,   WalkToEntity,   MoveTo,        MoveToAxis,
                                      CameraTo, CameraToEntity, PlayAnimation, WaitForEntity};

} // namespace dwe::session::story_motion_api
