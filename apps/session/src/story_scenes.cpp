#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_lua.hpp>
#include <dwe/session/story_scenes.hpp>

#include <algorithm>
#include <string>
#include <utility>

#include "story_scenes_context.hpp"

namespace dwe::session {

using game::story::BattleOutcome;
using game::story::PartnerScene;
using game::story::ScriptedBattle;

namespace {

// script_value.c: scriptIdToEntityId: the player and the partner have fixed script ids.
constexpr std::int64_t PlayerScriptId = 0xFD;
constexpr std::int64_t PartnerScriptId = 0xFC;
constexpr std::int64_t PlayerEntity = 0;
constexpr std::int64_t PartnerEntity = 1;

} // namespace

// The battles the story asks the app for.
class SceneBattlePort final : public game::story::StoryBattlePort {
public:
  explicit SceneBattlePort(StoryScenesContext& scenes) : scenes_{&scenes} {}

  [[nodiscard]] std::optional<std::int64_t> entityOf(std::int64_t scriptId) const override {
    if (scriptId == PlayerScriptId) {
      return PlayerEntity;
    }
    if (scriptId == PartnerScriptId) {
      return PartnerEntity;
    }
    // The field's NPCs are known to the app by their script id (MapNpc::scriptId), which stands
    // for the entity until the field numbers its NPCs as entities.
    return scriptId;
  }

  void startBattle(const ScriptedBattle& battle) override {
    core::logDebug("[port:battle] startBattle (tamer.c:832): {} predefined enemies, fixed={}",
                   battle.enemies.size(), battle.fixedFight);
    scenes_->battleOutcome.reset();
    scenes_->battleRequest = battle;
  }

  std::optional<BattleOutcome> battleOutcome() override {
    return std::exchange(scenes_->battleOutcome, std::nullopt);
  }

  // The walk back is the scripted movement's (S4): until it moves the partner, it is over at once.
  void startPartnerReturn() override {
    core::logDebug("[port:battle] start_battle won (script_interp.c:1950): the partner's walk back "
                   "is not moved yet, over at once");
  }
  [[nodiscard]] bool partnerReturned() const override { return true; }

private:
  StoryScenesContext* scenes_;
};

// The partner's scenes: the evolutions run in the session, the others on the app's screens.
class ScenePartnerPort final : public game::story::PartnerScenePort {
public:
  explicit ScenePartnerPort(StoryScenesContext& scenes) : scenes_{&scenes} {}

  void startScene(PartnerScene scene, std::optional<std::int64_t> species) override {
    if (scene == PartnerScene::ReturnFromVirus || scene == PartnerScene::EvolveTo) {
      evolve(scene, species);
      scenes_->sync.finish();
      return;
    }
    scenes_->partnerScene = PartnerSceneRequest{.scene = scene};
  }

private:
  void evolve(PartnerScene scene, std::optional<std::int64_t> species) const {
    EvolutionAdapter* evolution = scenes_->desc.evolution;
    GameSession* session = scenes_->session;
    if (evolution == nullptr || session == nullptr) {
      core::logWarning("[port:partner] callDigimonRoutine (partner.c:1153): no evolution to run");
      return;
    }
    if (scene == PartnerScene::ReturnFromVirus) {
      const bool changed = evolution->returnFromVirus(*session);
      core::logDebug("[port:partner] callDigimonRoutine 2 (partner.c:1153): back from the virus, "
                     "changed={}",
                     changed);
      return;
    }
    const std::string target = speciesId(species.value_or(-1));
    auto evolved = evolution->evolveByScript(*session, target);
    core::logDebug("[port:partner] callDigimonRoutine 10 (partner.c:1208): into {} ({}), "
                   "evolved={}",
                   target, species.value_or(-1), evolved.value_or(false));
    if (!evolved) {
      core::logWarning("{}", evolved.error().message);
    }
  }

  [[nodiscard]] std::string speciesId(std::int64_t number) const {
    if (scenes_->digimon) {
      for (const assets::DigimonRow& row : scenes_->digimon->digimon) {
        if (row.index == number) {
          return row.id;
        }
      }
    }
    return std::to_string(number);
  }

  StoryScenesContext* scenes_;
};

game::story::StoryStatePort& StoryScenesContext::state() const {
  game::story::StoryStatePort* port = desc.story->statePort();
  DWE_ASSERT(port != nullptr);
  return *port;
}

game::story::StoryRunner& StoryScenesContext::runner() const {
  game::story::StoryRunner* runner = desc.story->runner();
  DWE_ASSERT(runner != nullptr);
  return *runner;
}

std::int64_t StoryScenesContext::variable(std::int64_t variable) const {
  auto value = state().variable(variable);
  if (!value) {
    core::logWarning("{}", value.error().message);
    return 0;
  }
  return *value;
}

std::int64_t StoryScenesContext::operand() const {
  return variable(rules.operandVariable);
}

bool StoryScenesContext::trigger(std::int64_t trigger) const {
  return state().trigger(trigger).value_or(false);
}

StoryScenes::StoryScenes(StoryScenesDesc desc)
    : context_{std::make_unique<StoryScenesContext>(desc)} {}

StoryScenes::~StoryScenes() = default;

ruleset::SectionChecks StoryScenes::sectionChecks() const {
  return {
      {std::string{StoryScenesSection}, [](const ruleset::Section& section) -> core::Result<void> {
         if (auto read = readStoryScenesRules(section); !read) {
           return std::unexpected{read.error()};
         }
         return {};
       }}};
}

core::Result<void> StoryScenes::readRules(const ruleset::Ruleset& rules) {
  StoryScenesContext& scenes = *context_;
  DWE_ASSERT(scenes.desc.story != nullptr);
  const ruleset::Section* section = rules.section(StoryScenesSection);
  if (section == nullptr) {
    core::logInfo("{}", messages::StoryScenesOff);
    scenes.enabled = false;
    return {};
  }
  auto read = readStoryScenesRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  scenes.rules = std::move(*read);
  scenes.enabled = true;
  scenes.screens = game::story::ScreenHistory{scenes.rules.screens};
  scenes.desc.story->seedNewGame(scenes.rules.newGameChain == NewGameChain::Seeded);
  scenes.digimon.reset();
  if (const assets::AssetId* table = rules.table(game::evolution::DigimonTableRole)) {
    if (auto digimon = assets::loadDigimonTable(scenes.desc.story->files(), *table)) {
      scenes.digimon = std::move(*digimon);
    }
  }
  return {};
}

void StoryScenes::bind(GameSession& session) {
  StoryScenesContext& scenes = *context_;
  scenes.session = &session;
  if (!scenes.enabled || scenes.published || scenes.desc.story->host() == nullptr ||
      scenes.desc.story->statePort() == nullptr) {
    return;
  }
  scenes.published = true;
  scenes.battlePort = std::make_unique<SceneBattlePort>(scenes);
  scenes.partnerPort = std::make_unique<ScenePartnerPort>(scenes);
  scenes.battles = std::make_unique<game::story::StoryBattles>(scenes.state(), *scenes.battlePort,
                                                               scenes.screens, scenes.rules.battle);
  scenes.partner = std::make_unique<game::story::PartnerRoutines>(
      scenes.state(), *scenes.partnerPort, scenes.sync, scenes.rules.partner);
  if (auto registered = registerOpeningRoutines(scenes); !registered) {
    core::logError("{}", registered.error().message);
  }
  StoryAdapter* story = scenes.desc.story;
  auto published = story->host()->publishSceneApi({
      .routines = &scenes.routines,
      .battles = scenes.battles.get(),
      .partner = scenes.partner.get(),
      .freeze = [story] { story->runner()->freezeWorld(); },
  });
  if (!published) {
    core::logError("{}", published.error().message);
  }
}

core::Result<void> StoryScenes::startNewGame(GameSession& session) {
  bind(session);
  StoryScenesContext& scenes = *context_;
  if (!scenes.published) {
    return {};
  }
  scenes.screens.start(scenes.rules.newGameMap, scenes.rules.newGameExit);
  scenes.battles->restoreCounters({});
  scenes.recruits.clear();
  scenes.shopTally = 0;
  scenes.chainPending = scenes.rules.newGameChain == NewGameChain::Original;
  core::logDebug("[port:story] initializeMap (map.c:3936): screen {} exit {}, chain={}",
                 scenes.rules.newGameMap, scenes.rules.newGameExit,
                 scenes.chainPending ? "original" : "seeded");
  return {};
}

void StoryScenes::tick(GameSession& session, const ruleset::ClockAdvance& /*advance*/) {
  bind(session);
  StoryScenesContext& scenes = *context_;
  if (!scenes.published) {
    return;
  }
  if (std::exchange(scenes.chainPending, false)) {
    // main.c: main runs the first map's map-head section once the new game is set up
    // (runMapHeadScript(0xCC)).
    core::logDebug("[port:story] runMapHeadScript (script_interp.c:2235): new game, map {}",
                   scenes.rules.newGameMap);
    scenes.runner().enterMap(scenes.rules.newGameMap);
  }
  watchRecruits(session);
}

void StoryScenes::watchRecruits(GameSession& session) {
  StoryScenesContext& scenes = *context_;
  const auto count = static_cast<std::size_t>(std::max<std::int64_t>(scenes.rules.recruitCount, 0));
  const bool first = scenes.recruits.size() != count;
  scenes.recruits.resize(count, false);
  for (std::size_t species = 0; species < count; ++species) {
    const bool joined =
        scenes.trigger(scenes.rules.firstRecruitTrigger + static_cast<std::int64_t>(species));
    if (joined && !scenes.recruits[species] && !first) {
      // The original's recruits are the story's triggers; the city grows from them in the
      // map-head sections. The engine autosaves on a new resident.
      core::logDebug("[port:story] recruit: species {} joined the city (trigger {})", species,
                     scenes.rules.firstRecruitTrigger + static_cast<std::int64_t>(species));
      session.requestAutosave(game::save::AutosaveReason::RecruitJoin);
    }
    scenes.recruits[species] = joined;
  }
}

std::optional<ScriptedBattle> StoryScenes::takeBattleRequest() {
  return std::exchange(context_->battleRequest, std::nullopt);
}

void StoryScenes::battleFinished(BattleOutcome outcome) {
  core::logDebug("[port:battle] BTL_battleMain (battle_main.c:1832) returned {}",
                 static_cast<int>(outcome));
  context_->battleOutcome = outcome;
}

std::optional<StoryScreenRequest> StoryScenes::takeScreenRequest() {
  StoryScenesContext& scenes = *context_;
  if (scenes.screenRequests.empty()) {
    return std::nullopt;
  }
  StoryScreenRequest request = scenes.screenRequests.front();
  scenes.screenRequests.pop_front();
  return request;
}

void StoryScenes::screenClosed() {
  context_->screenOpen = false;
}

std::optional<PartnerSceneRequest> StoryScenes::takePartnerScene() {
  return std::exchange(context_->partnerScene, std::nullopt);
}

void StoryScenes::partnerSceneFinished() {
  context_->sync.finish();
}

game::story::ScreenHistory& StoryScenes::screens() {
  return context_->screens;
}

const game::story::BattleCounters& StoryScenes::battleCounters() const {
  static const game::story::BattleCounters None{};
  return context_->battles ? context_->battles->counters() : None;
}

game::story::SceneSync& StoryScenes::sceneSync() {
  return context_->sync;
}

const StoryScenesRules& StoryScenes::rules() const {
  return context_->rules;
}

} // namespace dwe::session
