#include "versus_arena.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/text_keys.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/screens/name_keys.hpp>

#include <glm/mat4x4.hpp>

#include <algorithm>
#include <utility>

#include "arena_letters.hpp"
#include "arena_lighting.hpp"
#include "arena_view.hpp"
#include "battle_motion.hpp"
#include "field_clock.hpp"

namespace dwe::app {

namespace {

// btl.c: loadBattleData: the partner's species bank is in slot 4 and the rival's in slot 5
// (vs_scene.c: VS_initializeVS loads the two fighters' banks into slots 4 and 5 the same way).
constexpr std::int64_t PartnerSoundSlot = 4;
// The system bank's sounds (playSound(0, n)): vs_intro.c: VS__runIntro's stats count (0x16, as
// STD_tickIntroStats), vs_combat.c: VS__combatSetup's banner (0x10) and its burst (0x11).
constexpr std::int64_t SystemSoundSlot = 0;
constexpr std::int32_t StatsCountSound = 0x16;
constexpr std::int32_t BannerSound = 0x10;
constexpr std::int32_t BurstSound = 0x11;
// VS__combatSetup: the fighters stand in clip 0x21 facing each other while the banner shows, at
// least 60 frames.
constexpr std::uint32_t BannerClip = 0x21;
constexpr std::int32_t VersusBannerFrames = 60;
// VS__deinitializeCombat: the winner plays clip 0x2a, again each time it ends, for 140 frames
// (playMusic(VS_MUSIC, 3)); then 20 frames more without the HUD.
constexpr std::uint32_t WinnerPoseClip = 0x2a;
constexpr std::int32_t WinnerPoseFrames = 140;
constexpr std::int32_t TailFrames = 20;
// VS_playVersusIntroSequence: the series' winner's letters start at frame 0x3c
// (VS_setVersusModelSceneTimer), a drawn series' at -10.
constexpr std::int32_t FinalLettersFrame = 0x3c;
constexpr std::int32_t FinalDrawFrame = -10;
constexpr double ShowFrameSeconds = 1.0 / 20.0;

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

} // namespace

core::Result<std::unique_ptr<VersusArena>> VersusArena::create(VersusArenaDesc desc) {
  if (desc.renderer == nullptr || desc.files == nullptr || desc.arenas.empty()) {
    return core::fail("versus arena: no renderer, no packs or no arena");
  }
  auto clock = std::make_unique<ruleset::GameClock>(desc.clock);
  // The field's start map stands under the arena's stage, hidden with its characters (the
  // tournament's arena is drawn the same way over its cup's map).
  FieldSceneDesc field;
  field.clock = clock.get();
  auto scene = FieldScene::create(*desc.renderer, *desc.files, field);
  if (!scene) {
    return std::unexpected{scene.error()};
  }
  return std::unique_ptr<VersusArena>{
      new VersusArena{std::move(desc), std::move(clock), std::move(*scene)}};
}

VersusArena::VersusArena(VersusArenaDesc desc, std::unique_ptr<ruleset::GameClock> clock,
                         std::unique_ptr<FieldScene> scene)
    : desc_{std::move(desc)}, clock_{std::move(clock)}, scene_{std::move(scene)},
      shotColumns_{readShotColumns(desc_.files)} {
  const assets::DigimonTable* species = desc_.species;
  scene_->attachSpeciesRadius([species](std::string_view id) {
    const auto* row = species != nullptr ? species->find(id) : nullptr;
    return row != nullptr ? static_cast<float>(row->radius) : 0.0F;
  });
  if (desc_.audio != nullptr) {
    sound_.emplace(*desc_.audio, *desc_.files);
    sound_->attachPartner([this] { return drawn_[0]; });
    scene_->attachSound(&*sound_, [species](std::string_view id) {
      const auto* row = species != nullptr ? species->find(id) : nullptr;
      return row != nullptr
                 ? std::optional{FieldScene::SpeciesBox{.radius = static_cast<float>(row->radius),
                                                        .height = static_cast<float>(row->height)}}
                 : std::nullopt;
    });
  }
  if (desc_.audio != nullptr) {
    music_.emplace(*desc_.audio, *desc_.files, session::MusicFacts{});
  }
  if (desc_.ui != nullptr) {
    matchScreen_.emplace(desc_.ui);
  }
  if (auto model = assets::AssetId::parse(arena().model)) {
    scene_->setProps(*model, {glm::mat4{1.0F}});
  } else {
    core::logWarning("versus: the arena {} is not drawn: {}", arena().model, model.error().message);
  }
  showStage(game::tournament::MatchCamera{arena().camera}.view(arena().start.partner,
                                                               arena().start.rival, std::nullopt));
}

VersusArena::~VersusArena() = default;

bool VersusArena::showFighters(const game::battle::BattleState& state) {
  if (state.fighters.size() < 2) {
    return false;
  }
  const std::string& partner = state.fighters[0].combatant.species;
  const std::string& rival = state.fighters[1].combatant.species;
  bool shown = true;
  if (drawn_[0] != partner) {
    if (auto loaded = scene_->showPartnerSpecies(partner); loaded) {
      drawn_[0] = partner;
    } else {
      core::logWarning("versus: the host's fighter {} is not drawn: {}", partner,
                       loaded.error().message);
      shown = false;
    }
  }
  if (drawn_[1] != rival) {
    scene_->clearStageFighters();
    showBattleEffects(*scene_, nullptr, false, effectFrames_);
    showBattleStatus(*scene_, nullptr, desc_.species, enemyNpcs_, &shotColumns_);
    drawn_[1].clear();
    const game::battle::Point at = state.fighters[1].position;
    if (scene_->addStageFighter(RivalFighter, rival,
                                {static_cast<float>(at.x), static_cast<float>(at.z)},
                                assets::yawFromOriginal(arena().rivalYaw))) {
      drawn_[1] = rival;
    } else {
      shown = false;
    }
  }
  return shown;
}

VersusArena::Rig* VersusArena::rigOf(const game::battle::Battle& battle) {
  for (Rig& rig : rigs_) {
    if (rig.battle == &battle) {
      return &rig;
    }
  }
  return nullptr;
}

void VersusArena::prepare(game::battle::Battle& battle) {
  if (!showFighters(battle.state())) {
    core::logWarning("versus: a fighter's model is missing: its battle times its moves without "
                     "it, and the other machine may not");
  }
  Rig* rig = rigOf(battle);
  if (rig == nullptr) {
    rig = &rigs_.emplace_back(Rig{.battle = &battle, .bodies = nullptr, .flights = nullptr});
  }
  // The battle at this address is a new one: it holds none of the last one's flights.
  rig->flights.reset();
  rig->bodies =
      std::make_unique<BattleEffectBodies>(battle, scene_.get(), desc_.species, enemyNpcs_);
  // The fighters move and time their animations by their models' clips, and the techniques fly as
  // their effect scripts do (battle_screen.cpp: BattleScreen::start).
  for (game::battle::FighterId id = 0; id < battle.state().fighters.size(); ++id) {
    if (std::optional<game::battle::FighterClips> clips = rig->bodies->clips(id)) {
      battle.setClips(id, std::move(*clips));
    }
  }
  rig->flights = std::make_unique<game::battle::ScriptedFlights>(
      [files = desc_.files](std::string_view move) {
        return session::readMoveEffect(*files, move);
      },
      *rig->bodies, rig->bodies.get());
  battle.setFlights(rig->flights.get());
}

void VersusArena::battleStarted(game::battle::Battle& battle) {
  prepare(battle);
  shown_ = &battle;
  lastFrame_.reset();
  effects_.reset();
  effectFrames_ = 0;
  if (sound_) {
    // vs_scene.c: VS_initializeVS: VSLoadSounds, then each fighter's species bank.
    sound_->startBattleBank();
    const std::array<std::string, 1> rival{drawn_[1]};
    sound_->useEnemyBanks(rival);
    effects_.emplace(*desc_.files, *sound_);
    if (const Rig* rig = rigOf(battle)) {
      effects_->setBodies(*rig->bodies);
    }
  }
  // vs_combat.c: VS__combatSetup, as the tournament's STD_combatSetup: the arena's camera.
  camera_.emplace(arena().camera, game::tournament::MatchCamera::Kind::Versus);
  attackFrames_ = {};
  lastFrame_ = battle.state().frame;
  letters_.reset();
  drawLetters_.reset();
  endCamera_.reset();
  timeUp_.reset();
  winner_.reset();
  startIntro(battle);
  core::logInfo("versus: the arena shows {} against {}", drawn_[0], drawn_[1]);
}

// STD_setupMoveExecution, as StoryTournament::noteMoveStarts: a fighter that starts a technique
// may take the camera.
void VersusArena::noteMoveStarts(const game::battle::Battle& battle) {
  const game::battle::BattleState& state = battle.state();
  const std::array<game::tournament::FightFigure, 2> figures = fightFiguresOf(battle);
  for (std::size_t i = 0; i < attackFrames_.size() && i < state.fighters.size(); ++i) {
    const game::battle::Fighter& fighter = state.fighters.at(i);
    std::optional<std::int32_t>& last = attackFrames_.at(i);
    if (!fighter.attack) {
      last.reset();
      continue;
    }
    const bool started = !last || fighter.attack->frame < *last;
    last = fighter.attack->frame;
    if (!started || fighter.attack->move == battle.moves().flattenedShot()) {
      continue;
    }
    const std::optional<std::size_t> focused = camera_->focused();
    camera_->moveStarted(i,
                         static_cast<std::int32_t>(battle.moves().at(fighter.attack->move).range),
                         focused ? figures.at(*focused).moveRange : std::nullopt, cameraRolls_);
  }
}

void VersusArena::battleFrame(const game::battle::Battle& battle,
                              const game::battle::BattleEvents& events) {
  if (&battle != shown_ || phase_ != Phase::Fight) {
    return;
  }
  const game::battle::BattleState& state = battle.state();
  if (lastFrame_ && *lastFrame_ == state.frame) {
    // The time-up ends the battle on a step that plays no frame: its end still shows.
    endOnResult(battle);
    return;
  }
  lastFrame_ = state.frame;
  if (effects_) {
    if (const render::MapCamera* camera = scene_->camera()) {
      effects_->setView(effectViewOf(*camera));
    }
    effects_->frame(events, battle.moves());
    ++effectFrames_;
  }
  // BTL_battleTickFrame: tickObjects, the field's effects among them.
  scene_->fieldEffects().tickFrame();
  figures_ = fighterFiguresOf(state, PartnerSoundSlot);
  if (state.fighters.size() > 1) {
    for (std::size_t i = 0; i < 2; ++i) {
      const game::battle::Fighter& fighter = state.fighters.at(i);
      standing_.at(i) = arenaPointOf(fighter.position);
      bodies_.at(i) = {.height = fighter.combatant.height, .radius = fighter.combatant.radius};
      if (state.matchClock) {
        // VS_DAMAGE: the HP each side lost since the battle started.
        const std::int32_t start =
            i == 0 ? state.matchClock->partnerStartHp : state.matchClock->enemyStartHp;
        lost_.at(i) = start - fighter.hp;
      }
    }
    // VS__checkEndCondition: a fighter fell with the other standing: the winner's letters and the
    // end camera (VS_addResultModelScene, the tournament's STD_startCameraIntro).
    const bool hostDown = state.fighters[0].defeated();
    const bool guestDown = state.fighters[1].defeated();
    if (!letters_ && !drawLetters_ && hostDown != guestDown) {
      startLetters(hostDown ? 1 : 0);
    }
  }
  if (letters_) {
    letters_->tick();
  }
  if (endCamera_) {
    endCamera_->tick();
    view_ = endCamera_->view(standing_.at(loserShown()), std::nullopt);
  } else if (camera_ && state.fighters.size() > 1) {
    bool busy = false;
    for (const game::battle::Fighter& fighter : state.fighters) {
      busy = busy || fighter.pose == game::battle::Pose::Attacking ||
             fighter.pose == game::battle::Pose::HitReaction;
    }
    noteMoveStarts(battle);
    camera_->tickFight(busy, cameraRolls_);
    view_ = camera_->view(fightFiguresOf(battle), std::nullopt);
  }
  showStage(view_);
  showBattleEffects(*scene_, effects_ ? &*effects_ : nullptr, !battle.finished(), effectFrames_);
  showBattleStatus(*scene_, &battle, desc_.species, enemyNpcs_, &shotColumns_);
  endOnResult(battle);
}

void VersusArena::endOnResult(const game::battle::Battle& battle) {
  if (battle.finished() && battle.result()) {
    // VS__deinitializeCombat: the battle's end.
    const game::battle::BattleResult& result = *battle.result();
    winner_ =
        result.draw
            ? std::nullopt
            : std::optional<std::size_t>{result.outcome == game::battle::Outcome::Victory ? 0 : 1};
    phase_ = Phase::Ending;
    endFrames_ = 0;
    const bool lettersShown = letters_ || drawLetters_;
    endStep_ = result.timeUp && !lettersShown ? EndStep::TimeUp : EndStep::Letters;
    if (endStep_ == EndStep::Letters && !lettersShown) {
      startLetters(winner_);
    }
  }
}

std::size_t VersusArena::loserShown() const {
  return letters_ && letters_->winner() == 0 ? 1 : 0;
}

void VersusArena::battlesOver() {
  if (shown_ == nullptr) {
    return;
  }
  shown_ = nullptr;
  if (effects_) {
    effects_->end();
  }
  effects_.reset();
  camera_.reset();
  figures_.clear();
  phase_ = Phase::Done;
  intro_.reset();
  banner_.reset();
  timeUp_.reset();
  letters_.reset();
  drawLetters_.reset();
  endCamera_.reset();
  finalLetters_.reset();
  finalDraw_.reset();
  scoring_ = false;
  if (matchScreen_) {
    matchScreen_->hide();
  }
  if (music_) {
    music_->stopSong();
  }
  showBattleEffects(*scene_, nullptr, false, effectFrames_);
  showBattleStatus(*scene_, nullptr, desc_.species, enemyNpcs_, &shotColumns_);
  scene_->releaseFighters();
  drawn_ = {};
  showStage(view_);
}

void VersusArena::useArena(std::int32_t index) {
  const auto wanted = static_cast<std::size_t>(
      std::clamp(index, 0, static_cast<std::int32_t>(desc_.arenas.size()) - 1));
  if (wanted == arenaIndex_) {
    return;
  }
  if (auto model = assets::AssetId::parse(arena().model)) {
    scene_->setProps(*model, {});
  }
  arenaIndex_ = wanted;
  if (auto model = assets::AssetId::parse(arena().model)) {
    scene_->setProps(*model, {glm::mat4{1.0F}});
  }
  view_ = game::tournament::MatchCamera{arena().camera}.view(arena().start.partner,
                                                             arena().start.rival, std::nullopt);
  showStage(view_);
  core::logInfo("versus: the series is on arena {} ({})", arenaIndex_, arena().model);
}

bool VersusArena::preloadArena() {
  while (preloaded_ < desc_.arenas.size()) {
    const std::string& model = desc_.arenas[preloaded_++].model;
    if (model == arena().model) {
      continue;
    }
    if (auto id = assets::AssetId::parse(model)) {
      // Drawn once and hidden again: the scene keeps the loaded model for when it is used.
      scene_->setProps(*id, {glm::mat4{1.0F}});
      scene_->setProps(*id, {});
      return true;
    }
  }
  return false;
}

const session::TournamentAdapter::ArenaStage& VersusArena::arena() const {
  return desc_.arenas.at(arenaIndex_);
}

std::string VersusArena::speciesName(const std::string& id) const {
  const screens::NameKeys names{desc_.species, nullptr, nullptr};
  return localization::tr(names.species(id));
}

bool VersusArena::presenting() const {
  return shown_ != nullptr &&
         (phase_ == Phase::Intro || phase_ == Phase::Banner || phase_ == Phase::Ending);
}

void VersusArena::presentFrame(bool confirm) {
  switch (phase_) {
  case Phase::Intro:
    introFrame(confirm);
    break;
  case Phase::Banner:
    bannerFrame();
    break;
  case Phase::Ending:
    endFrame(confirm);
    break;
  case Phase::Fight:
  case Phase::Done:
    break;
  }
}

// vs_intro.c: VS__runIntro, the tournament's STD_runIntro (the versus places the fighters on every
// arena as on the first: VS__placePlayer1/2): each fighter starts `walk_in` back and runs in.
void VersusArena::startIntro(const game::battle::Battle& battle) {
  const game::battle::BattleState& state = battle.state();
  if (state.fighters.size() < 2) {
    phase_ = Phase::Fight;
    return;
  }
  const std::int32_t walk = arena().walkIn;
  const game::tournament::ArenaPoint partnerStart{.x = arena().start.partner.x + walk,
                                                  .z = arena().start.partner.z};
  const game::tournament::ArenaPoint rivalStart{.x = arena().start.rival.x - walk,
                                                .z = arena().start.rival.z};
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
    // The pose at the end of the run: the clip of its first technique.
    if (fighter.moves.front()) {
      const auto& list = fighter.speciesMoves;
      const auto found = std::ranges::find(list, fighter.moves.front());
      if (found != list.end()) {
        out.poseClip = static_cast<std::uint32_t>(game::battle::FirstMoveAnimation +
                                                  std::distance(list.begin(), found));
      }
    }
    if (const assets::ModelData* model = scene_->fighterModel(npc)) {
      out.clips = battleClipsOf(*model);
    }
    return out;
  };
  const game::battle::Combatant& host = state.fighters[0].combatant;
  const game::battle::Combatant& guest = state.fighters[1].combatant;
  names_ = {speciesName(host.species), speciesName(guest.species)};
  intro_.emplace(introOf(host, names_[0], partnerStart, arena().partnerYaw, walk, std::nullopt),
                 introOf(guest, names_[1], rivalStart, arena().rivalYaw, -walk, RivalFighter));
  phase_ = Phase::Intro;
  introFrame(false);
}

void VersusArena::introFrame(bool confirm) {
  intro_->tick(confirm, {});
  if (const std::optional<std::int32_t> turn = intro_->takeMusic(); turn && arena().music) {
    playSong(*turn == 0 ? arena().music->partnerIntro : arena().music->rivalIntro);
  }
  if (intro_->counting()) {
    playSound(StatsCountSound);
  }
  figures_.clear();
  for (std::size_t i = 0; i < 2; ++i) {
    const game::tournament::MatchIntro::Figure& figure = intro_->figure(i);
    const game::battle::Point ground = battlePointOf(figure.position);
    FighterFigure shown;
    shown.x = static_cast<float>(ground.x);
    shown.z = static_cast<float>(ground.z);
    shown.yaw = assets::yawFromOriginal(figure.yaw);
    shown.clip = figure.clip;
    shown.starts = figure.starts;
    shown.soundSlot = PartnerSoundSlot + static_cast<std::int64_t>(i);
    figures_.push_back(shown);
  }
  view_ = intro_->view();
  showStage(view_);
  if (!intro_->done()) {
    if (matchScreen_) {
      TournamentMatchView shown;
      shown.intro = TournamentMatchView::Intro{
          .name = firstLetters(names_.at(intro_->turn()), intro_->nameShown()),
          .nameY = intro_->nameY(),
          .stats = intro_->stats(),
          .counted = intro_->statsCounted()};
      matchScreen_->show(shown);
    }
    return;
  }
  intro_.reset();
  startBanner();
}

// vs_combat.c: VS__combatMain: playMusic(VS_MUSIC, 2), then VS__combatSetup: the banner over the
// fighters facing each other, the camera's nearest distance out by 400 a frame up to 4200 while it
// shows, back by 400 to 1000 as it bursts.
void VersusArena::startBanner() {
  if (matchScreen_) {
    matchScreen_->hide();
  }
  if (arena().music) {
    playSong(arena().music->fight);
  }
  banner_.emplace(static_cast<std::uint32_t>(cameraRolls_.next()),
                  screens::bannerPlacesOf(localization::tr(game::battle::BannerShapeTextKey)));
  banner_->burstAfter(VersusBannerFrames);
  banner_->skipAnger();
  playSound(BannerSound);
  phase_ = Phase::Banner;
  if (shown_ != nullptr) {
    figures_ = fighterFiguresOf(shown_->state(), PartnerSoundSlot);
    for (FighterFigure& figure : figures_) {
      figure.clip = BannerClip;
      ++figure.starts;
    }
  }
  bannerFrame();
}

void VersusArena::bannerFrame() {
  const screens::IntroStep before = banner_->current();
  banner_->step();
  if (before == screens::IntroStep::Banner && banner_->current() == screens::IntroStep::Burst) {
    playSound(BurstSound);
  }
  if (camera_ && shown_ != nullptr) {
    camera_->tickBanner(banner_->current() == screens::IntroStep::Banner);
    view_ = camera_->view(fightFiguresOf(*shown_), std::nullopt);
    showStage(view_);
  }
  if (banner_->done()) {
    banner_.reset();
    phase_ = Phase::Fight;
  }
}

void VersusArena::startLetters(std::optional<std::size_t> winner) {
  if (!winner) {
    // VS__deinitializeCombat: a draw stops the song and shows the drawn letters.
    drawLetters_.emplace(game::tournament::MatchEnding::Drawn, cameraRolls_);
    if (music_) {
      music_->stopSong();
    }
    core::logInfo("versus: the battle is drawn: its letters");
    return;
  }
  letters_.emplace(*winner);
  const std::size_t loser = 1 - *winner;
  endCamera_.emplace(standing_.at(*winner), bodies_.at(*winner), standing_.at(loser),
                     bodies_.at(loser), cameraRolls_);
  core::logInfo("versus: {} wins the battle: its letters", *winner == 0 ? "1P" : "2P");
}

// vs_combat.c: VS__checkEndCondition (VS__tickBattleResultScreen at time up) and
// VS__deinitializeCombat: the time-up box, then the letters with the winner's pose for 140 frames
// and playMusic(VS_MUSIC, 3) (or the drawn letters until they are in), then 20 frames.
void VersusArena::endFrame(bool confirm) {
  ++endFrames_;
  switch (endStep_) {
  case EndStep::TimeUp:
    timeUpFrame(confirm);
    break;
  case EndStep::Letters:
    if (endFrames_ == 1 && winner_) {
      startWinnerPose();
    }
    if (winner_ && *winner_ < figures_.size()) {
      FighterFigure& figure = figures_.at(*winner_);
      const auto frame = static_cast<std::size_t>(endFrames_ - 1);
      const bool restart =
          figure.clip != WinnerPoseClip || (poseLength_ > 0 && frame % poseLength_ == 0);
      figure.clip = WinnerPoseClip;
      figure.starts += restart ? 1 : 0;
    }
    if (winner_ ? endFrames_ >= WinnerPoseFrames : (!drawLetters_ || drawLetters_->finished())) {
      endStep_ = EndStep::Tail;
      endFrames_ = 0;
    }
    break;
  case EndStep::Tail:
    if (endFrames_ >= TailFrames) {
      phase_ = Phase::Done;
      letters_.reset();
      drawLetters_.reset();
      endCamera_.reset();
    }
    break;
  }
  if (letters_) {
    letters_->tick();
  }
  if (drawLetters_) {
    drawLetters_->tick();
  }
  if (endCamera_) {
    endCamera_->tick();
    view_ = endCamera_->view(standing_.at(loserShown()), std::nullopt);
  }
  // VS_tickFrame: the objects run on through the end, the moves' effects among them.
  if (effects_ && shown_ != nullptr) {
    effects_->frame({}, shown_->moves());
    ++effectFrames_;
    showBattleEffects(*scene_, &*effects_, false, effectFrames_);
  }
  showStage(view_);
}

void VersusArena::timeUpFrame(bool confirm) {
  if (!timeUp_) {
    timeUp_.emplace(!winner_.has_value());
  }
  timeUp_->tick(confirm);
  if (matchScreen_) {
    TournamentMatchView shown;
    shown.timeUp = TournamentMatchView::TimeUp{.box = timeUp_->boxShown(),
                                               .partner = names_[0],
                                               .rival = names_[1],
                                               .dealt = lost_[1],
                                               .taken = lost_[0]};
    matchScreen_->show(shown);
  }
  if (!timeUp_->done()) {
    return;
  }
  timeUp_.reset();
  if (matchScreen_) {
    matchScreen_->hide();
  }
  startLetters(winner_);
  endStep_ = EndStep::Letters;
  endFrames_ = 0;
}

// The winner's song and its clip 0x2a, whose length the model's clips give.
void VersusArena::startWinnerPose() {
  if (arena().music) {
    playSong(arena().music->won);
  }
  poseLength_ = 0;
  const std::optional<std::int32_t> npc =
      *winner_ == 0 ? std::nullopt : std::optional<std::int32_t>{RivalFighter};
  if (const assets::ModelData* model = scene_->fighterModel(npc)) {
    const game::battle::FighterClips clips = battleClipsOf(*model);
    if (const auto found = clips.find(static_cast<std::int32_t>(WinnerPoseClip));
        found != clips.end()) {
      poseLength_ = found->second.length();
    }
  }
}

void VersusArena::showScore(bool shown, bool last, std::optional<std::size_t> winner) {
  scoring_ = shown;
  finalLetters_.reset();
  finalDraw_.reset();
  if (matchScreen_) {
    matchScreen_->hide();
  }
  if (shown) {
    // The score screen hides the arena and its fighters; the next battle shows them again.
    scene_->clearStageFighters();
    drawn_[1].clear();
    figures_.clear();
    letterSeconds_ = 0.0;
  }
  if (shown && last) {
    if (winner) {
      finalLetters_.emplace(*winner);
      finalLetters_->setFrame(FinalLettersFrame);
    } else {
      finalDraw_.emplace(game::tournament::MatchEnding::Drawn, cameraRolls_);
      finalDraw_->setFrame(FinalDrawFrame);
    }
  }
  showStage(view_);
}

void VersusArena::menuSound(std::int32_t sound) {
  playSound(sound);
}

void VersusArena::playSong(std::int32_t track) {
  if (music_ && arena().musicFont) {
    music_->playSong({.font = *arena().musicFont, .track = track});
  }
}

void VersusArena::playSound(std::int32_t sound) {
  if (sound_) {
    sound_->playSound(SystemSoundSlot, sound);
  }
}

void VersusArena::addSettings(screens::ScreenSettings& out) const {
  std::string_view show;
  if (scoring_) {
    show = "score";
  } else if (shown_ != nullptr) {
    switch (phase_) {
    case Phase::Intro:
      show = "intro";
      break;
    case Phase::Banner:
      show = "banner";
      break;
    case Phase::Fight:
      show = "fight";
      break;
    case Phase::Ending:
      show = endStep_ == EndStep::Tail ? "tail" : "ending";
      break;
    case Phase::Done:
      show = "tail";
      break;
    }
  }
  out.push_back({.name = "versus.show", .value = std::string{show}});
  const screens::ScreenSettings banner = screens::settingsOf(
      banner_ ? screens::battleIntroViewOf(*banner_, {}) : screens::BattleIntroView{}, 0);
  out.insert(out.end(), banner.begin(), banner.end());
}

void VersusArena::showLetters(const game::tournament::ShowView& view) {
  const std::array<LetterModel, 2> models{VersusWinModel, DrawLetters};
  std::optional<game::tournament::LetterScene> letters;
  LetterModel shown{};
  if (scoring_ && finalLetters_) {
    letters = finalLetters_->scene();
    shown = VersusWinModel;
  } else if (scoring_ && finalDraw_) {
    letters = finalDraw_->scene();
    shown = DrawLetters;
  } else if (!scoring_ && letters_) {
    letters = letters_->scene();
    shown = VersusWinModel;
  } else if (!scoring_ && drawLetters_) {
    letters = drawLetters_->scene();
    shown = DrawLetters;
  }
  showLetterModel(*scene_, models, shown, letters, view);
}

void VersusArena::showStage(const game::tournament::ShowView& view) {
  FieldStage stage;
  stage.sceneryHidden = true;
  stage.view =
      render::MapCameraDesc{.eye = worldOf(view.eye),
                            .target = worldOf(view.target),
                            .focalLength = static_cast<float>(view.projection),
                            .backgroundSize = render::OriginalScreenSize * ArenaBackgroundScale};
  // Over the score screen only the series' winner's letters show, on black.
  if (!scoring_) {
    stage.shownProps = {arena().model};
  }
  if (scoring_ ? finalLetters_.has_value() : letters_.has_value()) {
    stage.shownProps.emplace_back(VersusWinModel.model);
  }
  if (scoring_ ? finalDraw_.has_value() : drawLetters_.has_value()) {
    stage.shownProps.emplace_back(DrawLetters.model);
  }
  stage.partnerHidden = scoring_;
  stage.ground = arena().collision;
  stage.lighting = arenaLighting();
  scene_->setStage(stage);
  showLetters(view);
}

void VersusArena::tick(double seconds) {
  // The fighters start each tick where they stand (PlaySession::tickSystems does the same).
  scene_->settleActors(seconds);
  if (shown_ == nullptr) {
    return;
  }
  // The series' winner's letters over the last score screen run at the original's 20 frames a
  // second on their own.
  if (scoring_ && (finalLetters_ || finalDraw_)) {
    letterSeconds_ += seconds;
    while (letterSeconds_ >= ShowFrameSeconds) {
      letterSeconds_ -= ShowFrameSeconds;
      if (finalLetters_) {
        finalLetters_->tick();
      }
      if (finalDraw_) {
        finalDraw_->tick();
      }
    }
    showStage(view_);
  }
  for (std::size_t index = 0; index < figures_.size() && index < 2; ++index) {
    const std::optional<std::int32_t> npc =
        index == 0 ? std::nullopt : std::optional<std::int32_t>{RivalFighter};
    scene_->holdFighter(npc, figures_[index]);
  }
  scene_->runFighters(seconds);
}

void VersusArena::update(double frameSeconds, float alpha, const GraphicsOptions& graphics) {
  scene_->setGraphics(graphics);
  scene_->update(frameSeconds, alpha, FieldClock::Frozen);
}

} // namespace dwe::app
