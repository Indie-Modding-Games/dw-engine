#include <dwe/core/log.hpp>
#include <dwe/game/story/dialogue.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/tournament_adapter.hpp>

#include <memory>
#include <string>
#include <utility>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 22;

// scriptCheckTournamentMedal's state 1 (SCRIPT_TEXTBOX_MODE = 1): the script waits until the
// player closes the medal's text box.
class MedalTextTask final : public game::story::NextFrameTask {
public:
  MedalTextTask(StoryAdapter& story, std::string line) : story_{&story}, line_{std::move(line)} {}

protected:
  game::story::TaskStep step() override {
    game::story::Dialogue& dialogue = story_->dialogue();
    if (!shown_) {
      shown_ = true;
      // showMapHeadTextbox(result, 0xff, 0, 0x4db): no speaker.
      dialogue.setOwner(game::story::NoSpeaker);
      static_cast<void>(dialogue.showText(line_));
      return game::story::TaskRunning{};
    }
    return dialogue.poll() ? game::story::TaskStep{game::story::TaskDone{}}
                           : game::story::TaskStep{game::story::TaskRunning{}};
  }

private:
  StoryAdapter* story_;
  std::string line_;
  bool shown_ = false;
};

} // namespace

// Routine 22: the tournaments' medal (tournament.c:242 scriptCheckTournamentMedal). When no
// condition earns one (checkTournamentMedalConditions gives -1) the script goes on at once; else
// the medal is given (unlockMedal) and its line shown, and the script waits for it to close.
core::Result<void> registerTournamentMedalRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        if (scenes.tournament == nullptr || scenes.session == nullptr ||
            scenes.desc.story == nullptr) {
          core::logDebug("[port:story] call_routine 22 (script_interp.c:1662): no tournaments; "
                         "it goes on");
          return game::story::RoutineGoesOn{};
        }
        auto line = scenes.tournament->awardMedal(*scenes.session);
        if (!line) {
          core::logWarning("[port:tournament] scriptCheckTournamentMedal (tournament.c:248): {}",
                           line.error().message);
          return game::story::RoutineGoesOn{};
        }
        if (!*line) {
          core::logDebug("[port:tournament] checkTournamentMedalConditions (tournament.c:168): "
                         "no medal");
          return game::story::RoutineGoesOn{};
        }
        return game::story::RoutineWaits{
            .task = std::make_shared<MedalTextTask>(*scenes.desc.story, std::move(**line))};
      });
}

} // namespace dwe::session
