#include <dwe/core/log.hpp>
#include <dwe/game/story/dialogue.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/map_head_lines.hpp>
#include <dwe/session/partner_moves.hpp>
#include <dwe/session/story_adapter.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 47;

void setVariable(StoryScenesContext& scenes, std::int64_t variable, std::int64_t value) {
  if (auto set = scenes.state().setVariable(variable, value); !set) {
    core::logWarning("[port:story] lostAllLives (script_common.c:2456): {}", set.error().message);
  }
}

// lostAllLives's states 2 and 3, from the frame after the order: state 2 forgets one known move at
// random (randomLimit over the hasMove pool, unlearnMove), names it in the move variable and shows
// its box (showMapHeadTextbox(7, 0xff, 0, 0x4d8)); state 3, once the player closed it, counts the
// boxes left down and goes back to state 2 on the next frame, or ends.
class ForgetMovesTask final : public game::story::NextFrameTask {
public:
  ForgetMovesTask(StoryScenesContext& scenes, std::int32_t count, std::string line)
      : scenes_{&scenes}, left_{count}, line_{std::move(line)} {}

protected:
  game::story::TaskStep step() override {
    StoryScenesContext& scenes = *scenes_;
    const StoryForgetMovesRules& rules = *scenes.rules.forgetMoves;
    game::story::Dialogue& dialogue = scenes.desc.story->dialogue();
    if (waiting_) {
      if (!dialogue.poll()) {
        return game::story::TaskRunning{};
      }
      waiting_ = false;
      --left_;
      setVariable(scenes, rules.countVariable, left_);
      return left_ <= 0 ? game::story::TaskStep{game::story::TaskDone{}}
                        : game::story::TaskStep{game::story::TaskRunning{}};
    }
    BattleAdapter& battle = *scenes.desc.battle;
    const std::vector<std::int32_t> known = battle.knownMoveNumbers();
    if (known.empty()) {
      return game::story::TaskDone{};
    }
    const auto pick = static_cast<std::size_t>(
        scenes.session->random().below(static_cast<std::int32_t>(known.size())));
    const std::int32_t move = known.at(pick);
    const std::vector<std::string> forgotten = battle.unlearnMoveNumber(*scenes.session, move);
    core::logDebug("[port:story] lostAllLives (script_common.c:2514): unlearnMove({:#x}) forgot "
                   "{} move(s), {} box(es) left",
                   move, forgotten.size(), left_);
    setVariable(scenes, rules.moveVariable, move);
    dialogue.setOwner(game::story::NoSpeaker);
    static_cast<void>(dialogue.showText(line_));
    waiting_ = true;
    return game::story::TaskRunning{};
  }

private:
  StoryScenesContext* scenes_;
  std::int32_t left_;
  std::string line_;
  bool waiting_ = false;
};

// MAPHEAD's line of the box, or empty when the listing does not have it.
std::string boxLine(const StoryScenesContext& scenes, const StoryForgetMovesRules& rules) {
  auto tables = readMapHeadLineTables(scenes.desc.story->files());
  if (!tables) {
    core::logWarning("[port:story] lostAllLives (script_common.c:2517): {}",
                     tables.error().message);
    return {};
  }
  const auto table = tables->find(rules.linesSection);
  if (table == tables->end() || rules.line < 0 ||
      static_cast<std::size_t>(rules.line) >= table->second.size()) {
    return {};
  }
  return table->second[static_cast<std::size_t>(rules.line)];
}

} // namespace

// Routine 47: after the partner lost all its lives it forgets some of its moves, a box for each,
// and the section waits until the last box is closed (script_interp.c:494, script_common.c:2456
// lostAllLives). State 0 counts them (movesToForget) and goes on at once when there are none.
core::Result<void> registerForgetMovesRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        if (!scenes.rules.forgetMoves || scenes.desc.battle == nullptr ||
            scenes.desc.story == nullptr || scenes.session == nullptr) {
          core::logDebug("[port:story] call_routine 47 (script_interp.c:494): no forget_moves "
                         "rules; it goes on");
          return game::story::RoutineGoesOn{};
        }
        const StoryForgetMovesRules& rules = *scenes.rules.forgetMoves;
        const auto known = static_cast<std::int32_t>(scenes.desc.battle->knownMoveNumbers().size());
        const std::int32_t count = movesToForget(known, scenes.session->tamer().level);
        core::logDebug("[port:story] lostAllLives (script_common.c:2485): {} known, trainer level "
                       "{}, {} to forget",
                       known, scenes.session->tamer().level, count);
        if (count == 0) {
          return game::story::RoutineGoesOn{};
        }
        setVariable(scenes, rules.countVariable, count);
        return game::story::RoutineWaits{
            .task = std::make_shared<ForgetMovesTask>(scenes, count, boxLine(scenes, rules))};
      });
}

} // namespace dwe::session
