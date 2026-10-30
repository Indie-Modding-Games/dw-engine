#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/game/story/npc_orders.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_lua.hpp>
#include <dwe/session/story_scenes.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "story_scenes_context.hpp"

namespace dwe::session {

using game::partner::EventKind;
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

  [[nodiscard]] std::optional<std::int64_t> talkedTo() const override {
    if (const auto talked = scenes_->desc.story->talkedTo()) {
      return *talked;
    }
    return std::nullopt;
  }

  void startBattle(const ScriptedBattle& battle) override {
    core::logDebug("[port:battle] startBattle (tamer.c:832): {} enemies, fixed={}",
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

  void holdAfterFixedFight() override {
    game::story::NpcOrders* orders = scenes_->desc.story->npcOrders();
    core::logDebug("[port:story] start_battle (script_interp.c:1944): fixed fight won, every NPC "
                   "held: {}",
                   orders != nullptr);
    if (orders != nullptr) {
      orders->holdAll();
    }
  }

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
    if (scenes_->rules.death.has_value()) {
      startDeathScene(scene);
    }
    scenes_->desc.story->setPartnerSync(false);
    scenes_->partnerScene = PartnerSceneRequest{.scene = scene};
  }

private:
  // The death section ran its scene: the lost life or the death is the app's to play; the
  // rebirth's scene gives the dead partner its new form first.
  void startDeathScene(PartnerScene scene) const {
    switch (scene) {
    case PartnerScene::Dying:
    case PartnerScene::SicknessLostLife:
    case PartnerScene::BattleLostLife:
      scenes_->deathPending = false;
      scenes_->dying = scenes_->session != nullptr && !scenes_->session->partner().alive();
      break;
    case PartnerScene::LostAllLives:
      rebirth();
      break;
    default:
      break;
    }
  }

  void rebirth() const {
    GameSession* session = scenes_->session;
    if (session == nullptr || session->partner().alive()) {
      core::logWarning("[port:death] partnerTickDying2: the partner is not dead, no rebirth");
      return;
    }
    const std::optional<std::size_t> egg = std::exchange(scenes_->egg, std::nullopt);
    core::logDebug("[port:death] DOOA_getSequenceState (dooa.c:1745): egg {}",
                   egg.has_value() ? static_cast<std::int64_t>(*egg) : -1);
    session->rebirthPartner(egg);
  }

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

void StoryScenes::setNewGameChain(std::optional<NewGameChain> chain) {
  context_->chainOverride = chain;
}

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
  if (scenes.chainOverride) {
    scenes.rules.newGameChain = *scenes.chainOverride;
  }
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
  session.setRebirthByStory(scenes.rules.death.has_value());
  scenes.battlePort = std::make_unique<SceneBattlePort>(scenes);
  scenes.partnerPort = std::make_unique<ScenePartnerPort>(scenes);
  scenes.battles = std::make_unique<game::story::StoryBattles>(scenes.state(), *scenes.battlePort,
                                                               scenes.screens, scenes.rules.battle);
  scenes.partner = std::make_unique<game::story::PartnerRoutines>(
      scenes.state(), *scenes.partnerPort, scenes.sync, scenes.rules.partner);
  scenes.propOrders = makePropOrders(scenes);
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
  if (auto props = story->host()->publishPropApi(*scenes.propOrders); !props) {
    core::logError("{}", props.error().message);
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
  scenes.deathPending = false;
  scenes.dying = false;
  scenes.egg.reset();
  // main.c: main runs the first map's map-head section once the new game is set up
  // (runMapHeadScript(0xCC)); the field does when it enters that map (newGameStart).
  core::logDebug("[port:story] initializeMap (map.c:3936): screen {} exit {}, chain={}",
                 scenes.rules.newGameMap, scenes.rules.newGameExit,
                 scenes.rules.newGameChain == NewGameChain::Original ? "original" : "seeded");
  return {};
}

void StoryScenes::tick(GameSession& session, const ruleset::ClockAdvance& /*advance*/) {
  bind(session);
  StoryScenesContext& scenes = *context_;
  if (!scenes.published) {
    return;
  }
  watchRecruits(session);
  askForDeath(session);
}

void StoryScenes::routePartnerEvent(GameSession& /*session*/,
                                    const game::partner::PartnerEvent& event) {
  if (playsDeath() && (event.kind == EventKind::LostLife || event.kind == EventKind::Died)) {
    context_->deathPending = true;
  }
}

void StoryScenes::askForDeath(const GameSession& session) {
  // partner_impl.c: tickDeathCondition and the sickness's lost life call the death section once the
  // player is free and no script runs; a story battle lost goes there itself, and its scene clears
  // the request.
  StoryScenesContext& scenes = *context_;
  if (!scenes.rules.death.has_value()) {
    return;
  }
  if (session.partner().alive()) {
    scenes.dying = false;
  } else if (!scenes.dying) {
    // A dead partner whose death section has not run, as a game saved during the death loads:
    // tickDeathCondition asks again while the partner is dead.
    scenes.deathPending = true;
  }
  if (!scenes.deathPending || scenes.runner().busy() || !session.playerFree()) {
    return;
  }
  const StoryDeathRules& death = *scenes.rules.death;
  const game::story::SectionRef section{.script = game::story::StoryRunner::MapHeadScript,
                                        .section = death.section};
  if (scenes.desc.story->request(section, game::story::StartTiming::NextFrame) !=
      game::story::RequestOutcome::Accepted) {
    return;
  }
  scenes.deathPending = false;
  scenes.dying = !session.partner().alive();
  if (auto cleared = scenes.state().setVariable(death.clearedVariable, 0); !cleared) {
    core::logWarning("{}", cleared.error().message);
  }
  core::logDebug("[port:death] tickDeathCondition (partner_impl.c:1918): section {} of the "
                 "map-head script, variable {} cleared",
                 death.section, death.clearedVariable);
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

std::optional<StoryPropRequest> StoryScenes::takePropRequest() {
  StoryScenesContext& scenes = *context_;
  if (scenes.propRequests.empty()) {
    return std::nullopt;
  }
  StoryPropRequest request = scenes.propRequests.front();
  scenes.propRequests.pop_front();
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
  if (context_->desc.story != nullptr) {
    context_->desc.story->setPartnerSync(true);
  }
}

bool StoryScenes::playsDeath() const {
  return context_->published && context_->rules.death.has_value();
}

void StoryScenes::chooseEgg(std::size_t egg) {
  core::logDebug("[port:death] DOO2_tickEggInput (doo2.c:739): egg {} taken", egg);
  context_->egg = egg;
}

game::story::ScreenHistory& StoryScenes::screens() {
  return context_->screens;
}

const game::story::BattleCounters& StoryScenes::battleCounters() const {
  static const game::story::BattleCounters none{};
  return context_->battles ? context_->battles->counters() : none;
}

game::story::SceneSync& StoryScenes::sceneSync() {
  return context_->sync;
}

const StoryScenesRules& StoryScenes::rules() const {
  return context_->rules;
}

std::optional<NewGameStart> StoryScenes::newGameStart() const {
  const StoryScenesContext& scenes = *context_;
  if (!scenes.enabled || scenes.rules.newGameChain != NewGameChain::Original) {
    return std::nullopt;
  }
  return NewGameStart{.map = scenes.rules.newGameMap, .exit = scenes.rules.newGameExit};
}

} // namespace dwe::session
