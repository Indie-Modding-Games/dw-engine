#include <dwe/core/log.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/session/story_audio_stubs.hpp>
#include <dwe/session/story_lua.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::session {

namespace {

using script::ApiCall;
using script::ApiValue;

// The call's integer arguments, for the log.
std::string argumentsOf(const ApiCall& call) {
  std::string text;
  for (std::size_t index = 0; index < call.arguments().size(); ++index) {
    const auto value = call.integer(index);
    text += (index == 0 ? "" : ", ") + (value ? std::to_string(*value) : std::string{"?"});
  }
  return text;
}

} // namespace

core::Result<void> publishStoryAudioStubs(script::ScriptVm& vm) {
  script::ApiRegistry registry = vm.api();
  // script_interp.c: 0x5A play_sound (bank, sound), 0x5D set_bgm (track), 0x5E stop_bgm.
  for (const std::string_view name : {std::string_view{"play_sound"}, std::string_view{"set_bgm"},
                                      std::string_view{"stop_bgm"}}) {
    if (registry.isPublished(StoryNamespace, name)) {
      continue;
    }
    auto published = registry.publish(
        StoryNamespace, name, [name](const ApiCall& call) -> core::Result<ApiValue> {
          core::logDebug("[port:audio] {} ({}): stub, no sound until the audio lane lands", name,
                         argumentsOf(call));
          return ApiValue{};
        });
    if (!published) {
      return published;
    }
  }
  return {};
}

} // namespace dwe::session
