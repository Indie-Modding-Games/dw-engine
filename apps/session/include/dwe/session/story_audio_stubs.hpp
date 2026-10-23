#pragma once

#include <dwe/core/error.hpp>
#include <dwe/script/script_vm.hpp>

// Stand-ins for the story's sound and music orders until the audio lane publishes the real ones,
// which replace this file: each logs its call and does nothing. Without set_bgm the map-head
// section of almost every map fails right after set_script.
namespace dwe::session {

// Publishes play_sound, set_bgm and stop_bgm in the story namespace, each only if no other system
// published it first.
core::Result<void> publishStoryAudioStubs(script::ScriptVm& vm);

} // namespace dwe::session
