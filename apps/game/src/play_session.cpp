#include "play_session.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/field_facts.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/ui/minimap.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
#include <variant>

#include "battle_trigger.hpp"
#include "common.hpp"
#include "messages.hpp"
#include "partner_events.hpp"
#include "partner_tab.hpp"
#include "story_map_edits.hpp"
#include "title_screen.hpp"

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
  if (std::exchange(saveMachine, false)) {
    dropped(messages::SaveMachineFailed);
  }
  if (std::exchange(evolution, std::nullopt)) {
    dropped(messages::EvolveFailed);
  }
  if (std::exchange(gym, std::nullopt)) {
    dropped(messages::GymFailed);
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
  scripts.bindCommand(SaveMachineCommand, [this](std::string_view) { saveMachine = true; });
  scripts.bindCommand(GymCommand, [this](std::string_view course) { gym = std::string{course}; });
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
      fieldHud_(*context.ui, *game_.session),
      sleepScreen_(*game_.session, *context.ui, scene_->field().rules().transition),
      saveMachine_(*game_.session, *context.ui),
      evolutionScene_({.session = game_.session.get(),
                       .scene = scene_.get(),
                       .species = species_ ? &*species_ : nullptr,
                       .evolution = game_.systems.evolution,
                       .frameSeconds = scene_->field().rules().transition.frameSeconds}),
      deathScene_({.session = game_.session.get(),
                   .scene = scene_.get(),
                   .story = game_.systems.story,
                   .transition = scene_->field().rules().transition}),
      gymScreen_({.session = game_.session.get(),
                  .story = game_.systems.story,
                  .inventory = game_.systems.inventory,
                  .scene = scene_.get(),
                  .ui = context.ui}),
      shopScreen_({.session = game_.session.get(), .shop = game_.systems.shop, .ui = context.ui}) {
  session::GameSession& session = *game_.session;
  if (game_.systems.story != nullptr) {
    storyTextbox_.emplace(*context.ui, *game_.systems.story, session);
  }
  session.attachField(scene_->field());
  game_.systems.story->attachNpcs(&scene_->npcs());
  storyLink_ = std::make_unique<StoryFieldLink>(*game_.systems.story, *scene_);
  storyScenes_.attachShop(game_.systems.shop);
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
  storyLink_.reset();
  game_.systems.story->attachNpcs(nullptr);
  for (const std::string_view document :
       {std::string_view{"hud"}, BattleDocument, PartnerMenuDocument, DestinationChoiceDocument,
        SleepMenuDocument, DialogDocument, ShopMenuDocument}) {
    context_.ui->hide(document);
  }
}

void PlaySession::pressStory(const input::InputState& input) {
  if (storyTextbox_) {
    storyTextbox_->press(input);
  }
}

void PlaySession::tick(const game::field::FieldInput& intent) {
  const ui::Ui& ui = *context_.ui;
  // The menus pause the field and the game time; a sleep or a battle runs instead of the field.
  sleepScreen_.tick(TickSeconds);
  if (sleepScreen_.active() || shopScreen_.active()) {
    return;
  }
  // partner.c: partnerTickEvolving stops the game time and holds the player for the scene.
  evolutionScene_.tick(TickSeconds);
  if (evolutionScene_.active()) {
    return;
  }
  // murd.c and dooa.c: a lost life's or a death's scene holds the field once a battle is over.
  if (!battleScreen_.active()) {
    deathScene_.tick(TickSeconds);
    if (deathScene_.active()) {
      return;
    }
  }
  if (battleScreen_.active()) {
    if (!ui.isVisible(MenuDocument)) {
      battleScreen_.tick(TickSeconds);
    }
  } else if (!ui.isVisible(MenuDocument) && !ui.isVisible(MainMenuDocument) &&
             !ui.isVisible(PartnerMenuDocument)) {
    // tamer.c: checkMapInteraction talks and confirms on a cell only while no script runs
    // (IS_SCRIPT_PAUSED 1), so the press that closes a textbox never starts a talk again.
    game::field::FieldInput fieldIntent = intent;
    if (fieldIntent.confirm && game_.systems.story != nullptr &&
        game_.systems.story->scriptRunning()) {
      core::logDebug("[port:dialogue] checkMapInteraction (tamer.c:671): confirm ignored while a "
                     "script runs");
      fieldIntent.confirm = false;
    }
    // A gym course takes the player's control; its buttons are the course's.
    if (gymScreen_.active()) {
      gymScreen_.tick(TickSeconds, intent);
      fieldIntent = {};
    }
    scene_->tick(fieldIntent, static_cast<float>(TickSeconds));
    game_.systems.story->runNpcContacts(scene_->field().npcContacts());
    storyLink_->afterFieldTick();
    game_.session->tick(TickSeconds);
    applyStoryMapEdits(*scene_, game_.systems.storyOrders);
    scene_->finishTick();
    storyLink_->afterMapChange();
  }
}

bool PlaySession::handle(const ui::UiCommand& command) {
  if (shopScreen_.handle(command) || sleepScreen_.handle(command) || saveMachine_.handle(command) ||
      battleScreen_.handle(command) || freeExploration_.handle(command)) {
    return true;
  }
  if (command.name == "to_title") {
    // Not over a sleep or a battle, whose state a later save would not see whole.
    if (!sleepScreen_.active() && !battleScreen_.active() && !saveMachine_.active()) {
      context_.ui->hide(MenuDocument);
      screens::publish(
          *context_.ui,
          dialogSettings({.text = "save.to_title_confirm", .file = {}, .question = true}),
          "dialog.revision");
      context_.ui->show(DialogDocument, true);
      toTitleAsked_ = true;
    }
    return true;
  }
  if (command.name == "dialog_answer" && toTitleAsked_) {
    const bool* yes = std::get_if<bool>(&command.value);
    context_.ui->hide(DialogDocument);
    toTitleAsked_ = false;
    toTitle_ = yes != nullptr && *yes;
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
  if (game_.systems.scenes != nullptr) {
    storyScenes_.update(*game_.systems.scenes, battleScreen_, species, *scene_);
  }
  guardFactsChanged = commands.care.apply(session) || guardFactsChanged;
  if (std::exchange(commands.saveMachine, false)) {
    saveMachine_.open();
  }
  if (const auto course = std::exchange(commands.gym, std::nullopt)) {
    if (auto started = gymScreen_.start(*course); !started) {
      core::logWarning("{}", core::formatMessage(messages::GymFailed, started.error().message));
    }
  }
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
  shopScreen_.update();
  // tamer.c: the menu opens with no script, no fade and no text box; the field has no scripts or
  // text boxes yet.
  sleepScreen_.update(!battleScreen_.active() && !gymScreen_.active() &&
                      !evolutionScene_.active() && !deathScene_.active() &&
                      !field.mapChangePending() && field.fadeLevel() == 0.0F);
  gymScreen_.update();
  const auto partnerEvents = session.takePartnerEvents();
  showPartnerEvents(deathScene_.take(evolutionScene_.take(partnerEvents)), session, *scene_);
  guardFactsChanged = guardFactsChanged || !partnerEvents.empty() ||
                      (battleRanLastFrame_ && !battleScreen_.active());
  battleRanLastFrame_ = battleScreen_.active();
  if (guardFactsChanged) {
    scene_->refreshScriptWarps();
  }

  fieldHud_.update(!paused(), partnerBubbleOf(*scene_, species, session.partnerView().species));
  if (storyTextbox_) {
    storyTextbox_->update(*scene_, species);
  }
  scene_->update(frameSeconds, alpha, paused());
}

screens::ScreenSettings PlaySession::partnerStatus() const {
  return partnerMenu_.statusSettings();
}

bool PlaySession::paused() const {
  const ui::Ui& ui = *context_.ui;
  return ui.isVisible(MenuDocument) || ui.isVisible(MainMenuDocument) ||
         ui.isVisible(PartnerMenuDocument) || battleScreen_.active() || sleepScreen_.active() ||
         saveMachine_.active() || toTitleAsked_ || evolutionScene_.active() || deathScene_.active() ||
         shopScreen_.active();
}

float PlaySession::fadeLevel() const {
  return std::max({scene_->field().fadeLevel(), sleepScreen_.fadeLevel(), deathScene_.fadeLevel()});
}

} // namespace dwe::app
