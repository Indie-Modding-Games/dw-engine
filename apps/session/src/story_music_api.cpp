#include <dwe/core/log.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/story_lua.hpp>
#include <dwe/session/story_music_api.hpp>

namespace dwe::session {

core::Result<void> publishStoryMusic(script::ScriptVm& vm, game::story::MusicPort& music) {
  script::ApiRegistry api = vm.api();
  // story.set_bgm(font), op 0x5D: as script_interp.c: scriptInstruction5Ato5F, it hands the font
  // to playBGM and goes on.
  auto setBgm = api.publish(
      StoryNamespace, story_api::SetBgm,
      [&music](const script::ApiCall& call) -> core::Result<script::ApiValue> {
        const auto font = call.integer(0);
        if (!font) {
          return std::unexpected{font.error()};
        }
        core::logDebug("[port:audio] scriptInstruction5Ato5F (script_interp.c:1569): set_bgm({})",
                       *font);
        music.playBgm(*font);
        return script::ApiValue{};
      });
  if (!setBgm) {
    return setBgm;
  }
  // story.stop_bgm(operand), op 0x5E: the operand is read and not used (script_interp.c:1573).
  return api.publish(StoryNamespace, story_api::StopBgm,
                     [&music](const script::ApiCall&) -> core::Result<script::ApiValue> {
                       core::logDebug("[port:audio] scriptInstruction5Ato5F "
                                      "(script_interp.c:1573): stop_bgm");
                       music.resetBgm();
                       return script::ApiValue{};
                     });
}

} // namespace dwe::session
