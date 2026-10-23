#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <array>
#include <string_view>

namespace dwe::script {
class ScriptVm;
} // namespace dwe::script

namespace dwe::session {

namespace story_api {
inline constexpr std::string_view SetBgm = "set_bgm";
inline constexpr std::string_view StopBgm = "stop_bgm";

inline constexpr std::array MusicPublished{SetBgm, StopBgm};
} // namespace story_api

// Publishes the story's music orders in the story namespace of a VM, next to the API of
// StoryLuaHost::publishApi: story.set_bgm(font) and story.stop_bgm(operand), ops 0x5D and 0x5E. The
// music must outlive every script call of the VM.
core::Result<void> publishStoryMusic(script::ScriptVm& vm, game::story::MusicPort& music);

} // namespace dwe::session
