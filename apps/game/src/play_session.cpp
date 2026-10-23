#include "play_session.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/field_facts.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/ui/minimap.hpp>

#include <cmath>
#include <string_view>
#include <utility>

#include "battle_trigger.hpp"
#include "common.hpp"
#include "messages.hpp"
#include "partner_events.hpp"
#include "partner_tab.hpp"

namespace dwe::app {
namespace {

// Frames left out of the steady frame times after a map change: the new map's textures upload in
// the next frames.
constexpr int MapChangeSettleFrames = 5;

// Where the original's need bubble hangs over the partner, in world units: it starts 20 below the
// top of its species' height (bubble.c: addConditionBubble: baseHeight = -height + 20) and rises 65
// over its first 10 frames, where it settles (bubble.c: tickConditionBubble: activeHeight =
// baseHeight - 65). Its bob of 12 up and down around that point is the skin's animation and is not
// published.
constexpr float BubbleDrop = 20.0F;
constexpr float BubbleRise = 65.0F;

// Where the partner's need bubble settles in the window; nothing off the map's camera.
std::optional<screens::WindowFraction> partnerBubbleOf(const FieldScene& scene,
                                                       const assets::DigimonTable* species,
                                                       std::string_view partnerSpecies) {
  const assets::DigimonRow* row = species != nullptr ? species->find(partnerSpecies) : nullptr;
  const float height = row != nullptr ? static_cast<float>(row->height) - BubbleDrop : 0.0F;
  glm::vec3 bubble = scene.field().partner().position;
  bubble.y += height + BubbleRise;
  const std::optional<glm::vec2> fraction = scene.worldToWindowFraction(bubble);
  if (!fraction) {
    return std::nullopt;
  }
  return screens::WindowFraction{.x = fraction->x, .y = fraction->y};
}

ui::MinimapMarker markerOf(const game::field::CharacterState& character) {
  return {.x = character.position.x,
          .z = character.position.z,
          .facingX = std::sin(character.yaw),
          .facingZ = std::cos(character.yaw)};
}

// Starts the battle a battle("npcs") command asks for against the NPCs of the field's map.
void startBattle(BattleScreen& screen, const assets::DigimonTable* species, const FieldScene& scene,
                 std::string_view npcs) {
  auto started = [&]() -> core::Result<void> {
    if (species == nullptr) {
      return core::fail(std::string{messages::NoSpeciesTable});
    }
    auto chosen = npcsOf(npcs);
    if (!chosen) {
      return std::unexpected{chosen.error()};
    }
    auto request = mapBattleRequest(scene.field(), scene.map(), *species, *chosen);
    if (!request) {
      return std::unexpected{request.error()};
    }
    return screen.start(std::move(*request));
  }();
  if (!started) {
    core::logWarning("{}",
                     core::formatMessage(messages::BattleStartFailed, started.error().message));
  }
}

// Logs which ruleset the game plays with, and where each of its sections comes from.
void logRuleset(const ruleset::Ruleset& rules) {
  std::string sections;
  for (const auto& [name, section] : rules.sections()) {
    if (!sections.empty()) {
      sections += messages::ListSeparator;
    }
    sections += core::formatMessage(messages::RulesetSection, name, section.owner);
  }
  const ruleset::RulesetInfo& info = rules.info();
  core::logInfo("{}", core::formatMessage(messages::RulesetActive, info.id, info.version, info.name,
                                          sections));
}

// The species table of the battle screen and the partner's menu; nothing, with a warning, when the
// packs have none.
std::optional<assets::DigimonTable> speciesOf(const assets::VirtualFileSystem& files,
                                              session::GameSession& session) {
  auto species = speciesTable(files, session.rules().current());
  if (!species) {
    core::logWarning("{}",
                     core::formatMessage(messages::BattleStartFailed, species.error().message));
    return std::nullopt;
  }
  return std::move(*species);
}

} // namespace

void SessionCommands::drop(Autotest* autotest) {
  const auto dropped = [](std::string_view message) {
    core::logWarning("{}", core::formatMessage(message, messages::NoGame));
  };
  if (std::exchange(gift, std::nullopt)) {
    dropped(messages::GiveItemFailed);
  }
  if (std::exchange(use, std::nullopt)) {
    dropped(messages::UseItemFailed);
  }
  if (std::exchange(battle, std::nullopt)) {
    dropped(messages::BattleStartFailed);
  }
  if (std::exchange(evolution, std::nullopt)) {
    dropped(messages::EvolveFailed);
  }
  if (care.pending()) {
    dropped(messages::CareCommandFailed);
    care = {};
  }
  if (save.pending()) {
    dropped(messages::SaveCommandFailed);
    save = {};
    if (autotest != nullptr) {
      autotest->fail(core::formatMessage(messages::SaveCommandFailed, messages::NoGame));
    }
  }
}

void SessionCommands::bind(script::ScriptVm& scripts) {
  scripts.bindCommand(GiveItemCommand, [this](std::string_view item) { gift = std::string{item}; });
  scripts.bindCommand(UseItemCommand, [this](std::string_view item) { use = std::string{item}; });
  scripts.bindCommand(BattleCommand, [this](std::string_view npcs) { battle = std::string{npcs}; });
  scripts.bindCommand(EvolveCommand,
                      [this](std::string_view form) { evolution = std::string{form}; });
  care.bind(scripts);
  save.bind(scripts);
}

core::Result<std::unique_ptr<PlaySession>> PlaySession::create(const PlayContext& context,
                                                               const PlayStart& start) {
  const Options& options = *context.options;
  auto game = startGame(*context.files, options, context.keepsSaves, start, context.sound);
  if (!game) {
    return std::unexpected{game.error()};
  }
  session::GameSession& session = *game->session;
  logRuleset(session.rules().current());
  // A loaded game starts where it was saved, unless --start says otherwise.
  const std::optional<session::FieldPlace> savedPlace =
      options.startMap ? std::nullopt : session.savedPlace();
  auto scene = FieldScene::create(
      *context.renderer, *context.files,
      {.startMap = savedPlace ? std::optional{savedPlace->map} : options.startMap,
       .startExit = options.startExit,
       .startAt = savedPlace ? std::optional{savedPlace->spawn} : std::nullopt,
       .clock = &session.clock(),
       .time = options.time,
       .facts =
           session::fieldGuardFacts(*game->session, game->systems.inventory, *game->systems.story),
       .writeTrigger = session::fieldTriggerWriter(*game->systems.story)});
  if (!scene) {
    return std::unexpected{scene.error()};
  }
  return std::make_unique<PlaySession>(Key{}, context, std::move(*game), std::move(*scene));
}

PlaySession::PlaySession(Key /*key*/, const PlayContext& context, GameStart game,
                         std::unique_ptr<FieldScene> scene)
    : context_(context), game_(std::move(game)), scene_(std::move(scene)),
      species_(speciesOf(*context.files, *game_.session)),
      freeExploration_(
          {.ui = context.ui,
           .scene = scene_.get(),
           .settings = context.userSettings,
           .settingsFile = context.settingsFile},
          startFreeExploration(context.options->explore, context.userSettings->freeExploration)),
      battleScreen_({.session = game_.session.get(),
                     .battle = game_.systems.battle,
                     .inventory = game_.systems.inventory,
                     .species = species_ ? &*species_ : nullptr,
                     .ui = context.ui}),
      partnerMenu_({.session = game_.session.get(),
                    .inventory = game_.systems.inventory,
                    .species = species_ ? &*species_ : nullptr,
                    .ui = context.ui}),
      fieldHud_(*context.ui, *game_.session) {
  session::GameSession& session = *game_.session;
  session.attachField(scene_->field());
  // A loaded game's partner may be another species than the game pack's partner model.
  if (auto shown = scene_->showPartnerSpecies(session.partnerView().species); !shown) {
    core::logWarning("{}",
                     core::formatMessage(messages::PartnerModelNotShown,
                                         session.partnerView().species, shown.error().message));
  }
  context_.ui->show("hud");
  // The tuning panel tunes the game's ruleset, sets the speed of its clock, pauses it and jumps it.
  // It is attached last and detached first, so it never points at a game half built or half gone.
  context_.debugUi->attachTuning(
      {.files = context_.files, .store = &session.rules(), .clock = &session.clock()});
  context_.debugUi->addTuningTab(partnerTab(session));
}

PlaySession::~PlaySession() {
  // The frames in flight may still draw the field's textures.
  context_.device->waitIdle();
  context_.debugUi->detachTuning();
  game_.session->detachField();
  for (const std::string_view document :
       {std::string_view{"hud"}, BattleDocument, PartnerMenuDocument, DestinationChoiceDocument}) {
    context_.ui->hide(document);
  }
}

void PlaySession::tick(const game::field::FieldInput& intent) {
  const ui::Ui& ui = *context_.ui;
  // The menus pause the field and the game time; a battle runs instead of the field.
  if (battleScreen_.active()) {
    if (!ui.isVisible(MenuDocument)) {
      battleScreen_.tick(TickSeconds);
    }
  } else if (!ui.isVisible(MenuDocument) && !ui.isVisible(MainMenuDocument) &&
             !ui.isVisible(PartnerMenuDocument)) {
    scene_->tick(intent, static_cast<float>(TickSeconds));
    game_.session->tick(TickSeconds);
    scene_->finishTick();
  }
}

bool PlaySession::handle(const ui::UiCommand& command) {
  if (battleScreen_.handle(command) || freeExploration_.handle(command)) {
    return true;
  }
  if (partnerMenu_.handle(command)) {
    guardFactsChanged_ = true;
    return true;
  }
  return false;
}

void PlaySession::update(SessionCommands& commands, Autotest* autotest, FrameStats& stats,
                         double frameSeconds, float alpha) {
  session::GameSession& session = *game_.session;
  const assets::DigimonTable* species = species_ ? &*species_ : nullptr;
  // What the script warps' guards read (bag, money, stats, conditions) may change only on these
  // events; the field decides its cells again once after them (docs/reference/campo.md), not
  // every frame.
  bool guardFactsChanged = std::exchange(guardFactsChanged_, false);
  if (commands.gift || commands.use) {
    guardFactsChanged = true;
  }
  if (const auto gift = std::exchange(commands.gift, std::nullopt)) {
    if (auto given = partnerMenu_.giveItem(*gift); !given) {
      core::logWarning("{}", core::formatMessage(messages::GiveItemFailed, given.error().message));
    }
  }
  if (const auto use = std::exchange(commands.use, std::nullopt)) {
    if (auto used = partnerMenu_.useItem(*use); !used) {
      core::logWarning("{}", core::formatMessage(messages::UseItemFailed, used.error().message));
    }
  }
  if (const auto npcs = std::exchange(commands.battle, std::nullopt)) {
    startBattle(battleScreen_, species, *scene_, *npcs);
  }
  guardFactsChanged = commands.care.apply(session) || guardFactsChanged;
  // Before a queued evolve, so a script that saves and then evolves saves the form it had when it
  // asked.
  if (auto saved = commands.save.apply(session); !saved) {
    core::logWarning("{}", saved.error().message);
    // A route that saves checks what it saved: a save that did not happen is the route's failure.
    if (autotest != nullptr) {
      autotest->fail(saved.error().message);
    }
  }
  freeExploration_.update();
  if (const auto evolution = std::exchange(commands.evolution, std::nullopt)) {
    guardFactsChanged = true;
    const std::string& form = *evolution;
    if (auto evolved = game_.systems.evolution->evolveByScript(session, form); !evolved) {
      core::logWarning("{}", core::formatMessage(messages::EvolveFailed, evolved.error().message));
    } else if (!*evolved) {
      core::logWarning("{}", core::formatMessage(messages::EvolveRefused, form));
    }
  }
  ui::Ui& ui = *context_.ui;
  if (scene_->mapSerial() != mapSerial_) {
    mapSerial_ = scene_->mapSerial();
    ui.setMinimap(ui::minimapGridOf(scene_->map()), scene_->mapNameKey());
    if (const auto& change = scene_->lastMapChange(); change && autotest != nullptr) {
      autotest->mapChanged(*change);
    }
    stats.skipNext(MapChangeSettleFrames);
  }
  const game::field::Field& field = scene_->field();
  ui.setMinimapMarkers(markerOf(field.player()), markerOf(field.partner()));

  partnerMenu_.update();
  const auto partnerEvents = session.takePartnerEvents();
  showPartnerEvents(partnerEvents, session, *scene_);
  guardFactsChanged = guardFactsChanged || !partnerEvents.empty() ||
                      (battleRanLastFrame_ && !battleScreen_.active());
  battleRanLastFrame_ = battleScreen_.active();
  if (guardFactsChanged) {
    scene_->refreshScriptWarps();
  }

  fieldHud_.update(!paused(), partnerBubbleOf(*scene_, species, session.partnerView().species));
  scene_->update(frameSeconds, alpha, paused());
}

screens::ScreenSettings PlaySession::partnerStatus() const {
  return partnerMenu_.statusSettings();
}

bool PlaySession::paused() const {
  const ui::Ui& ui = *context_.ui;
  return ui.isVisible(MenuDocument) || ui.isVisible(MainMenuDocument) ||
         ui.isVisible(PartnerMenuDocument) || battleScreen_.active();
}

} // namespace dwe::app
