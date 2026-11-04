#include "play_session.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/species_extra.hpp>
#include <dwe/scene/components.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/field_facts.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/map_visits.hpp>
#include <dwe/session/session_music.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_game_orders.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/session/weather_seed.hpp>
#include <dwe/ui/minimap.hpp>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "battle_trigger.hpp"
#include "bulk_amount.hpp"
#include "common.hpp"
#include "hd_scenery_setting.hpp"
#include "messages.hpp"
#include "partner_events.hpp"
#include "partner_set_command.hpp"
#include "partner_tab.hpp"
#include "place_command.hpp"
#include "player_chart.hpp"
#include "story_map_edits.hpp"
#include "story_set_command.hpp"
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
// published. Across the ground it hangs where the partner's condition effects do
// (renderConditionBubble: translateConditionFXToEntity's x and z), over its feet without that
// point.
constexpr float BubbleDrop = 20.0F;
constexpr float BubbleRise = 65.0F;

// Where the partner's need bubble settles in the window; nothing off the map's camera.
std::optional<screens::WindowFraction> partnerBubbleOf(const FieldScene& scene,
                                                       const assets::DigimonTable* species,
                                                       std::string_view partnerSpecies) {
  const assets::DigimonRow* row = species != nullptr ? species->find(partnerSpecies) : nullptr;
  const float height = row != nullptr ? static_cast<float>(row->height) - BubbleDrop : 0.0F;
  glm::vec3 bubble = scene.field().partner().position;
  if (const std::optional<glm::vec3> condition = scene.partnerConditionPoint()) {
    bubble.x = condition->x;
    bubble.z = condition->z;
  }
  bubble.y += height + BubbleRise;
  const std::optional<glm::vec2> fraction = scene.worldToWindowFraction(bubble);
  if (!fraction) {
    return std::nullopt;
  }
  return screens::WindowFraction{.x = fraction->x, .y = fraction->y};
}

// Where an enemy's anger mark hangs in the window (btl.c: renderFleeBubble): `lift` over the top of
// its species' height, over the map's NPC it is (the one of its species that the battle placed
// where it stood, in world axes). Nothing off the map's camera or without that NPC.
screens::AngerAnchor angerAnchorOf(const FieldScene& fieldScene,
                                   const assets::DigimonTable* species,
                                   const game::battle::Combatant& enemy, float lift) {
  // The battle starts each enemy where its NPC stands, in world axes (mapBattleRequest): its NPC
  // is the one of its species nearest that place.
  const glm::vec2 start{static_cast<float>(enemy.position.x), static_cast<float>(enemy.position.z)};
  std::optional<glm::vec3> nearest;
  for (const assets::MapNpc& npc : fieldScene.map().npcs) {
    if (npc.digimon != enemy.species) {
      continue;
    }
    const glm::vec3 at = fieldScene.field()
                             .npcs()
                             .positionOf(npc.scriptId)
                             .value_or(assets::toWorldPosition(npc.position));
    if (!nearest || glm::distance(glm::vec2{at.x, at.z}, start) <
                        glm::distance(glm::vec2{nearest->x, nearest->z}, start)) {
      nearest = at;
    }
  }
  if (!nearest) {
    return std::nullopt;
  }
  glm::vec3 position = *nearest;
  const assets::DigimonRow* row = species != nullptr ? species->find(enemy.species) : nullptr;
  position.y += (row != nullptr ? static_cast<float>(row->height) : 0.0F) + lift;
  const std::optional<glm::vec2> fraction = fieldScene.worldToWindowFraction(position);
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
    const bool story = npcs.starts_with(StoryBattlePrefix);
    if (story) {
      npcs.remove_prefix(StoryBattlePrefix.size());
    }
    auto chosen = npcsOf(npcs);
    if (!chosen) {
      return std::unexpected{chosen.error()};
    }
    auto request = mapBattleRequest(scene.field(), scene.map(), *species, *chosen);
    if (!request) {
      return std::unexpected{request.error()};
    }
    // Without NPC numbers this is the battle touching the first NPC that can fight starts: the
    // others on screen may join it, as in the story's battles with no predefined enemies.
    if (chosen->empty() && !story && !request->enemyNpcs.empty()) {
      std::vector<std::int32_t> enemies{request->enemyNpcs.front()};
      std::ranges::copy(screen.joiningNpcs(enemies.front(), false), std::back_inserter(enemies));
      std::vector<std::size_t> indices;
      const auto& records = scene.map().npcs;
      for (const std::int32_t npc : enemies) {
        const auto found = std::ranges::find(records, npc, &assets::MapNpc::scriptId);
        indices.push_back(static_cast<std::size_t>(found - records.begin()));
      }
      request = mapBattleRequest(scene.field(), scene.map(), *species, indices);
      if (!request) {
        return std::unexpected{request.error()};
      }
    }
    request->request.scripted = story;
    return screen.start(std::move(request->request), std::move(request->enemyNpcs));
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

// The ruleset's species patches (the hidden content): what the field draws differently for a
// species. None, with a warning, when they do not read.
ruleset::SpeciesExtra speciesExtraOf(session::GameSession& session) {
  auto extra = ruleset::readSpeciesExtra(session.rules().current());
  if (!extra) {
    core::logWarning("species patches: {}", extra.error().message);
    return {};
  }
  return std::move(*extra);
}

// learn_move("<move> [place]") (partner_events.hpp): the move learned and, with a place, equipped
// there from the partner's species list.
core::Result<void> learnMove(session::BattleAdapter* battle, const session::GameSession& session,
                             const assets::DigimonTable* species, std::string_view lesson) {
  const std::size_t gap = lesson.find(' ');
  const std::string_view move = lesson.substr(0, gap);
  std::optional<std::size_t> place;
  if (gap != std::string_view::npos) {
    const std::string_view text = lesson.substr(gap + 1);
    std::size_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
      return core::fail(core::formatMessage(messages::LearnMoveUsage, lesson));
    }
    place = value;
  }
  if (battle == nullptr || move.empty()) {
    return core::fail(core::formatMessage(messages::LearnMoveUsage, lesson));
  }
  battle->learnMove(move);
  if (!place) {
    return {};
  }
  const std::string& partner = session.partner().state().species;
  const assets::DigimonRow* row = species != nullptr ? species->find(partner) : nullptr;
  if (row == nullptr) {
    return core::fail(core::formatMessage(messages::LearnMoveNotInSpecies, partner, move));
  }
  const auto found = std::ranges::find(row->moves, std::optional<std::string>{move});
  if (found == row->moves.end()) {
    return core::fail(core::formatMessage(messages::LearnMoveNotInSpecies, partner, move));
  }
  return battle->equip(session, *place,
                       static_cast<session::MoveSlot>(std::distance(row->moves.begin(), found)));
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
  if (std::exchange(groundDrop, std::nullopt)) {
    dropped(messages::DropItemFailed);
  }
  if (std::exchange(battle, std::nullopt)) {
    dropped(messages::BattleStartFailed);
  }
  if (std::exchange(battleWin, false)) {
    dropped(messages::BattleWinFailed);
  }
  if (std::exchange(battleLose, false)) {
    dropped(messages::BattleLoseFailed);
  }
  if (std::exchange(battleTimeUp, std::nullopt)) {
    dropped(messages::BattleTimeUpFailed);
  }
  if (!std::exchange(placements, {}).empty()) {
    dropped(messages::PlaceFailed);
  }
  if (!std::exchange(storyWrites, {}).empty()) {
    dropped(messages::StorySetFailed);
  }
  if (!std::exchange(partnerWrites, {}).empty()) {
    dropped(messages::PartnerSetFailed);
  }
  if (!std::exchange(lessons, {}).empty()) {
    dropped(messages::LearnMoveFailed);
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
  scripts.bindCommand(DropItemCommand,
                      [this](std::string_view item) { groundDrop = std::string{item}; });
  scripts.bindCommand(BattleCommand, [this](std::string_view npcs) { battle = std::string{npcs}; });
  scripts.bindCommand(BattleWinCommand, [this](std::string_view) { battleWin = true; });
  scripts.bindCommand(BattleLoseCommand, [this](std::string_view) { battleLose = true; });
  scripts.bindCommand(BattleTimeUpCommand,
                      [this](std::string_view side) { battleTimeUp = std::string{side}; });
  scripts.bindCommand(PlaceCommand,
                      [this](std::string_view where) { placements.emplace_back(where); });
  scripts.bindCommand(StorySetCommand,
                      [this](std::string_view what) { storyWrites.emplace_back(what); });
  scripts.bindCommand(PartnerSetCommand,
                      [this](std::string_view what) { partnerWrites.emplace_back(what); });
  scripts.bindCommand(LearnMoveCommand,
                      [this](std::string_view move) { lessons.emplace_back(move); });
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
  if (context.ui != nullptr) {
    publishRulesetChart(*context.ui, *context.files, session.rules().current());
  }
  // A loaded game starts where it was saved, unless --start says otherwise.
  const std::optional<session::FieldPlace> savedPlace =
      options.startMap ? std::nullopt : session.savedPlace();
  // A new game on the original chain starts where the story's opening runs (story_scenes).
  const std::optional<session::NewGameStart> newGameStart =
      !start.load && !options.startMap && game->systems.scenes != nullptr
          ? game->systems.scenes->newGameStart()
          : std::nullopt;
  auto scene = FieldScene::create(
      *context.renderer, *context.files,
      {.startMap = savedPlace ? std::optional{savedPlace->map} : options.startMap,
       .startExit = options.startExit,
       .startAt = savedPlace ? std::optional{savedPlace->spawn} : std::nullopt,
       .startPartnerAt = savedPlace ? savedPlace->partner : std::nullopt,
       .startOriginalMap =
           newGameStart ? std::optional{newGameStart->map} : std::optional<std::int32_t>{},
       .startOriginalExit = newGameStart ? newGameStart->exit : 0,
       .clock = &session.clock(),
       .time = options.time,
       .facts =
           session::fieldGuardFacts(*game->session, game->systems.inventory, *game->systems.story),
       .writeTrigger = session::fieldTriggerWriter(*game->systems.story)});
  if (!scene) {
    return std::unexpected{scene.error()};
  }
  const bool cleared = start.load.has_value() && session.gameCleared();
  auto play = std::make_unique<PlaySession>(Key{}, context, std::move(*game), std::move(*scene));
  if (cleared) {
    // main.c: main, MAIN_STATE 2: a game saved after the ending plays ED2 before its field.
    core::logDebug("[port:save] main (main.c:3474): a finished game, its movie first");
    play->requestMovie(MovieMoment::ClearedLoad);
  }
  return play;
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
      battleGround_(*scene_), battleScreen_({.session = game_.session.get(),
                                             .battle = game_.systems.battle,
                                             .inventory = game_.systems.inventory,
                                             .fieldItems = game_.systems.fieldItems,
                                             .ground = &battleGround_,
                                             .scene = scene_.get(),
                                             .species = species_ ? &*species_ : nullptr,
                                             .files = context.files,
                                             .ui = context.ui}),
      feedScene_(*game_.session, *game_.systems.inventory, *game_.systems.fieldItems, *scene_),
      fieldToilet_(*game_.session, *game_.systems.fieldItems, *scene_),
      fieldPoop_(*game_.session, *game_.systems.poop, *scene_), storyProps_(*scene_),
      bossArrival_(*scene_), partnerMenu_({.session = game_.session.get(),
                                           .inventory = game_.systems.inventory,
                                           .species = species_ ? &*species_ : nullptr,
                                           .ui = context.ui,
                                           .battle = game_.systems.battle,
                                           .story = game_.systems.story,
                                           .cards = game_.systems.cards,
                                           .city = game_.systems.city,
                                           .feed = &feedScene_}),
      chartRequirements_(*context.ui, game_.systems.evolution),
      fieldHud_(*context.ui, *game_.session),
      shopJobBar_(game_.systems.scenes != nullptr ? game_.systems.scenes->rules().shopJob
                                                  : std::nullopt),
      sleepScreen_(*game_.session, *context.ui, scene_->field().rules().transition),
      saveMachine_({.session = game_.session.get(),
                    .ui = context.ui,
                    .battle = game_.systems.battle,
                    .registry = game_.systems.registry,
                    .species = species_ ? &*species_ : nullptr,
                    .city = game_.systems.city,
                    .maps = scene_->mapTable(),
                    .scenes = game_.systems.scenes}),
      evolutionScene_({.session = game_.session.get(),
                       .scene = scene_.get(),
                       .species = species_ ? &*species_ : nullptr,
                       .evolution = game_.systems.evolution,
                       .frameSeconds = scene_->field().rules().transition.frameSeconds,
                       .fishing = game_.systems.fishing}),
      deathScene_({.session = game_.session.get(),
                   .scene = scene_.get(),
                   .story = game_.systems.story,
                   .scenes = game_.systems.scenes,
                   .transition = scene_->field().rules().transition,
                   .ui = context.ui,
                   .species = species_ ? &*species_ : nullptr,
                   .fishing = game_.systems.fishing}),
      endingScene_({.session = game_.session.get(),
                    .scene = scene_.get(),
                    .species = species_ ? &*species_ : nullptr,
                    .frameSeconds = scene_->field().rules().transition.frameSeconds}),
      eggBoxScreen_(*game_.session, *context.ui),
      gymScreen_({.session = game_.session.get(),
                  .story = game_.systems.story,
                  .inventory = game_.systems.inventory,
                  .battle = game_.systems.battle,
                  .species = species_ ? &*species_ : nullptr,
                  .scene = scene_.get(),
                  .ui = context.ui}),
      shopScreen_({.session = game_.session.get(), .shop = game_.systems.shop, .ui = context.ui}),
      itemKeeperScreen_({.keeper = game_.systems.itemKeeper, .ui = context.ui}),
      discardScreen_({.menu = game_.systems.discardMenu,
                      .inventory = game_.systems.inventory,
                      .ui = context.ui}),
      jukeboxScreen_({.jukebox = game_.systems.jukebox,
                      .session = game_.session.get(),
                      .species = species_ ? &*species_ : nullptr,
                      .ui = context.ui}),
      scheduleScreen_({.tournament = game_.systems.tournament,
                       .session = game_.session.get(),
                       .story = game_.systems.story,
                       .species = species_ ? &*species_ : nullptr,
                       .ui = context.ui}),
      bracketScreen_({.tournament = game_.systems.tournament,
                      .session = game_.session.get(),
                      .species = species_ ? &*species_ : nullptr,
                      .ui = context.ui}),
      matchScreen_(context.ui), tradeScreen_({.trader = game_.systems.itemTrader,
                                              .session = game_.session.get(),
                                              .sound = game_.session->sound(),
                                              .ui = context.ui}),
      birdScreen_({.transport = game_.systems.birdTransport,
                   .session = game_.session.get(),
                   .sound = game_.session->sound(),
                   .ui = context.ui,
                   .story = game_.systems.story}),
      worldMapScreen_({.ui = context.ui, .files = context.files}),
      cardShopScreen_({.shop = game_.systems.cardShop, .ui = context.ui}),
      meritShopScreen_(
          {.shop = game_.systems.meritShop, .session = game_.session.get(), .ui = context.ui}),
      moneyBoxScreen_({.shop = game_.systems.shop, .ui = context.ui}),
      fieldItemsScreen_(*game_.systems.fieldItems, *game_.systems.inventory, *context.ui,
                        game_.session->sound()),
      awardScreen_(game_.systems.medals, *game_.session, *context.ui, game_.session->sound()),
      praiseScold_(*game_.session, *context.ui),
      fishingScreen_(*game_.systems.fishing, *game_.systems.inventory, *game_.session, *context.ui,
                     game_.session->sound(), game_.systems.story),
      curlingScreen_(*game_.systems.curling, *game_.session, *context.ui, game_.session->sound()) {
  session::GameSession& session = *game_.session;
  if (game_.systems.story != nullptr) {
    storyTextbox_.emplace(*context.ui, *game_.systems.story, session);
  }
  session.attachField(scene_->field());
  // map.c: isInDaytimeTransition: the field's time-of-day fade holds the evolution checks.
  game_.systems.evolution->setDaytimeFade(
      [this] { return scene_ != nullptr && scene_->daytimeFadeRunning(); });
  game_.systems.story->attachNpcs(&scene_->npcs());
  storyLink_ = std::make_unique<StoryFieldLink>(*game_.systems.story, *scene_);
  if (game_.systems.scenes != nullptr) {
    storyLink_->followScreens(&game_.systems.scenes->screens());
  }
  storyScenes_.attachShop(game_.systems.shop);
  storyScenes_.attachItemKeeper(game_.systems.itemKeeper);
  itemKeeperScreen_.setSpecies(species_ ? &*species_ : nullptr);
  storyScenes_.attachGym(&gymScreen_);
  storyScenes_.attachTournament(game_.systems.tournament, &session);
  storyScenes_.attachCurling(&curlingScreen_);
  storyScenes_.attachBossArrival(&bossArrival_);
  storyScenes_.attachDeath(&deathScene_);
  storyScenes_.attachEnding(&endingScene_);
  storyScenes_.attachEggBox(&eggBoxScreen_);
  storyScenes_.attachDiscard(&discardScreen_);
  storyScenes_.attachJukebox(&jukeboxScreen_);
  storyScenes_.attachTournamentSchedule(&scheduleScreen_);
  storyScenes_.attachTournamentBracket(&bracketScreen_);
  storyScenes_.attachTournamentMatch(&matchScreen_, game_.systems.battle,
                                     species_ ? &*species_ : nullptr);
  storyScenes_.attachTrade(&tradeScreen_);
  tradeScreen_.setSpecies(species_ ? &*species_ : nullptr);
  storyScenes_.attachBirdTransport(&birdScreen_);
  birdScreen_.setSpecies(species_ ? &*species_ : nullptr);
  storyProps_.setSpecies(species_ ? &*species_ : nullptr);
  storyProps_.setBossArrival(&bossArrival_);
  bossArrival_.setSound(session.sound());
  storyScenes_.attachCardShop(&cardShopScreen_);
  cardShopScreen_.setSpecies(species_ ? &*species_ : nullptr);
  storyScenes_.attachMeritShop(&meritShopScreen_);
  storyScenes_.attachMoneyBox(&moneyBoxScreen_);
  meritShopScreen_.setSpecies(species_ ? &*species_ : nullptr);
  if (game_.systems.cards != nullptr) {
    game_.systems.cards->setDigimon(species_ ? &*species_ : nullptr);
  }
  storyScenes_.attachSaveMachine(&saveMachine_);
  if (auto newGameScene = loadNewGameSceneRules(*context.files, session.rules().current().id())) {
    namingBox_ = std::make_unique<NamingBox>(*context.ui, std::move(newGameScene->naming),
                                             newGameScene->sounds);
    namingBox_->setSound(session.sound());
    namingBox_->setLongNames(context.userSettings->longNames.value_or(true));
    storyScenes_.attachNaming(namingBox_.get(), &session);
  }
  // A loaded game's partner may be another species than the game pack's partner model.
  scene_->attachSpeciesRadius([this](std::string_view species) {
    const auto* row = species_ ? species_->find(species) : nullptr;
    return row != nullptr ? static_cast<float>(row->radius) : 0.0F;
  });
  const ruleset::SpeciesExtra extra = speciesExtraOf(session);
  scene_->attachSpeciesClips([extra](std::string_view species) {
    const ruleset::SpeciesPatch* patch = extra.find(species);
    return patch != nullptr ? FieldScene::ClipStandIns{patch->clips.begin(), patch->clips.end()}
                            : FieldScene::ClipStandIns{};
  });
  scene_->attachSpeciesScale([extra](std::string_view species) {
    const ruleset::SpeciesPatch* patch = extra.find(species);
    return patch != nullptr ? patch->fieldScale.value_or(1.0F) : 1.0F;
  });
  scene_->attachSound(session.sound(), [this](std::string_view species) {
    const auto* row = species_ ? species_->find(species) : nullptr;
    return row != nullptr
               ? std::optional{FieldScene::SpeciesBox{.radius = static_cast<float>(row->radius),
                                                      .height = static_cast<float>(row->height)}}
               : std::nullopt;
  });
  if (auto shown = scene_->showPartnerSpecies(session.partnerView().species); !shown) {
    core::logWarning("{}",
                     core::formatMessage(messages::PartnerModelNotShown,
                                         session.partnerView().species, shown.error().message));
  }
  // The chibi style (chibi_style.hpp): the saved mode (external models unless chosen), or all
  // with --chibi for a run. The options change it through the game (game.cpp).
  const ChibiMode chibi =
      context.options->chibi ? ChibiMode::All : startChibiMode(context.userSettings->chibiStyle);
  scene_->setChibiMode(chibi);
  context_.ui->setSetting(ChibiStyleSetting, std::string{chibiModeName(chibi)});
  // Smooth motion (game::field::Field::setSmoothMotion), the engine's, on unless turned off.
  const bool smooth = context.userSettings->smoothMotion.value_or(true);
  scene_->field().setSmoothMotion(smooth);
  context_.ui->setSetting(SmoothMotionSetting, smooth);
  // HD scenery (hd_scenery_setting.hpp), the engine's, on unless turned off.
  scene_->setHdScenery(startHdScenery(*context.userSettings));
  // The time in digits under the HUD's clock, the engine's, on unless turned off.
  context_.ui->setSetting(DigitalTimeSetting, context.userSettings->digitalTime.value_or(true));
  // The evolution requirements on the chart (an engine improvement), on unless turned off.
  context_.ui->setSetting(EvolutionRequirementsSetting,
                          context.userSettings->evolutionRequirements.value_or(true));
  // The signs painted in the maps, in the player's language unless turned off.
  const bool translatedSigns = context.userSettings->translatedSigns.value_or(true);
  scene_->setTranslatedSigns(translatedSigns);
  context_.ui->setSetting(TranslatedSignsSetting, translatedSigns);
  // The status page's extras (the tiredness and the care mistakes), on unless turned off.
  context_.ui->setSetting(StatusExtrasSetting, context.userSettings->statusExtras.value_or(true));
  context_.ui->setSetting(EvolutionActiveSetting, false);
  // Long names in the naming box, on unless turned off.
  context_.ui->setSetting(LongNamesSetting, context.userSettings->longNames.value_or(true));
  // The recruit bars (the shop job's and the stones game's), on unless turned off.
  context_.ui->setSetting(RecruitBarSetting, context.userSettings->shopJobBar.value_or(true));
  // Continuous music across the maps that share a song, on unless turned off.
  const bool continuousMusic = context.userSettings->continuousMusic.value_or(true);
  if (session::SessionMusic* music = game_.session->music(); music != nullptr) {
    music->setContinuousMusic(continuousMusic);
  }
  context_.ui->setSetting(ContinuousMusicSetting, continuousMusic);
  // Several needs in the partner's bubble at once, on unless turned off.
  context_.ui->setSetting(NeedBubbleStackSetting,
                          context.userSettings->needBubbleStack.value_or(true));
  // The weather, on unless turned off: the playthrough's seed from the save, a kind --weather
  // forces, and its sound.
  if (game_.systems.weather != nullptr) {
    scene_->setWeatherSeed(game_.systems.weather->seed());
  }
  scene_->forceWeather(context.options->weather);
  const bool weather = context.userSettings->weather.value_or(true);
  scene_->setWeatherEnabled(weather);
  context_.ui->setSetting(WeatherSetting, weather);
  // The seasons, on unless turned off, and a season --season forces.
  scene_->forceSeason(context.options->season);
  const bool seasons = context.userSettings->seasons.value_or(true) && !context.options->seasonsOff;
  scene_->setSeasonsEnabled(seasons);
  context_.ui->setSetting(SeasonsSetting, seasons);
  weatherAmbience_ = std::make_unique<WeatherAmbience>(context.sound, *context.files);
  // The island map on M, on unless turned off.
  context_.ui->setSetting(WorldMapSetting, context.userSettings->worldMap.value_or(true));
  // The enemies on the minimap, on unless turned off.
  minimapEnemiesShown_ = context.userSettings->minimapEnemies.value_or(true);
  context_.ui->setSetting(MinimapEnemiesSetting, minimapEnemiesShown_);
  if (context.files != nullptr) {
    minimapEnemies_.emplace(*context.files);
  }
  // The canonical stages on the chart, off unless turned on.
  context_.ui->setSetting(CanonicalStagesSetting,
                          context.userSettings->canonicalStages.value_or(false));
  // The save indicator's document goes up before the HUD's, which takes the focus.
  saveIndicator_.emplace(*context_.ui, game_.session->savesWritten(),
                         context.userSettings->saveNotice.value_or(true));
  // The autosave, on unless turned off.
  const bool autosave = context.userSettings->autosave.value_or(true);
  game_.session->setAutosaveEnabled(autosave);
  context_.ui->setSetting(AutosaveSetting, autosave);
  setBulkTrades(context.userSettings->bulkTrades.value_or(true));
  context_.ui->show("hud");
  fieldNumbers_.emplace(*context_.ui, scene_->field().rules().transition.frameSeconds);
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
  game_.systems.evolution->setDaytimeFade({});
  storyLink_.reset();
  game_.systems.story->attachNpcs(nullptr);
  for (const std::string_view document : {std::string_view{"hud"},
                                          BattleDocument,
                                          PartnerMenuDocument,
                                          DestinationChoiceDocument,
                                          SleepMenuDocument,
                                          DialogDocument,
                                          ShopMenuDocument,
                                          ItemKeeperDocument,
                                          DiscardMenuDocument,
                                          JukeboxDocument,
                                          TournamentScheduleDocument,
                                          TournamentBracketDocument,
                                          TournamentMatchDocument,
                                          TradeMenuDocument,
                                          CardShopDocument,
                                          MeritShopDocument,
                                          FieldItemNoticeDocument,
                                          AwardNoticeDocument,
                                          FishingBaitDocument,
                                          FishingNoticeDocument,
                                          FishingHudDocument,
                                          CurlingHudDocument,
                                          EggBoxDocument,
                                          NamingBoxDocument,
                                          LivesBoxDocument,
                                          BirdTransportDocument,
                                          MoneyBoxDocument,
                                          WorldMapDocument}) {
    context_.ui->hide(document);
  }
}

bool PlaySession::takesText() const {
  return namingBox_ != nullptr && namingBox_->isOpen();
}

void PlaySession::type(std::string_view text, bool enter) {
  if (namingBox_ != nullptr) {
    namingBox_->type(text, enter);
  }
}

bool PlaySession::holdsMenu() const {
  // The item keeper and the discard menu read pause (P) for an item's description.
  return takesText() || gymScreen_.active() || itemKeeperScreen_.active() ||
         discardScreen_.active() || scheduleScreen_.active();
}

void PlaySession::pressStory(const input::InputState& input) {
  if (gymScreen_.active()) {
    gymScreen_.pressCancel(input.wasPressed("menu"),
                           input.isDown("menu") || input.isDown("cancel"));
  }
  if (storyScenes_.pressNaming(input)) {
    return;
  }
  if (storyTextbox_) {
    storyTextbox_->press(input);
  }
}

void PlaySession::openWorldMap() {
  static const std::set<std::string, std::less<>> None;
  const session::MapVisits* visits = game_.systems.mapVisits;
  worldMapScreen_.open(*scene_, visits != nullptr ? visits->maps() : None);
}

void PlaySession::pressWorldMap(const input::InputState& input, bool canOpen) {
  const ui::UiValue enabled = context_.ui->setting(WorldMapSetting);
  const WorldMapKeyStep step = worldMapKeyStep({
      .mapPressed = input.wasPressed(WorldMapAction),
      .cancelPressed = false,
      .open = worldMapScreen_.isOpen(),
      .canOpen = canOpen && !paused() && menuShutBy().empty() && walksByPointer(),
      .enabled = !std::holds_alternative<bool>(enabled) || std::get<bool>(enabled),
  });
  if (step == WorldMapKeyStep::Open) {
    openWorldMap();
  } else if (step == WorldMapKeyStep::Close) {
    worldMapScreen_.close();
  }
}

void PlaySession::pressItemKeeper(const input::InputState& input) {
  itemKeeperScreen_.press(input);
  discardScreen_.press(input);
  jukeboxScreen_.press(input);
  scheduleScreen_.press(input);
  tradeScreen_.press(input);
  birdScreen_.press(input);
  cardShopScreen_.press(input);
  meritShopScreen_.press(input);
}

void PlaySession::pressPartnerMenu(const input::InputState& input) {
  partnerMenu_.press(input);
}

void PlaySession::pressBattle(const input::InputState& input) {
  if (input.wasPressed("extra")) {
    battleScreen_.pressFinisher();
  }
  if (input.wasPressed("confirm")) {
    storyScenes_.pressTournamentConfirm();
  }
}

void PlaySession::tick(const game::field::FieldInput& intent, const FishingButtons& fishing) {
  fieldWalks_ = false;
  tickSystems(intent, fishing);
  tickWater();
  // A tick that did not leave the player walking freely (a talk, a battle, a gym course, a screen)
  // drops the click's target, so the player does not walk on to it once he is let go.
  if (!walksByPointer()) {
    scene_->field().dropMoveTarget();
  }
}

bool PlaySession::walksByPointer() const {
  const game::field::Field& field = scene_->field();
  const auto& story = game_.systems.story;
  return fieldWalks_ && !battleScreen_.active() && field.playerFree() &&
         !field.mapChangePending() &&
         (story == nullptr || !storyTextbox_ || !story->dialogue().view().open);
}

void PlaySession::tickWater() {
  // fish.c: FISH_init adds the water of a fishing map as one of the field's objects, after the
  // player's and the partner's; tickObjects runs them in that order every frame, under the pause
  // menu too (main.c: gameLoop, utils.c: pauseFrame), so the fish roll after the field. A battle
  // keeps it: BTL_battleTickFrame runs tickObjects after the fighters, and nothing removes the
  // water (measured: FISH_tickFishingWorld ticks while the battle loads; the original then hangs,
  // as the battle's code lands over the fishing's, a limit the game does not have). The water
  // skips the frames the partner evolves (FISH_tickFishingWorld, PARTNER_STATE 13).
  if (evolutionScene_.active()) {
    fishingScreen_.hideWater(*scene_);
    return;
  }
  fishingScreen_.tickWater(*scene_, TickSeconds);
}

void PlaySession::tickSystems(const game::field::FieldInput& intent,
                              const FishingButtons& fishing) {
  const ui::Ui& ui = *context_.ui;
  // Every tick starts with the characters where they stand: the screens below that run instead of
  // the field (a battle, the evolution, a sleep, the fishing, a menu) move them themselves or not
  // at all, and none is drawn sliding from where an older tick left it.
  scene_->settleActors();
  // The menus pause the field and the game time; a sleep or a battle runs instead of the field.
  sleepScreen_.tick(TickSeconds);
  saveMachine_.tick(TickSeconds);
  bracketScreen_.tick(TickSeconds);
  storyScenes_.tickTournament(TickSeconds, battleScreen_, *scene_);
  const auto& fieldRules = game_.systems.fieldItems->rules();
  sleepScreen_.tickField(*scene_, static_cast<float>(TickSeconds),
                         fieldRules ? std::optional{fieldRules->sleepSound} : std::nullopt);
  if (sleepScreen_.active() || shopScreen_.active() || itemKeeperScreen_.active() ||
      discardScreen_.active() || jukeboxScreen_.active() || tradeScreen_.active() ||
      birdScreen_.active() || cardShopScreen_.active() || meritShopScreen_.active() ||
      scheduleScreen_.active() || bracketScreen_.playing() || storyScenes_.tournamentIntro() ||
      worldMapScreen_.isOpen()) {
    return;
  }
  // partner.c: partnerTickOverworld runs the partner's state: the care scenes below (praise and
  // scold 4 and 15, feeding 5, the toilet 6), the sleep (3), the evolution (13) and the death (8,
  // 14) each run instead of the field; the normal state (1) is the field's following, and the
  // condition bubble (10) the HUD's. A pile on the field (7) and eating one (9) are FieldPoop's.
  if (fieldPoop_.active()) {
    guardFactsChanged_ = fieldPoop_.tick(static_cast<float>(TickSeconds)) || guardFactsChanged_;
    return;
  }
  if (fieldToilet_.active()) {
    guardFactsChanged_ = fieldToilet_.tick(static_cast<float>(TickSeconds)) || guardFactsChanged_;
    return;
  }
  if (feedScene_.active()) {
    guardFactsChanged_ = feedScene_.tick(static_cast<float>(TickSeconds)) || guardFactsChanged_;
    return;
  }
  if (praiseScold_.active()) {
    guardFactsChanged_ =
        praiseScold_.tick(*scene_, static_cast<float>(TickSeconds), *game_.systems.fieldItems) ||
        guardFactsChanged_;
    return;
  }
  // partner.c: partnerTickEvolving stops the game time and holds the player for the scene.
  evolutionScene_.tick(TickSeconds);
  if (evolutionScene_.active()) {
    return;
  }
  // murd.c and dooa.c: a lost life's or a death's scene holds the field once a battle is over; one
  // the story asked for leaves it to the story's script.
  if (!battleScreen_.active()) {
    deathScene_.tick(TickSeconds);
    endingScene_.tick(TickSeconds);
    if (deathScene_.holdsField()) {
      return;
    }
  }
  scene_->holdCamera(battleScreen_.active());
  if (battleScreen_.active()) {
    // fade.c: a fade in the battle started under runs on with the drawing.
    scene_->field().tickFadeIn(static_cast<float>(TickSeconds));
    if (!ui.isVisible(MenuDocument)) {
      battleScreen_.tick(TickSeconds);
    }
  } else if (!ui.isVisible(MenuDocument) && !ui.isVisible(MainMenuDocument) &&
             !ui.isVisible(PartnerMenuDocument)) {
    // The fishing runs instead of the field and the game time (clock.c: tickGameClock).
    if (fishingScreen_.active()) {
      fishingScreen_.tick(*scene_, fishing, TickSeconds);
      return;
    }
    // kar.c: the stones game runs instead of the field while the story waits on it. It starts on
    // the arrival at the rink, under the map's fade in, which runs on with the drawing (fade.c).
    if (curlingScreen_.active()) {
      scene_->field().tickFadeIn(static_cast<float>(TickSeconds));
      curlingScreen_.tick(*scene_, TickSeconds);
      return;
    }
    // tamer.c: checkMapInteraction talks and confirms on a cell only while no script runs
    // (IS_SCRIPT_PAUSED 1), so the press that closes a textbox never starts a talk again.
    game::field::FieldInput fieldIntent = intent;
    if (fieldIntent.confirm && game_.systems.story != nullptr &&
        game_.systems.story->scriptRunning()) {
      core::logDebug("[port:dialogue] checkMapInteraction (tamer.c:671): confirm ignored while a "
                     "script runs");
      fieldIntent.confirm = false;
    }
    // An item on the ground or a chest in reach takes the confirm, and its notice stops the field.
    const game::field::Field& field = scene_->field();
    if (!field.mapChangePending() && field.fadeLevel() == 0.0F &&
        fieldItemsScreen_.tick(field, fieldIntent.confirm, TickSeconds)) {
      fieldIntent.confirm = false;
    }
    if (fieldItemsScreen_.active()) {
      return;
    }
    // tamer.c: tamerTickWalkingState checks the medals and opens a due award notice while the
    // player walks freely; the notice stops the field and the game time until it is confirmed.
    if (!field.mapChangePending() && field.fadeLevel() == 0.0F && !gymScreen_.active() &&
        !field.heldByScript() && !game_.systems.story->scriptRunning()) {
      awardScreen_.tick();
    }
    if (awardScreen_.active()) {
      return;
    }
    // A gym course takes the player's control; its buttons are the course's.
    if (gymScreen_.active()) {
      fieldIntent = {};
    }
    fieldWalks_ = !gymScreen_.active();
    // Without a course it only closes the last one's stats box.
    gymScreen_.tick(TickSeconds, intent);
    scene_->tick(fieldIntent, static_cast<float>(TickSeconds));
    game_.systems.story->runNpcContacts(scene_->field().npcContacts());
    storyLink_->afterFieldTick();
    game_.session->tick(TickSeconds);
    applyStoryMapEdits(*scene_, game_.systems.storyOrders);
    if (game_.systems.storyOrders != nullptr) {
      const std::vector<std::int32_t> damage = game_.systems.storyOrders->takePartnerDamage();
      std::optional<screens::WindowFraction> feet;
      if (!damage.empty()) {
        if (const auto shown = scene_->fighterFeetOnWindow(std::nullopt)) {
          feet = screens::WindowFraction{.x = shown->x, .y = shown->y};
        }
      }
      fieldNumbers_->add(damage, feet);
    }
    fieldNumbers_->tick(TickSeconds);
    scene_->finishTick();
    // map.c: changeMap clears the last map's dropped items and chests as the new map loads, before
    // its head script spawns this map's: none of them is drawn over the new map's fade in.
    game_.systems.fieldItems->items().enterMap(scene_->field().mapId());
    storyLink_->afterMapChange();
    if (auto notice = storyLink_->takeLockNotice()) {
      showLockNotice(*notice);
    }
  }
}

void PlaySession::showLockNotice(const std::string& textKey) {
  if (context_.ui->isVisible(DialogDocument)) {
    return;
  }
  screens::publish(
      *context_.ui,
      dialogSettings({.text = textKey, .file = {}, .question = false, .startOnYes = false}),
      "dialog.revision");
  context_.ui->show(DialogDocument, true);
  lockNoticeShown_ = true;
}

void PlaySession::setBulkTrades(bool enabled) {
  tradeScreen_.setBulk(enabled);
  if (game_.systems.cardShop != nullptr) {
    game_.systems.cardShop->setBulkPacks(enabled);
  }
  context_.ui->setSetting(BulkTradesSetting, enabled);
}

bool PlaySession::handle(const ui::UiCommand& command) {
  if (eggBoxScreen_.handle(command) || shopScreen_.handle(command) ||
      itemKeeperScreen_.handle(command) || discardScreen_.handle(command) ||
      jukeboxScreen_.handle(command) || scheduleScreen_.handle(command) ||
      tradeScreen_.handle(command) || birdScreen_.handle(command) ||
      cardShopScreen_.handle(command) || meritShopScreen_.handle(command) ||
      sleepScreen_.handle(command) || saveMachine_.handle(command) ||
      praiseScold_.handle(command) || feedScene_.handle(command) || fieldToilet_.handle(command) ||
      fieldPoop_.handle(command) || fieldItemsScreen_.handle(command) ||
      awardScreen_.handle(command) || fishingScreen_.handle(command) ||
      battleScreen_.handle(command) || freeExploration_.handle(command)) {
    return true;
  }
  // The HUD's menu button follows Tab's gate (the walking state's tick, tamer.c:520).
  if (command.name == "open_menu") {
    if (const std::string_view shutBy = menuShutBy(); !shutBy.empty()) {
      core::logDebug("[port:menu] walking state (tamer.c:520): the menu stays shut ({})", shutBy);
      return true;
    }
    return false;
  }
  if (command.name == SmoothMotionSetting) {
    if (const bool* smooth = std::get_if<bool>(&command.value)) {
      scene_->field().setSmoothMotion(*smooth);
      context_.userSettings->smoothMotion = *smooth;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    context_.ui->setSetting(SmoothMotionSetting, scene_->field().smoothMotion());
    return true;
  }
  if (command.name == "world_map_action") {
    if (const auto* action = std::get_if<std::string>(&command.value);
        action != nullptr && *action == "cancel") {
      worldMapScreen_.close();
    }
    return true;
  }
  if (command.name == WorldMapSetting) {
    if (const bool* enabled = std::get_if<bool>(&command.value)) {
      context_.ui->setSetting(WorldMapSetting, *enabled);
      context_.userSettings->worldMap = *enabled;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == MinimapEnemiesSetting) {
    if (const bool* shown = std::get_if<bool>(&command.value)) {
      minimapEnemiesShown_ = *shown;
      context_.ui->setSetting(MinimapEnemiesSetting, *shown);
      context_.userSettings->minimapEnemies = *shown;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == NeedBubbleStackSetting) {
    if (const bool* stacked = std::get_if<bool>(&command.value)) {
      context_.ui->setSetting(NeedBubbleStackSetting, *stacked);
      context_.userSettings->needBubbleStack = *stacked;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == RecruitBarSetting) {
    if (const bool* shown = std::get_if<bool>(&command.value)) {
      context_.ui->setSetting(RecruitBarSetting, *shown);
      context_.userSettings->shopJobBar = *shown;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == ContinuousMusicSetting) {
    if (const bool* continuous = std::get_if<bool>(&command.value)) {
      if (session::SessionMusic* music = game_.session->music(); music != nullptr) {
        music->setContinuousMusic(*continuous);
      }
      context_.ui->setSetting(ContinuousMusicSetting, *continuous);
      context_.userSettings->continuousMusic = *continuous;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == DigitalTimeSetting) {
    if (const bool* digits = std::get_if<bool>(&command.value)) {
      context_.ui->setSetting(DigitalTimeSetting, *digits);
      context_.userSettings->digitalTime = *digits;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == BulkTradesSetting) {
    if (const bool* bulk = std::get_if<bool>(&command.value)) {
      setBulkTrades(*bulk);
      context_.userSettings->bulkTrades = *bulk;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == EvolutionRequirementsSetting) {
    if (const bool* shown = std::get_if<bool>(&command.value)) {
      context_.userSettings->evolutionRequirements = *shown;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
      context_.ui->setSetting(EvolutionRequirementsSetting, *shown);
    }
    return true;
  }
  if (command.name == CanonicalStagesSetting) {
    if (const bool* canonical = std::get_if<bool>(&command.value)) {
      context_.ui->setSetting(CanonicalStagesSetting, *canonical);
      context_.userSettings->canonicalStages = *canonical;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == LongNamesSetting) {
    if (const bool* longNames = std::get_if<bool>(&command.value)) {
      if (namingBox_ != nullptr) {
        namingBox_->setLongNames(*longNames);
      }
      context_.ui->setSetting(LongNamesSetting, *longNames);
      context_.userSettings->longNames = *longNames;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == StatusExtrasSetting) {
    if (const bool* extras = std::get_if<bool>(&command.value)) {
      context_.ui->setSetting(StatusExtrasSetting, *extras);
      context_.userSettings->statusExtras = *extras;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == TranslatedSignsSetting) {
    if (const bool* translated = std::get_if<bool>(&command.value)) {
      scene_->setTranslatedSigns(*translated);
      context_.ui->setSetting(TranslatedSignsSetting, *translated);
      context_.userSettings->translatedSigns = *translated;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == AutosaveSetting) {
    if (const auto autosave = changeAutosave(command.value, *context_.ui, *context_.userSettings,
                                             context_.settingsFile)) {
      game_.session->setAutosaveEnabled(*autosave);
    }
    return true;
  }
  if (command.name == SaveNoticeSetting) {
    if (const bool* notice = std::get_if<bool>(&command.value)) {
      saveIndicator_->setEnabled(*notice);
      context_.userSettings->saveNotice = *notice;
      if (context_.settingsFile) {
        saveUserSettings(*context_.settingsFile, *context_.userSettings);
      }
    }
    return true;
  }
  if (command.name == "to_title") {
    // Not over a sleep or a battle, whose state a later save would not see whole.
    if (!sleepScreen_.active() && !battleScreen_.active() && !saveMachine_.active() &&
        !praiseScold_.active() && !feedScene_.active() && !fieldToilet_.active() &&
        !fieldPoop_.active()) {
      context_.ui->hide(MenuDocument);
      screens::publish(
          *context_.ui,
          dialogSettings(
              {.text = "save.to_title_confirm", .file = {}, .question = true, .startOnYes = false}),
          "dialog.revision");
      context_.ui->show(DialogDocument, true);
      toTitleAsked_ = true;
      lockNoticeShown_ = false;
    }
    return true;
  }
  if (command.name == "dialog_answer" && lockNoticeShown_) {
    context_.ui->hide(DialogDocument);
    lockNoticeShown_ = false;
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
  if (const auto item = std::exchange(commands.groundDrop, std::nullopt)) {
    const game::field::Field& field = scene_->field();
    if (!game_.systems.fieldItems->dropItem(field.mapId(), *item, field.player().position,
                                            field.collision())) {
      core::logWarning("{}", core::formatMessage(messages::DropItemFailed, *item));
    }
  }
  for (const std::string& where : std::exchange(commands.placements, {})) {
    const auto placement = placementFromText(where);
    if (!placement) {
      core::logWarning("{}", core::formatMessage(messages::PlaceFailed, placement.error().message));
    } else if (!scene_->place(*placement)) {
      core::logWarning(
          "{}", core::formatMessage(messages::PlaceFailed,
                                    core::formatMessage(messages::NoNpcToPlace, placement->npc)));
    }
  }
  for (const std::string& what : std::exchange(commands.storyWrites, {})) {
    auto write = storyWriteFromText(what);
    if (write && game_.systems.story == nullptr) {
      write = core::fail(std::string{messages::NoStory});
    }
    if (write && write->kind == StoryWrite::Kind::Money) {
      if (game_.systems.inventory == nullptr) {
        write = core::fail(std::string{messages::NoGame});
      } else {
        game::inventory::Inventory& bag = game_.systems.inventory->inventory();
        bag.spendMoney(bag.money());
        bag.addMoney(write->value);
      }
    } else if (write && write->kind == StoryWrite::Kind::Tally) {
      game_.systems.story->setShopTally(write->value);
    } else if (write && write->kind == StoryWrite::Kind::Card) {
      auto given = game_.systems.cards != nullptr
                       ? applyCardWrite(*game_.systems.cards, *write)
                       : core::fail(std::format("card {}: the game has no cards", write->number));
      if (!given) {
        write = std::unexpected{given.error()};
      }
    } else if (write) {
      if (auto written = applyStoryWrite(game_.systems.story->state(), *write); !written) {
        write = std::unexpected{written.error()};
      }
    }
    if (!write) {
      core::logWarning("{}", core::formatMessage(messages::StorySetFailed, write.error().message));
    }
  }
  for (const std::string& what : std::exchange(commands.partnerWrites, {})) {
    if (auto write = partnerWriteFromText(what)) {
      applyPartnerWrite(game_.session->partner(), *write);
    } else {
      core::logWarning("{}",
                       core::formatMessage(messages::PartnerSetFailed, write.error().message));
    }
  }
  for (const std::string& lesson : std::exchange(commands.lessons, {})) {
    if (auto learned = learnMove(game_.systems.battle, *game_.session, species, lesson); !learned) {
      core::logWarning("{}",
                       core::formatMessage(messages::LearnMoveFailed, learned.error().message));
    }
  }
  if (const auto npcs = std::exchange(commands.battle, std::nullopt)) {
    startBattle(battleScreen_, species, *scene_, *npcs);
  }
  if (std::exchange(commands.battleWin, false) && !battleScreen_.defeatEnemies()) {
    core::logWarning("{}", core::formatMessage(messages::BattleWinFailed, messages::NoFight));
  }
  if (const auto side = std::exchange(commands.battleTimeUp, std::nullopt);
      side && !battleScreen_.runOutClock(*side)) {
    core::logWarning("{}", core::formatMessage(messages::BattleTimeUpFailed, messages::NoFight));
  }
  if (std::exchange(commands.battleLose, false) && !battleScreen_.defeatPartner()) {
    core::logWarning("{}", core::formatMessage(messages::BattleLoseFailed, messages::NoFight));
  }
  if (game_.systems.scenes != nullptr) {
    storyScenes_.update(*game_.systems.scenes, battleScreen_, species, *scene_);
    storyProps_.update(*game_.systems.scenes);
    if (game_.systems.scenes->takeGameEnded()) {
      requestMovie(MovieMoment::Ending);
    }
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
    fishingScreen_.enterMap(*scene_, scene_->field().mapId());
    if (const auto& change = scene_->lastMapChange(); change && autotest != nullptr) {
      autotest->mapChanged(*change);
    }
    stats.skipNext(MapChangeSettleFrames);
  }
  const game::field::Field& field = scene_->field();
  ui.setMinimapMarkers(markerOf(field.player()), markerOf(field.partner()), minimapEnemyMarkers());

  partnerMenu_.setBagContext(battleScreen_.active() ? game::inventory::UseContext::Battle
                                                    : game::inventory::UseContext::Field);
  partnerMenu_.update();
  chartRequirements_.update(*game_.session);
  shopScreen_.update();
  itemKeeperScreen_.update();
  // tamer.c: the menu opens with no script, no fade and no text box; the field has no scripts or
  // text boxes yet.
  const bool fieldIdle = !battleScreen_.active() && !gymScreen_.active() &&
                         !fishingScreen_.active() && !evolutionScene_.active() &&
                         !deathScene_.active() && !feedScene_.active() && !fieldToilet_.active() &&
                         !fieldPoop_.active() && !field.mapChangePending() &&
                         field.fadeLevel() == 0.0F;
  sleepScreen_.update(fieldIdle);
  gymScreen_.update();
  praiseScold_.update(fieldIdle);
  feedScene_.update(fieldIdle);
  fishingScreen_.update(fieldIdle);
  if (fieldIdle && !paused()) {
    guardFactsChanged = fieldToilet_.checkCell() || guardFactsChanged;
  }
  // partner_impl.c: createPoopPile, tickPoopDetection: the piles the partner left, and the one a
  // partner that eats piles finds; not once the toilet cell sent the partner off this frame (state
  // 6: tickPoopDetection runs only in the normal state).
  if (fieldIdle && !paused() && !fieldToilet_.active()) {
    fieldPoop_.update();
  }
  // item.c: handleChips: the auto pilot chip calls its map-head section if no script runs; the
  // story drops the request otherwise, and the chip is gone either way.
  if (const auto section = game_.systems.inventory->takeScriptSection(); section) {
    const auto outcome =
        game_.systems.story->request({.script = game::story::StoryRunner::MapHeadScript,
                                      .section = static_cast<std::uint16_t>(*section)},
                                     game::story::StartTiming::NextFrame);
    core::logDebug("[port:items] handleChips (item.c:2260): section {} of the map-head script {}",
                   *section,
                   outcome == game::story::RequestOutcome::Accepted ? "asked for" : "dropped");
  }
  // particle.c: addHealingParticleEffect, from the item the partner just took in the field.
  if (const auto healing = game_.systems.inventory->takeHealingParticles(); healing) {
    const assets::DigimonRow* row =
        species_ ? species_->find(session.partnerView().species) : nullptr;
    const glm::vec3 feet = field.partner().position;
    // The original's axes: y and z point the other way.
    scene_->fieldEffects().addHealing(
        {static_cast<std::int32_t>(feet.x), static_cast<std::int32_t>(-feet.y),
         static_cast<std::int32_t>(-feet.z)},
        row != nullptr ? row->height : 0, row != nullptr ? row->radius : 0, healing->ring);
  }
  const auto partnerEvents = session.takePartnerEvents();
  if (fellSick(partnerEvents)) {
    awardScreen_.noteSickness(partnerNameOf(session, species_ ? &*species_ : nullptr));
  }
  showPartnerEvents(deathScene_.take(evolutionScene_.take(partnerEvents)), session, *scene_);
  guardFactsChanged = guardFactsChanged || !partnerEvents.empty() ||
                      (battleRanLastFrame_ && !battleScreen_.active());
  if (battleRanLastFrame_ != battleScreen_.active()) {
    context_.ui->setSetting(BattleActiveSetting, battleScreen_.active());
  }
  // The battle draws its own numbers; the field's go with the field it leaves.
  if (battleScreen_.active() && fieldNumbers_->active()) {
    fieldNumbers_->clear();
  }
  battleRanLastFrame_ = battleScreen_.active();
  if (evolutionRanLastFrame_ != evolutionScene_.active()) {
    evolutionRanLastFrame_ = evolutionScene_.active();
    context_.ui->setSetting(EvolutionActiveSetting, evolutionRanLastFrame_);
  }
  if (guardFactsChanged) {
    scene_->refreshScriptWarps();
  }

  // clock.c: renderGameClock slides the clock and the bars off the screen whenever game time stands
  // still: a script's scene or talk (script_interp.c: scriptPauseGame), a fade, a map change.
  // partner_impl.c: handleConditionBubble runs in the partner's normal state (partner.c:
  // partnerTickNormal), so a care scene shows no need bubble; and in its training state
  // (partnerSetState(10)), so a course shows it while game time stands still.
  const bool fieldRunning = !paused() && field.gameTimeRan();
  publishMinimapAllowed(MinimapScene{
      .fieldRunning = fieldRunning,
      .battle = battleScreen_.active(),
      .minigame = gymScreen_.active() || fishingScreen_.active() || curlingScreen_.active() ||
                  scheduleScreen_.active() || bracketScreen_.opened() || matchScreen_.shown() ||
                  evolutionScene_.active() || deathScene_.active() || endingScene_.active()});
  showButterfly(fieldIdle && !field.heldForCare(), session);
  fieldHud_.update(fieldRunning, fieldRunning || (!paused() && gymScreen_.active()),
                   field.heldForCare()
                       ? std::nullopt
                       : partnerBubbleOf(*scene_, species, session.partnerView().species));
  publishShopJob();
  battleScreen_.showIntro([&](const game::battle::Combatant& enemy, float lift) {
    return angerAnchorOf(*scene_, species, enemy, lift);
  });
  if (storyTextbox_) {
    // script_common.c: openShop and openRecycleShop speak in the dialogue box itself
    // (showShopkeeperTextbox, box 0), so the choice that called routine 8 (the recycle shop's
    // "regular or recycled") leaves the screen as the shop opens.
    storyTextbox_->cover(shopScreen_.active() || jukeboxScreen_.active() ||
                         scheduleScreen_.active() || tradeScreen_.active() ||
                         birdScreen_.active() || cardShopScreen_.active() ||
                         meritShopScreen_.active());
    storyTextbox_->update(*scene_, species);
  }
  fieldPoop_.show();
  if (!paused()) {
    storyProps_.advance(frameSeconds);
    bossArrival_.advance(frameSeconds);
  }
  storyProps_.show();
  bossArrival_.show();
  fieldItemsScreen_.show(*scene_);
  const game::partner::PartnerState& partner = session.partner().state();
  scene_->setPartnerMood(partner.conditions, partner.mood.happiness);
  scene_->update(frameSeconds, alpha, fieldClock());
  weatherAmbience_->update(scene_->weather(), frameSeconds);
  // The season's name next to the day in the menu, when it changes.
  const auto season = scene_->season();
  const std::string_view seasonName =
      season ? game::weather::seasonName(*season) : std::string_view{};
  if (seasonShown_ != seasonName) {
    seasonShown_ = seasonName;
    context_.ui->setSetting(SeasonNameSetting, std::string{seasonName});
  }
  keepFieldSnapshot(frameSeconds);
  // Last, after every save this frame could write (the autosave, a command, the save machine).
  saveIndicator_->noteSkippedAutosaves(session.autosavesSkipped());
  saveIndicator_->update(session.savesWritten(), frameSeconds);
}

// The shop job's bar (shop_job_bar.hpp): the customer count and the tally, published when they
// change.
std::vector<ui::MinimapMarker> PlaySession::minimapEnemyMarkers() {
  if (!minimapEnemies_ || scene_ == nullptr) {
    return {};
  }
  const game::story::StoryRunner* runner =
      game_.systems.story != nullptr ? game_.systems.story->runner() : nullptr;
  const std::set<std::int32_t>& fighting =
      minimapEnemies_->sectionsOf(runner != nullptr ? runner->mapScript() : std::nullopt);
  const game::field::FieldNpcs& npcs = scene_->field().npcs();
  return minimapEnemies(minimapEnemiesShown_, scene_->map().npcs, fighting,
                        [&](std::int32_t scriptId) { return npcs.positionOf(scriptId); });
}

void PlaySession::publishMinimapAllowed(const MinimapScene& scene) {
  const bool allowed = minimapAllowed(scene);
  if (minimapAllowed_ != allowed) {
    minimapAllowed_ = allowed;
    context_.ui->setSetting(MinimapAllowedSetting, allowed);
  }
}

void PlaySession::publishShopJob() {
  ShopJobFrame frame{.enabled = context_.userSettings->shopJobBar.value_or(true),
                     .map = scene_->field().mapId()};
  const session::StoryAdapter* story = game_.systems.story;
  if (story != nullptr && game_.systems.scenes != nullptr) {
    if (const auto& job = game_.systems.scenes->rules().shopJob; job.has_value()) {
      frame.customer =
          story->state().variable(static_cast<std::int32_t>(job->customersVariable)).value_or(0);
      frame.tally = story->shopTally();
    }
  }
  const ShopJobBarView view = shopJobBar_.update(frame);
  if (publishedShopJob_ == view) {
    return;
  }
  screens::publish(*context_.ui, settingsOf(view), ShopJobRevisionSetting);
  publishedShopJob_ = view;
}

// partner_impl.c: handleConditionBubble: a partner whose only condition is sulking, in its normal
// state, has the butterfly instead of a bubble (setButterfly); a scene, a battle or any other
// condition takes it away (unsetButterfly from partnerTick, tamerTick, handlePraiseScold and
// handleConditionBubble). It flies round the point of CONDITION_FX_OFFSETS of the species.
void PlaySession::showButterfly(bool partnerNormal, const session::GameSession& session) {
  FieldEffects& effects = scene_->fieldEffects();
  const game::partner::PartnerState& partner = session.partner().state();
  if (!partnerNormal || !partner.conditions.only(game::partner::Condition::Sulking)) {
    effects.unsetButterfly();
    return;
  }
  const assets::DigimonRow* row =
      species_ ? species_->find(session.partnerView().species) : nullptr;
  if (row == nullptr || !row->conditionFx) {
    effects.unsetButterfly();
    scene_->setPartnerConditionPoint(std::nullopt);
    return;
  }
  const auto& offset = row->conditionFx->offset;
  scene_->setPartnerConditionPoint(
      std::pair{row->conditionFx->node,
                glm::vec3{static_cast<float>(offset[0]), static_cast<float>(offset[1]),
                          static_cast<float>(offset[2])}});
  effects.setButterfly();
}

core::Result<assets::RaiseTable>
PlaySession::raiseTable(const assets::VirtualFileSystem& files) const {
  return ruleset::loadPatchedRaiseTable(files, game_.session->rules().current());
}

float PlaySession::speciesScale(std::string_view species) const {
  const ruleset::SpeciesExtra extra = speciesExtraOf(*game_.session);
  const ruleset::SpeciesPatch* patch = extra.find(species);
  return patch != nullptr ? patch->fieldScale.value_or(1.0F) : 1.0F;
}

screens::ScreenSettings PlaySession::partnerStatus() const {
  return partnerMenu_.statusSettings();
}

const game::story::StoryState* PlaySession::storyState() const {
  return game_.systems.story != nullptr ? &game_.systems.story->state() : nullptr;
}

bool PlaySession::paused() const {
  const ui::Ui& ui = *context_.ui;
  return ui.isVisible(MenuDocument) || ui.isVisible(MainMenuDocument) ||
         ui.isVisible(PartnerMenuDocument) || screenStops();
}

FieldClock PlaySession::fieldClock() const {
  const ui::Ui& ui = *context_.ui;
  return fieldClockOf({.gameMenu = ui.isVisible(MenuDocument) || ui.isVisible(PartnerMenuDocument),
                       .otherScreen = ui.isVisible(MainMenuDocument) || screenStops()});
}

bool PlaySession::screenStops() const {
  return battleScreen_.active() || (sleepScreen_.active() && !sleepScreen_.fieldRuns()) ||
         saveMachine_.active() || toTitleAsked_ || evolutionScene_.active() ||
         deathScene_.active() || shopScreen_.active() || itemKeeperScreen_.active() ||
         discardScreen_.active() || jukeboxScreen_.active() || tradeScreen_.active() ||
         birdScreen_.active() || cardShopScreen_.active() || meritShopScreen_.active() ||
         fieldItemsScreen_.active() || awardScreen_.active() || scheduleScreen_.active() ||
         bracketScreen_.playing() || storyScenes_.tournamentIntro() || worldMapScreen_.isOpen();
}

float PlaySession::fadeLevel() const {
  return std::max({scene_->field().fadeLevel(), sleepScreen_.fadeLevel(), deathScene_.fadeLevel(),
                   storyScenes_.tournamentFade()});
}

namespace {

std::string_view poseName(game::battle::Pose pose) {
  using game::battle::Pose;
  switch (pose) {
  case Pose::Standing:
    return "standing";
  case Pose::Walking:
    return "walking";
  case Pose::Attacking:
    return "attacking";
  case Pose::Charging:
    return "charging";
  case Pose::HitReaction:
    return "hit";
  case Pose::Blocking:
    return "blocking";
  case Pose::Stunned:
    return "stunned";
  case Pose::Squashed:
    return "squashed";
  case Pose::Disobeying:
    return "disobeying";
  case Pose::Fainting:
    return "fainting";
  case Pose::Fainted:
    return "fainted";
  }
  return "unknown";
}

} // namespace

TraceFrame PlaySession::traceFrame(std::uint64_t tick, const input::InputState& input) const {
  TraceFrame frame;
  frame.tick = tick;
  frame.seconds = static_cast<double>(tick) * TickSeconds;
  constexpr std::array<std::pair<std::string_view, std::uint32_t>, 8> Buttons{{
      {"move_up", trace_buttons::Up},
      {"move_right", trace_buttons::Right},
      {"move_down", trace_buttons::Down},
      {"move_left", trace_buttons::Left},
      {"confirm", trace_buttons::Cross},
      {"cancel", trace_buttons::Circle},
      {"menu", trace_buttons::Triangle},
      {"walk", trace_buttons::Square},
  }};
  for (const auto& [action, bit] : Buttons) {
    if (input.isDown(action)) {
      frame.buttons |= bit;
    }
  }
  const game::field::Field& field = scene_->field();
  frame.map = field.mapId();
  frame.player =
      TraceCharacter{.position = field.player().position, .yaw = field.player().yaw, .id = {}};
  frame.partner =
      TraceCharacter{.position = field.partner().position, .yaw = field.partner().yaw, .id = {}};
  const entt::registry& registry = scene_->world().registry();
  for (const game::field::NpcBody& body : field.npcs().bodies()) {
    const auto& transform = registry.get<scene::Transform>(body.entity);
    frame.npcs.push_back(
        {.position = transform.position, .yaw = transform.yaw, .id = body.scriptId});
  }
  if (const game::battle::Battle* battle = battleScreen_.battle()) {
    TraceBattle traced;
    traced.frame = battle->state().frame;
    traced.rolls = battle->rollState();
    if (context_.ui != nullptr) {
      const ui::UiValue phase = context_.ui->setting(BattlePhaseSetting);
      if (const auto* text = std::get_if<std::string>(&phase)) {
        traced.phase = *text;
      }
    }
    for (const game::battle::Fighter& fighter : battle->state().fighters) {
      traced.fighters.push_back({
          .x = fighter.position.x,
          .z = fighter.position.z,
          .yaw = assets::yawOfDirection(
              {static_cast<float>(fighter.facing.x), 0.0F, static_cast<float>(fighter.facing.z)}),
          .hp = fighter.hp,
          .mp = fighter.mp,
          .maxHp = fighter.stats.maxHp,
          .maxMp = fighter.stats.maxMp,
          .offense = fighter.stats.offense,
          .defense = fighter.stats.defense,
          .speed = fighter.stats.speed,
          .brain = fighter.stats.brain,
          .pose = std::string{poseName(fighter.pose)},
      });
    }
    frame.battle = std::move(traced);
  }
  return frame;
}

} // namespace dwe::app
