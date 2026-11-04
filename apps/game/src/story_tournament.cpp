#include "story_tournament.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/original_math.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/screens/name_keys.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "battle_motion.hpp"
#include "place_command.hpp"

namespace dwe::app {

namespace {

// std_main.c: STD_placePlayer2: the rival starts across the field from the partner (a cup played
// without its arena).
constexpr std::int32_t RivalDistance = 1000;
// The id the battle holds the rival under (FieldScene::addStageFighter), past any script id.
constexpr std::int32_t RivalFighter = 0x1000;
// The arena's camera centres on its own background, wide enough that no bars show at any window
// size (the original draws the arena over its whole 320 x 240 screen).
constexpr float ArenaBackgroundScale = 100.0F;

game::battle::Point pointOf(glm::vec3 world) {
  return {.x = static_cast<std::int32_t>(std::lround(world.x)),
          .z = static_cast<std::int32_t>(std::lround(world.z))};
}

// The original's z points into the screen, the world's out of it (core::fromOriginalAxes).
game::battle::Point battlePointOf(game::tournament::ArenaPoint point) {
  return {.x = point.x, .z = -point.z};
}

game::tournament::ArenaPoint arenaPointOf(game::battle::Point point) {
  return {.x = point.x, .z = -point.z};
}

glm::vec3 worldOf(const std::array<std::int32_t, 3>& original) {
  return assets::toWorldPosition(glm::ivec3{original[0], original[1], original[2]});
}

void placeCharacter(FieldScene& scene, Placement::Who who, glm::vec2 ground, float yaw) {
  scene.place(Placement{.who = who, .npc = 0, .ground = ground, .yaw = yaw, .clip = {}});
}

// The match's show runs at the original's 20 frames a second (STD_tickFrames).
constexpr double ShowFrameSeconds = 1.0 / 20.0;
// std_main.c: STD_tickIntroStats: playSound(0, 0x16) while the stats count.
constexpr std::int64_t SystemSoundSlot = 0;
constexpr std::int32_t StatsCountSound = 0x16;
// The letters' models (std_hud.c: STD_loadWinLoseModel: STDDAT\WIN_LOSE.TMD, four objects for the
// won match and four for the lost one; STD_loadVersusSceneModel: STDDAT\DRAW.TMD).
constexpr std::string_view WinLoseModel = "model/stddat/win_lose";
constexpr std::size_t WinLoseObjects = 8;
constexpr std::string_view DrawModel = "model/stddat/draw";
constexpr std::size_t DrawObjects = 4;
// std_hud.c: STD_loadChampionModels: the champion's title (STDDAT\CHAMP.TMD, nine objects) and the
// podium of the arena (STDDAT\B_DAI.TMD for B_TOGI's arena, T_DAI.TMD for the other).
constexpr std::string_view ChampionModel = "model/stddat/champ";
constexpr std::size_t ChampionObjects = 9;

std::string podiumOf(std::string_view arenaModel) {
  std::string podium{arenaModel};
  if (const auto at = podium.rfind("togi"); at != std::string::npos) {
    podium.replace(at, 4, "dai");
  }
  return podium;
}
// STD_MODEL_SCENE_MATRIX: the letters' scene stands 1000 in front of the eye.
constexpr float LetterSceneDepth = 1000.0F;
// The letters are drawn this much nearer the eye, as large on screen, so the arena and the
// fighters never hide them (the original draws them on their own ordering table).
constexpr float LetterNear = 0.2F;
constexpr float OriginalTurn = 4096.0F;
constexpr float OriginalOne = 4096.0F;

std::string firstLetters(std::string_view text, std::int32_t count) {
  std::size_t at = 0;
  for (std::int32_t shown = 0; shown < count && at < text.size(); ++shown) {
    ++at;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0U) == 0x80U) {
      ++at;
    }
  }
  return std::string{text.substr(0, at)};
}

std::int32_t letterCount(std::string_view text) {
  return static_cast<std::int32_t>(std::ranges::count_if(
      text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

// The original's rotation of an object (RotMatrix with no z): about y, then x.
glm::mat3 originalRotation(std::int32_t x, std::int32_t y) {
  const float ax = static_cast<float>(x) * 2.0F * std::numbers::pi_v<float> / OriginalTurn;
  const float ay = static_cast<float>(y) * 2.0F * std::numbers::pi_v<float> / OriginalTurn;
  const glm::mat3 rx{1.0F,         0.0F, 0.0F,          0.0F,        std::cos(ax),
                     std::sin(ax), 0.0F, -std::sin(ax), std::cos(ax)};
  const glm::mat3 ry{std::cos(ay), 0.0F,         -std::sin(ay), 0.0F,        1.0F,
                     0.0F,         std::sin(ay), 0.0F,          std::cos(ay)};
  return ry * rx;
}

} // namespace

bool StoryTournament::take(const session::StoryScreenRequest& request, session::StoryScenes& scenes,
                           FieldScene& scene) {
  if (request.screen != session::StoryScreen::Tournament) {
    return false;
  }
  auto started = tournament_ != nullptr && session_ != nullptr
                     ? tournament_->begin(*session_)
                     : core::Result<void>{core::fail("the app has no tournament")};
  if (!started) {
    core::logWarning("[port:tournament] scriptStartTournament (tournament.c:128): {}; the "
                     "tournament is over at once",
                     started.error().message);
    if (request.waits) {
      scenes.screenClosed();
    }
    return true;
  }
  running_ = true;
  matchDue_ = true;
  boardShown_ = false;
  championDue_ = false;
  championNext_ = false;
  enterArena(scene);
  // STD_initializeBracket, STD_addBracketIntro: 120 frames before the first board.
  if (bracket_ != nullptr) {
    bracket_->open();
  }
  return true;
}

void StoryTournament::enterArena(FieldScene& scene) {
  arena_.reset();
  auto arena = tournament_->arenaStage(scene.field().mapId());
  if (!arena) {
    core::logWarning("[port:tournament] STD_initializeTournament (std_setup.c:243): {}; the cup "
                     "is fought on the map",
                     arena.error().message);
    return;
  }
  arena_ = std::move(*arena);
  playerBefore_ = scene.field().player();
  partnerBefore_ = scene.field().partner();
  auto model = assets::AssetId::parse(arena_->model);
  if (model) {
    scene.setProps(*model, {glm::mat4{1.0F}});
  }
  // removeMapEntities, ENTITY_TABLE[0]->isOnScreen = 0: the map's characters are gone and the
  // player hidden; it waits where the partner starts, so the partner does not wander off between
  // the matches.
  const game::battle::Point partner = battlePointOf(arena_->start.partner);
  placeCharacter(scene, Placement::Who::Player,
                 {static_cast<float>(partner.x), static_cast<float>(partner.z)},
                 assets::yawFromOriginal(arena_->partnerYaw));
  placeCharacter(scene, Placement::Who::Partner,
                 {static_cast<float>(partner.x), static_cast<float>(partner.z)},
                 assets::yawFromOriginal(arena_->partnerYaw));
  // STD_initializeTournament: STD_initializeCamera, then STD_setVSPhase(10): the arena turns.
  turnCamera_ = {};
  showArena(scene, turnCamera_.view());
}

void StoryTournament::showArena(FieldScene& scene, game::tournament::ArenaPoint partner,
                                game::tournament::ArenaPoint rival) {
  const game::tournament::ArenaView view =
      game::tournament::arenaView(arena_->camera, partner, rival);
  FieldStage stage;
  stage.sceneryHidden = true;
  stage.view =
      render::MapCameraDesc{.eye = worldOf(view.eye),
                            .target = worldOf(view.target),
                            .focalLength = static_cast<float>(arena_->camera.projection),
                            .backgroundSize = render::OriginalScreenSize * ArenaBackgroundScale};
  stage.shownProps = {arena_->model};
  stage.ground = arena_->collision;
  // STD_addBracket, STD_addBracketIntro: the partner is off the map while the board shows.
  stage.partnerHidden = bracket_ != nullptr && bracket_->playing();
  scene.setStage(stage);
}

void StoryTournament::showArena(FieldScene& scene, const game::tournament::ShowView& view) {
  FieldStage stage;
  stage.sceneryHidden = true;
  stage.view =
      render::MapCameraDesc{.eye = worldOf(view.eye),
                            .target = worldOf(view.target),
                            .focalLength = static_cast<float>(view.projection),
                            .backgroundSize = render::OriginalScreenSize * ArenaBackgroundScale};
  stage.shownProps = {arena_->model};
  if (letters_) {
    stage.shownProps.push_back(lettersModel_);
  }
  if (champion_) {
    stage.shownProps.push_back(podiumModel_);
    stage.shownProps.emplace_back(ChampionModel);
  }
  stage.ground = arena_->collision;
  stage.partnerHidden = bracket_ != nullptr && bracket_->playing();
  scene.setStage(stage);
  showLetters(scene, view);
}

void StoryTournament::leaveArena(FieldScene& scene) {
  scene.clearStageFighters();
  if (!arena_) {
    return;
  }
  scene.setStage({});
  if (auto model = assets::AssetId::parse(arena_->model)) {
    scene.setProps(*model, {});
  }
  // STD_deinitializeTournament, reinitializeAfterTournament: the map as it was.
  placeCharacter(scene, Placement::Who::Player,
                 {playerBefore_.position.x, playerBefore_.position.z}, playerBefore_.yaw);
  placeCharacter(scene, Placement::Who::Partner,
                 {partnerBefore_.position.x, partnerBefore_.position.z}, partnerBefore_.yaw);
  arena_.reset();
}

void StoryTournament::matchOver(game::battle::Outcome outcome) {
  matchRunning_ = false;
  matchDue_ = tournament_->matchOver(*session_, outcome);
  boardShown_ = false;
  const bool won = outcome == game::battle::Outcome::Victory;
  if (won) {
    // STD_tournamentMain after a won match: STD_initializeCamera, STD_CAMERA_STATE 10.
    turnCamera_ = {};
  }
  if (bracket_ != nullptr && bracket_->opened()) {
    bracket_->matchOver(won);
    // STD_tournamentMain: after the final won, STD_addBracket(2).
    championDue_ = won && !matchDue_;
  }
}

void StoryTournament::update(session::StoryScenes& scenes, BattleScreen& battle,
                             FieldScene& scene) {
  if (running_ && arena_ && !camera_) {
    // STD_setCameraOrbit follows the fighters while a match runs; between them it stays.
    if (const game::battle::Battle* fight = battle.battle();
        fight != nullptr && fight->state().fighters.size() > 1) {
      showArena(scene, arenaPointOf(fight->state().partner().position),
                arenaPointOf(fight->state().fighters.at(1).position));
    }
  }
  if (champion_) {
    // STD_removePodiumRenderer, STD_removeChampionScene, STD_deinitializeTournament.
    if (champion_->done()) {
      end(scenes, scene);
    }
    return;
  }
  if (intro_) {
    // STD_runMatch: the fight once the intro is over.
    if (intro_->done()) {
      intro_.reset();
      startMatch(scenes, battle, scene);
    }
    return;
  }
  if (!running_ || matchRunning_ || battle.active()) {
    return;
  }
  // removeMapEntities after each match: the beaten (or winning) rival leaves the arena;
  // STD_removeWinScene and STD_removeLoseScene after STD_runMatch.
  clearShow(scene);
  scene.clearStageFighters();
  if (bracket_ != nullptr && bracket_->playing()) {
    if (arena_) {
      showArena(scene, turnCamera_.view());
    }
    return;
  }
  const bool board = bracket_ != nullptr && bracket_->opened();
  if (!matchDue_) {
    if (board && championDue_) {
      championDue_ = false;
      championNext_ = true;
      bracket_->show(game::tournament::BracketShow::Champion);
      return;
    }
    if (std::exchange(championNext_, false) && startChampion()) {
      return;
    }
    end(scenes, scene);
    return;
  }
  if (board && !boardShown_) {
    boardShown_ = true;
    bracket_->show(game::tournament::BracketShow::Match);
    return;
  }
  const game::field::Field& field = scene.field();
  game::battle::Point partner = pointOf(field.partner().position);
  game::battle::Point rival{.x = partner.x - RivalDistance, .z = partner.z};
  game::battle::Point tamer = pointOf(field.player().position);
  if (arena_) {
    // STD_runIntro ends with the fighters where STD_setPostIntroPosition puts them.
    partner = battlePointOf(arena_->start.partner);
    rival = battlePointOf(arena_->start.rival);
    tamer = partner;
    showArena(scene, arena_->start.partner, arena_->start.rival);
  }
  auto request = tournament_->matchRequest(*session_, partner, tamer, field.mapId(), rival);
  if (!request) {
    core::logWarning("[port:tournament] STD_runMatch (std_setup.c:401): {}; the tournament ends",
                     request.error().message);
    end(scenes, scene);
    return;
  }
  matchDue_ = false;
  if (startIntro(std::move(*request), scene)) {
    return;
  }
  startMatch(scenes, battle, scene);
}

bool StoryTournament::startIntro(session::BattleRequest request, FieldScene& scene) {
  request_ = std::move(request);
  if (!arena_ || matchScreen_ == nullptr || battleAdapter_ == nullptr ||
      request_->enemies.empty()) {
    return false;
  }
  auto partner = battleAdapter_->partnerCombatant(*session_);
  if (!partner) {
    core::logWarning("[port:tournament] STD_runIntro (std_setup.c:263): {}; no intro",
                     partner.error().message);
    return false;
  }
  const game::battle::Combatant& rival = request_->enemies.front();
  // STD_runIntro: each fighter starts `dist` back from where the match starts and runs in.
  const std::int32_t walk = arena_->walkIn;
  const game::tournament::ArenaPoint partnerStart{.x = arena_->start.partner.x + walk,
                                                  .z = arena_->start.partner.z};
  const game::tournament::ArenaPoint rivalStart{.x = arena_->start.rival.x - walk,
                                                .z = arena_->start.rival.z};
  const game::battle::Point rivalGround = battlePointOf(rivalStart);
  if (!scene.addStageFighter(RivalFighter, rival.species,
                             {static_cast<float>(rivalGround.x), static_cast<float>(rivalGround.z)},
                             assets::yawFromOriginal(arena_->rivalYaw))) {
    return false;
  }
  partnerName_ = speciesName(partner->species);
  rivalName_ = speciesName(rival.species);
  const auto introOf = [&](const game::battle::Combatant& fighter, const std::string& name,
                           game::tournament::ArenaPoint start, std::int32_t yaw,
                           std::int32_t walkIn, std::optional<std::int32_t> npc) {
    game::tournament::IntroFighter out{.start = start,
                                       .yaw = yaw,
                                       .walkIn = walkIn,
                                       .body = {.height = fighter.height, .radius = fighter.radius},
                                       .nameLength = letterCount(name),
                                       .stats = {fighter.hp, fighter.mp, fighter.stats.offense,
                                                 fighter.stats.defense, fighter.stats.speed,
                                                 fighter.stats.brain},
                                       .poseClip = std::nullopt,
                                       .clips = {}};
    // The pose at the end of the run: the clip of its first technique (stats.base.moves[0]).
    if (fighter.moves.front()) {
      const auto& list = fighter.speciesMoves;
      const auto found = std::ranges::find(list, fighter.moves.front());
      if (found != list.end()) {
        out.poseClip = static_cast<std::uint32_t>(game::battle::FirstMoveAnimation +
                                                  std::distance(list.begin(), found));
      }
    }
    if (const assets::ModelData* model = scene.fighterModel(npc)) {
      out.clips = battleClipsOf(*model);
    }
    return out;
  };
  intro_.emplace(
      introOf(*partner, partnerName_, partnerStart, arena_->partnerYaw, walk, std::nullopt),
      introOf(rival, rivalName_, rivalStart, arena_->rivalYaw, -walk, RivalFighter));
  frameSeconds_ = 0.0;
  confirm_ = false;
  core::logInfo("[port:tournament] STD_runIntro (std_setup.c:263): {} against {}", partnerName_,
                rivalName_);
  return true;
}

void StoryTournament::startMatch(session::StoryScenes& scenes, BattleScreen& battle,
                                 FieldScene& scene) {
  if (!request_) {
    end(scenes, scene);
    return;
  }
  session::BattleRequest request = std::move(*request_);
  request_.reset();
  if (arena_) {
    showArena(scene, arena_->start.partner, arena_->start.rival);
  }
  // STD_initializeOpponent's entity, placed by STD_placePlayer2: the battle holds it as it holds a
  // map's NPC (the intro added it already).
  std::vector<std::int32_t> fighters;
  const game::battle::Point rival =
      request.enemies.empty() ? game::battle::Point{} : request.enemies.front().position;
  if (arena_ && !request.enemies.empty() &&
      (scene.fighterModel(RivalFighter) != nullptr ||
       scene.addStageFighter(RivalFighter, request.enemies.front().species,
                             {static_cast<float>(rival.x), static_cast<float>(rival.z)},
                             assets::yawFromOriginal(arena_->rivalYaw)))) {
    fighters.push_back(RivalFighter);
  }
  auto started = battle.start(std::move(request), std::move(fighters), true);
  if (!started) {
    core::logWarning("[port:tournament] STD_runMatch (std_setup.c:401): {}; the tournament ends",
                     started.error().message);
    end(scenes, scene);
    return;
  }
  matchRunning_ = true;
  // STD_combatSetup: the cup's camera, its nearest distance at 200 as the banner opens.
  if (arena_ && matchScreen_ != nullptr) {
    camera_.emplace(arena_->camera);
  }
  seen_.reset();
  attackFrames_ = {};
  timeUpDone_ = false;
}

void StoryTournament::tick(double seconds, BattleScreen& battle, FieldScene& scene) {
  if (running_ && arena_ && !intro_ && !camera_ && !champion_ && !battle.active()) {
    // STD_applyRotatingCamera between the matches, 2 a frame.
    confirm_ = false;
    frameSeconds_ += seconds;
    bool turned = false;
    while (frameSeconds_ >= ShowFrameSeconds) {
      frameSeconds_ -= ShowFrameSeconds;
      turnCamera_.tick();
      turned = true;
    }
    if (turned) {
      showArena(scene, turnCamera_.view());
    }
    return;
  }
  if (!running_ || (!intro_ && !camera_ && !champion_)) {
    confirm_ = false;
    return;
  }
  frameSeconds_ += seconds;
  while (frameSeconds_ >= ShowFrameSeconds) {
    frameSeconds_ -= ShowFrameSeconds;
    frame(battle, scene, std::exchange(confirm_, false));
  }
}

void StoryTournament::frame(BattleScreen& battle, FieldScene& scene, bool confirm) {
  using game::tournament::MatchEnding;
  if (champion_) {
    if (!champion_->done()) {
      championFrame(scene);
    }
    return;
  }
  TournamentMatchView shown;
  if (intro_) {
    if (intro_->done()) {
      return;
    }
    intro_->tick(confirm, {});
    if (const std::optional<std::int32_t> track = intro_->takeMusic()) {
      core::logDebug("[port:tournament] STD_runIntro (std_setup.c:263): STD_MUSIC track {}",
                     *track);
    }
    if (intro_->counting() && session_ != nullptr && session_->sound() != nullptr) {
      session_->sound()->playSound(SystemSoundSlot, StatsCountSound);
    }
    for (std::size_t i = 0; i < 2; ++i) {
      const game::tournament::MatchIntro::Figure& figure = intro_->figure(i);
      const game::battle::Point ground = battlePointOf(figure.position);
      scene.holdFighter(i == 0 ? std::nullopt : std::optional{RivalFighter},
                        FighterFigure{.x = static_cast<float>(ground.x),
                                      .z = static_cast<float>(ground.z),
                                      .yaw = assets::yawFromOriginal(figure.yaw),
                                      .clip = figure.clip,
                                      .starts = figure.starts});
    }
    scene.runFighters(ShowFrameSeconds);
    view_ = intro_->view();
    if (!intro_->done()) {
      const std::string& name = intro_->turn() == 0 ? partnerName_ : rivalName_;
      shown.intro = TournamentMatchView::Intro{.name = firstLetters(name, intro_->nameShown()),
                                               .nameY = intro_->nameY(),
                                               .stats = intro_->stats(),
                                               .counted = intro_->statsCounted()};
    }
  } else {
    if (const game::battle::Battle* fight = battle.battle();
        fight != nullptr && fight->state().fighters.size() > 1) {
      watchFight(*fight);
      // tickPartnerAI, tickEnemyAI: a fighter fell with the other standing: selectRandomCamera
      // (mode 5), STD_addWinScene(1) or STD_addLoseScene, STD_startCameraIntro.
      if (!letters_ && (seen_->partnerDown || seen_->rivalDown)) {
        startLetters(seen_->rivalDown && !seen_->partnerDown ? MatchEnding::Won
                                                             : MatchEnding::Lost);
      }
      if (camera_) {
        if (battle.introShowing()) {
          camera_->tickBanner(true);
        } else {
          bool busy = false;
          for (const game::battle::Fighter& fighter : fight->state().fighters) {
            busy = busy || fighter.pose == game::battle::Pose::Attacking ||
                   fighter.pose == game::battle::Pose::HitReaction;
          }
          noteMoveStarts(*fight);
          camera_->tickFight(busy, random_);
        }
      }
    } else if (battle.matchEnding() && seen_ && !timeUpDone_ && seen_->seconds == 0 &&
               !seen_->partnerDown && !seen_->rivalDown) {
      // STD_tickBattleResultScreen: the time ran out with both standing.
      const bool draw = seen_->partnerLost == seen_->rivalLost;
      if (!timeUp_) {
        timeUp_.emplace(draw);
        battle.holdMatchEnd(true);
        core::logInfo("[port:tournament] STD_tickBattleResultScreen (std_main.c:4690): time up, "
                      "damage dealt {} taken {}",
                      seen_->rivalLost, seen_->partnerLost);
      }
      timeUp_->tick(confirm);
      if (timeUp_->done()) {
        timeUpDone_ = true;
        timeUp_.reset();
        startLetters(
            draw ? MatchEnding::Drawn
                 : (seen_->partnerLost < seen_->rivalLost ? MatchEnding::Won : MatchEnding::Lost));
      }
    }
    if (timeUpDone_ &&
        (!letters_ || letters_->ending() != MatchEnding::Drawn || letters_->finished())) {
      battle.holdMatchEnd(false);
    }
    if (letters_) {
      letters_->tick();
    }
    if (endCamera_) {
      endCamera_->tick();
      view_ = endCamera_->view(seen_->rival, std::nullopt);
    } else if (camera_ && seen_) {
      view_ = camera_->view(seen_->figures, std::nullopt);
    }
    if (timeUp_) {
      shown.timeUp = TournamentMatchView::TimeUp{.box = timeUp_->boxShown(),
                                                 .partner = partnerName_,
                                                 .rival = rivalName_,
                                                 .dealt = seen_->rivalLost,
                                                 .taken = seen_->partnerLost};
    }
    if (letters_ && letters_->ending() == MatchEnding::Lost) {
      shown.wipe = letters_->scene().wipe;
    }
  }
  if (view_) {
    showArena(scene, *view_);
  }
  if (matchScreen_ != nullptr) {
    if (shown.intro || shown.timeUp || !shown.wipe.empty()) {
      matchScreen_->show(shown);
    } else {
      matchScreen_->hide();
    }
  }
}

void StoryTournament::watchFight(const game::battle::Battle& fight) {
  const game::battle::BattleState& state = fight.state();
  const game::battle::Fighter& partner = state.partner();
  const game::battle::Fighter& rival = state.fighters.at(1);
  Seen seen{.partner = arenaPointOf(partner.position),
            .rival = arenaPointOf(rival.position),
            .partnerBody = {.height = partner.combatant.height, .radius = partner.combatant.radius},
            .rivalBody = {.height = rival.combatant.height, .radius = rival.combatant.radius},
            .partnerLost = 0,
            .rivalLost = 0,
            .seconds = -1,
            .partnerDown = partner.defeated(),
            .rivalDown = rival.defeated()};
  const game::battle::MoveCatalog& moves = fight.moves();
  for (std::size_t i = 0; i < seen.figures.size(); ++i) {
    const game::battle::Fighter& fighter = state.fighters.at(i);
    game::tournament::FightFigure& figure = seen.figures.at(i);
    figure.at = arenaPointOf(fighter.position);
    // The facing is (sin, -cos) of the original's yaw in world axes.
    figure.yaw = core::original::atan4096(fighter.facing.x, -fighter.facing.z) & 0xfff;
    figure.moveRange.reset();
    if (fighter.attack) {
      figure.moveRange = static_cast<std::int32_t>(moves.at(fighter.attack->move).range);
    }
  }
  if (state.matchClock) {
    // STD_DAMAGE: the HP each side lost since the match started.
    seen.partnerLost = state.matchClock->partnerStartHp - partner.hp;
    seen.rivalLost = state.matchClock->enemyStartHp - rival.hp;
    seen.seconds = state.matchClock->seconds;
  }
  seen_ = seen;
}

void StoryTournament::noteMoveStarts(const game::battle::Battle& fight) {
  const game::battle::BattleState& state = fight.state();
  for (std::size_t i = 0; i < attackFrames_.size(); ++i) {
    const game::battle::Fighter& fighter = state.fighters.at(i);
    std::optional<std::int32_t>& last = attackFrames_.at(i);
    if (!fighter.attack) {
      last.reset();
      continue;
    }
    const bool started = !last || fighter.attack->frame < *last;
    last = fighter.attack->frame;
    // The flattened fighter's shot has no technique camera (STD_startQueuedMove skips
    // STD_setupMoveExecution).
    if (!started || fighter.attack->move == fight.moves().flattenedShot()) {
      continue;
    }
    const std::optional<std::size_t> focused = camera_->focused();
    std::optional<std::int32_t> focusedRange;
    if (focused && seen_) {
      focusedRange = seen_->figures.at(*focused).moveRange;
    }
    camera_->moveStarted(i, static_cast<std::int32_t>(fight.moves().at(fighter.attack->move).range),
                         focusedRange, random_);
  }
}

void StoryTournament::startLetters(game::tournament::MatchEnding ending) {
  letters_.emplace(ending, random_);
  lettersModel_ =
      std::string{ending == game::tournament::MatchEnding::Drawn ? DrawModel : WinLoseModel};
  if (ending != game::tournament::MatchEnding::Drawn && seen_) {
    endCamera_.emplace(seen_->partner, seen_->partnerBody, seen_->rival, seen_->rivalBody, random_);
  }
  core::logInfo("[port:tournament] {}: the match's letters",
                ending == game::tournament::MatchEnding::Won    ? "STD_addWinScene (std_hud.c)"
                : ending == game::tournament::MatchEnding::Lost ? "STD_addLoseScene (std_hud.c)"
                                                                : "STD_addVersusModelScene");
}

void StoryTournament::showLetters(FieldScene& scene, const game::tournament::ShowView& view) {
  // Which letters show: a match's end's or the champion's title; the others' models pose nothing.
  std::optional<game::tournament::LetterScene> letters;
  std::string_view shownModel;
  std::size_t objects = 0;
  if (champion_ && champion_->podiumShown()) {
    letters = champion_->letters();
    shownModel = ChampionModel;
    objects = ChampionObjects;
  } else if (letters_) {
    letters = letters_->scene();
    shownModel = lettersModel_;
    objects = lettersModel_ == DrawModel ? DrawObjects : WinLoseObjects;
  }
  for (const std::string_view model : {WinLoseModel, DrawModel, ChampionModel}) {
    if (model == shownModel) {
      continue;
    }
    if (auto id = assets::AssetId::parse(model)) {
      scene.setPosedProps(*id, {});
    }
  }
  if (!letters) {
    return;
  }
  const auto id = assets::AssetId::parse(shownModel);
  if (!id) {
    return;
  }
  // The eye's axes in the world: the letters' scene is the eye's own space (x right, y down, z
  // ahead), 1000 ahead (STD_MODEL_SCENE_MATRIX), seen with the scene's projection.
  const glm::vec3 eye = worldOf(view.eye);
  const glm::vec3 ahead = glm::normalize(worldOf(view.target) - eye);
  const glm::vec3 right = glm::normalize(glm::cross(ahead, glm::vec3{0.0F, 1.0F, 0.0F}));
  const glm::vec3 up = glm::cross(right, ahead);
  const glm::mat3 eyeAxes{right, -up, ahead};
  const glm::mat3 toOriginal{1.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, -1.0F};
  const float depthScale = letters->projection > 0 ? static_cast<float>(view.projection) /
                                                         static_cast<float>(letters->projection)
                                                   : 1.0F;
  std::vector<glm::mat4> joints(objects, glm::mat4{0.0F});
  for (const game::tournament::LetterPose& letter : letters->letters) {
    if (letter.object >= objects) {
      continue;
    }
    const glm::vec3 at{static_cast<float>(letter.location[0]),
                       static_cast<float>(letter.location[1]),
                       (static_cast<float>(letter.location[2]) + LetterSceneDepth) * depthScale};
    const glm::mat3 scale{1.0F, 0.0F, 0.0F, 0.0F, static_cast<float>(letter.scaleY) / OriginalOne,
                          0.0F, 0.0F, 0.0F, 1.0F};
    const glm::mat3 turn = eyeAxes * originalRotation(letter.rotation[0], letter.rotation[1]) *
                           scale * toOriginal * LetterNear;
    glm::mat4 joint{turn};
    joint[3] = glm::vec4{eye + (eyeAxes * at * LetterNear), 1.0F};
    joints.at(letter.object) = joint;
  }
  scene.setPosedProps(*id, {FieldScene::PropPlace{.world = glm::mat4{1.0F},
                                                  .joints = std::move(joints),
                                                  .tint = glm::vec4{1.0F}}});
}

bool StoryTournament::startChampion() {
  if (!arena_ || matchScreen_ == nullptr || battleAdapter_ == nullptr) {
    return false;
  }
  auto partner = battleAdapter_->partnerCombatant(*session_);
  if (!partner) {
    core::logWarning("[port:tournament] STD_initializeChampionScene (std_hud.c:1372): {}; no "
                     "champion scene",
                     partner.error().message);
    return false;
  }
  champion_.emplace(
      game::tournament::ShowBody{.height = partner->height, .radius = partner->radius});
  podiumModel_ = podiumOf(arena_->model);
  championFigure_ = {};
  frameSeconds_ = 0.0;
  core::logInfo("[port:tournament] STD_tournamentMain (std_setup.c:584): the champion scene, "
                "podium {}",
                podiumModel_);
  return true;
}

void StoryTournament::championFrame(FieldScene& scene) {
  const bool wasShown = champion_->podiumShown();
  champion_->tick(wasShown && scene.fighterPlayedThrough(std::nullopt, championFigure_));
  if (!champion_->podiumShown()) {
    return;
  }
  if (!wasShown && bracket_ != nullptr) {
    // STD_removeBracket(2) once the screen is black, then STD_addPodiumRenderer.
    bracket_->close();
  }
  const game::tournament::ChampionShow::Partner partner = champion_->partner();
  championFigure_ = FighterFigure{.x = 0.0F,
                                  .z = 0.0F,
                                  .yaw = assets::yawFromOriginal(partner.yaw),
                                  .clip = partner.clip,
                                  .starts = partner.starts,
                                  .rise = static_cast<float>(-partner.y)};
  scene.holdFighter(std::nullopt, championFigure_);
  scene.runFighters(ShowFrameSeconds);
  showArena(scene, champion_->view());
  if (auto podium = assets::AssetId::parse(podiumModel_)) {
    const glm::vec3 at = worldOf({0, champion_->podiumY(), 0});
    scene.setPosedProps(*podium,
                        {FieldScene::PropPlace{.world = glm::mat4{1.0F},
                                               .joints = {glm::translate(glm::mat4{1.0F}, at)},
                                               .tint = glm::vec4{1.0F}}});
  }
}

void StoryTournament::clearShow(FieldScene& scene) {
  if (!letters_ && !endCamera_ && !camera_ && !timeUp_ && !view_) {
    return;
  }
  letters_.reset();
  endCamera_.reset();
  camera_.reset();
  timeUp_.reset();
  view_.reset();
  seen_.reset();
  timeUpDone_ = false;
  showLetters(scene, {});
  if (matchScreen_ != nullptr) {
    matchScreen_->hide();
  }
}

std::string StoryTournament::speciesName(const std::string& id) const {
  const screens::NameKeys names{species_, nullptr, nullptr};
  return localization::tr(names.species(id));
}

void StoryTournament::end(session::StoryScenes& scenes, FieldScene& scene) {
  running_ = false;
  matchDue_ = false;
  championNext_ = false;
  intro_.reset();
  if (champion_) {
    champion_.reset();
    if (auto podium = assets::AssetId::parse(podiumModel_)) {
      scene.setPosedProps(*podium, {});
    }
    showLetters(scene, {});
    scene.releaseFighters();
  }
  request_.reset();
  clearShow(scene);
  if (bracket_ != nullptr) {
    bracket_->close();
  }
  leaveArena(scene);
  if (auto finished = tournament_->finish(*session_); !finished) {
    core::logWarning("[port:tournament] startTournament (utils2.c:662): {}",
                     finished.error().message);
  }
  scenes.screenClosed();
}

} // namespace dwe::app
