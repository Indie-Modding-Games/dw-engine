#include "battle_screen.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/battle/text_keys.hpp>
#include <dwe/game/inventory/use.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/screens/bag_view.hpp>

#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "bag_slots.hpp"
#include "field_scene.hpp"
#include "messages.hpp"
#include "partner_menu.hpp"

namespace dwe::app {
namespace {

// What the battle's camera shows, for the effects: the window's size in original pixels (a screen
// layer covers what it shows past the original's 320 x 240), the camera's world-to-view transform
// in the console's axes, F V F and F t with F = diag(1, -1, -1) (GsWSMATRIX), its projection
// distance and where its projection's centre is on screen.
game::battle::EffectView effectViewOf(const render::MapCamera& camera) {
  constexpr float MatrixOne = 4096.0F;
  constexpr std::array<float, 3> Flip{1.0F, -1.0F, -1.0F};
  game::battle::EffectView view;
  const glm::vec2 shown = glm::ceil(camera.visibleSize());
  view.screen = {.x = static_cast<std::int32_t>(shown.x), .y = static_cast<std::int32_t>(shown.y)};
  const glm::mat4& worldToView = camera.view();
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      // glm is column-major: worldToView[column][row].
      const float value =
          Flip.at(row) * Flip.at(column) *
          worldToView[static_cast<glm::length_t>(column)][static_cast<glm::length_t>(row)];
      view.camera.m.at(row).at(column) = static_cast<std::int32_t>(std::lround(value * MatrixOne));
    }
  }
  // glm::mat4's last column is the translation.
  const glm::vec4 translation = worldToView[3];
  view.camera.t = {.x = static_cast<std::int32_t>(std::lround(Flip[0] * translation.x)),
                   .y = static_cast<std::int32_t>(std::lround(Flip[1] * translation.y)),
                   .z = static_cast<std::int32_t>(std::lround(Flip[2] * translation.z))};
  view.projection = static_cast<std::int32_t>(std::lround(camera.desc().focalLength));
  // The projection's centre is the background's: where it is from the middle of what is shown.
  const glm::vec2 centre =
      (camera.desc().backgroundSize * 0.5F) - (camera.scroll() + (camera.visibleSize() * 0.5F));
  view.centre = {.x = static_cast<std::int32_t>(std::lround(centre.x)),
                 .y = static_cast<std::int32_t>(std::lround(centre.y))};
  return view;
}

// The original's battle runs at 20 frames a second.
constexpr double IntroFrameSeconds = 1.0 / 20.0;
// battle_setup.c: BTL_handleBattleIntro: the banner's sounds, in slot 0.
constexpr std::int64_t BannerSound = 0x10;
constexpr std::int64_t BurstSound = 0x11;
// battle_main.c: BTL_handleVictorySequence: the fanfare as the victory starts, then, at least 0x29
// battle frames on (while the map's sounds load), the sound of the victory poses; both in slot 0.
constexpr std::int64_t VictorySound = 0x15;
constexpr std::int64_t VictoryPoseSound = 0x18;
constexpr int VictoryPoseFrames = 0x29;
constexpr double VictoryPoseSeconds = VictoryPoseFrames * IntroFrameSeconds;
// battle_main.c: BTL_handleVictorySequence: with that sound the player and the partner strike their
// poses (startAnimation), and the end box opens once both clips have played through.
constexpr std::uint32_t PlayerPoseClip = 0xb;
constexpr std::uint32_t PartnerPoseClip = 0x2a;
// std_main.c: STD_deinitializeCombat: a tournament match has no end box. The winner strikes its
// pose (startAnimation 0x2a, again each time it plays through) for 100 frames when the partner won,
// 120 when the rival did, then 20 frames pass and the match is over.
constexpr int MatchWonFrames = 0x64;
constexpr int MatchLostFrames = 0x78;
constexpr int MatchAfterFrames = 0x14;
// Before the fanfare the partner takes its stance, the healthy one whatever its HP.
constexpr std::uint32_t VictoryStanceClip = 0x21;
// The port's safeguard, not the original's: a pose that never plays through holds the box no longer
// than this many battle frames.
constexpr int PoseGuardFrames = 200;
// battle_main.c: BTL_renderEnemyHPBars points each bar at the enemy's model node 1
// (getEntityScreenPos(entity, 1)).
constexpr std::uint32_t HpBarNode = 1;
// command_shout.c: BTL_drawCommandShout hangs the shout where the player's node 4 shows
// (getEntityScreenPos(ENTITY_TABLE[0], 4)).
constexpr std::uint32_t ShoutNode = 4;
constexpr double PoseGuardSeconds = PoseGuardFrames * IntroFrameSeconds;
// battle_ui.c: tickBitBox and renderPostBattleStatsBox: the end box's count, a sound for each point
// and bit, and another once the bits are all in; both in slot 0.
constexpr std::int64_t EndCountSound = 0x16;
constexpr std::int64_t EndCountedSound = 0x17;
// btl.c: loadBattleData: the partner's species bank is in slot 4 and enemy i's in slot 4 + i.
constexpr std::int64_t PartnerSoundSlot = 4;

} // namespace

core::Result<void> BattleScreen::start(session::BattleRequest request,
                                       std::vector<std::int32_t> enemyNpcs, bool fixedFight) {
  if (active()) {
    return core::fail(std::string{messages::BattleAlreadyRunning});
  }
  std::string enemies;
  for (const game::battle::Combatant& enemy : request.enemies) {
    enemies +=
        enemies.empty() ? enemy.species : std::string{messages::ListSeparator} + enemy.species;
  }
  const std::string map = request.map;
  std::vector<game::battle::Combatant> introEnemies = request.enemies;
  const session::BattleMusicFacts music = musicFactsOf(request);
  auto battle = desc_.battle->start(*desc_.session, std::move(request));
  if (!battle) {
    return std::unexpected{battle.error()};
  }
  battle_.emplace(std::move(*battle));
  partnerBeforeFrame_.reset();
  effects_.reset();
  enemyNpcs_ = std::move(enemyNpcs);
  // The ground also knows where the field draws the fighters' models (BTL_tickEnemyAI projects an
  // enemy's node 1).
  ground_.reset();
  if (desc_.ground != nullptr && desc_.scene != nullptr) {
    ground_.emplace(*desc_.ground, *desc_.scene, enemyNpcs_);
  }
  battle_->setGround(ground_ ? &*ground_ : desc_.ground);
  bodies_.emplace(*battle_, desc_.scene, desc_.species, enemyNpcs_);
  // The fighters move and time their animations by their models' clips, and the techniques fly as
  // their effect scripts do.
  for (game::battle::FighterId id = 0; id < battle_->state().fighters.size(); ++id) {
    if (std::optional<game::battle::FighterClips> clips = bodies_->clips(id)) {
      battle_->setClips(id, std::move(*clips));
    }
  }
  flights_.reset();
  if (desc_.files != nullptr) {
    flights_.emplace([files = desc_.files](
                         std::string_view move) { return session::readMoveEffect(*files, move); },
                     *bodies_, &*bodies_);
    battle_->setFlights(&*flights_);
  }
  desc_.session->startBattleSounds();
  // btl.c: loadBattleData loads each enemy's species bank into slot 4 + its place.
  std::vector<std::string> enemySpecies;
  for (const game::battle::Combatant& enemy : introEnemies) {
    enemySpecies.push_back(enemy.species);
  }
  desc_.session->useEnemySounds(enemySpecies);
  if (game::story::SoundPort* sound = desc_.session->sound();
      sound != nullptr && desc_.files != nullptr) {
    effects_.emplace(*desc_.files, *sound);
    effects_->setBodies(*bodies_);
    effects_->useRolls(battle_->originalRolls(), battle_->particleRolls());
  }
  map_ = map;
  drops_.clear();
  numbers_.clear();
  numbersSeconds_ = 0.0;
  publishNumbers();
  publishHpBars();
  fixedFight_ = fixedFight;
  onScreenAtStart_ = npcsOnScreen();
  takeFigures();
  showFighters(0.0);
  if (session::SessionMusic* sessionMusic = desc_.session->music(); sessionMusic != nullptr) {
    sessionMusic->startBattle(music);
  }
  core::logInfo("{}", core::formatMessage(messages::BattleStarted, enemies, map));
  // btl.c: handleBattleStart marks each enemy with an anger bubble before the battle loads, and
  // battle_setup.c: BTL_handleBattleIntro shows the "BATTLE" banner before the fight starts.
  intro_.emplace(battlesStarted_++);
  // The command strip opens with the banner (BTL_initializeCombat, just before it).
  menu_ = {};
  // BTL_initializePartnerStatusBars: the partner's bars wait off the right edge.
  bars_.reset();
  shout_.clear();
  publishShout();
  menuSeconds_ = 0.0;
  closing_ = false;
  publishMenu();
  introEnemies_ = std::move(introEnemies);
  angerAnchors_.assign(introEnemies_.size(), std::nullopt);
  fleeAnchors_.clear();
  introSeconds_ = 0.0;
  retreat_.reset();
  endEscape();
  core::logDebug("[port:battle] handleBattleStart (btl.c:237): {} anger bubbles; "
                 "BTL_handleBattleIntro (battle_setup.c:207) follows",
                 introEnemies_.size());
  desc_.ui->setSetting(BattlePhaseSetting, std::string{IntroPhase});
  publish();
  publishIntro();
  desc_.ui->show(BattleDocument, true);
  return {};
}

void BattleScreen::tick(double seconds) {
  syncBag();
  tickVictory(seconds);
  tickMatchEnd(seconds);
  if (battle_) {
    tickBattle(seconds);
  } else {
    tickNumbers(seconds);
  }
  if (exit_) {
    tickExit(seconds);
  }
  if (!battle_) {
    tickMenu(seconds);
  }
  showFighters(seconds);
}

glm::vec3 BattleScreen::partnerWorld() const {
  const game::battle::Point at = battle_->state().partner().position;
  return {static_cast<float>(at.x), 0.0F, static_cast<float>(at.z)};
}

void BattleScreen::tickBattle(double seconds) {
  if (intro_) {
    tickIntro(seconds);
    return;
  }
  if (!partnerBeforeFrame_) {
    partnerBeforeFrame_ = battle_->state().partner().position;
  }
  battle_->advance(seconds, [this](const game::battle::BattleEvents& frame) {
    // tamerTickBattle runs before the fighters move: the player looks where the partner stood as
    // the frame began.
    facePlayerToPartner(*partnerBeforeFrame_);
    partnerBeforeFrame_ = battle_->state().partner().position;
    // battle_main.c: BTL_battleMain: BTL_tickPartnerWaypointTrail after the fighters moved.
    if (desc_.scene != nullptr) {
      desc_.scene->field().tickBattleTrail(partnerWorld());
    }
    if (effects_) {
      if (const render::MapCamera* camera =
              desc_.scene != nullptr ? desc_.scene->camera() : nullptr) {
        effects_->setView(effectViewOf(*camera));
      }
      effects_->frame(frame, battle_->moves());
    }
    stepNumbers(frame);
    stepMenu(true);
    stepShout();
    stepThrow();
    stepEscape();
  });
  publish();
  takeFigures();
  publishHpBars();
  showEffects();
  showStatusVisuals();
  if (battle_->finished()) {
    finish();
  }
}

// battle_effect.c: BTL_renderEFEEngine: what the effects drew on the battle's last frame.
void BattleScreen::showEffects() const {
  if (desc_.scene == nullptr) {
    return;
  }
  std::vector<SceneEffect> shown;
  if (effects_) {
    for (const game::battle::EffectDraw& draw : effects_->draws()) {
      shown.push_back({.move = effects_->moveOf(draw.slot),
                       .shape = draw.shape,
                       .palettes = effects_->palettesOf(draw.slot)});
    }
  }
  desc_.scene->setEffects(std::move(shown));
  desc_.scene->setMapLayerHidden(battle_ && effects_ && effects_->mapHidden());
}

// battle_effect.c: the status visuals over the fighters (game::battle::StatusVisuals), with each
// fighter's actor and its species' height and radius; none once the battle is over.
void BattleScreen::showStatusVisuals() const {
  if (desc_.scene == nullptr) {
    return;
  }
  if (!battle_) {
    desc_.scene->setStatusVisuals(std::nullopt, {});
    return;
  }
  const game::battle::BattleState& state = battle_->state();
  std::vector<FieldScene::StatusBody> bodies;
  for (std::size_t id = 0; id < state.fighters.size(); ++id) {
    FieldScene::StatusBody body;
    if (id > 0) {
      if (id > enemyNpcs_.size()) {
        break;
      }
      body.npc = enemyNpcs_[id - 1];
    }
    const assets::DigimonRow* row = desc_.species != nullptr
                                        ? desc_.species->find(state.fighters[id].combatant.species)
                                        : nullptr;
    if (row != nullptr) {
      body.height = row->height;
      body.radius = row->radius;
      if (row->conditionFx) {
        const auto& offset = row->conditionFx->offset;
        body.conditionFx =
            std::pair{row->conditionFx->node,
                      glm::vec3{static_cast<float>(offset[0]), static_cast<float>(offset[1]),
                                static_cast<float>(offset[2])}};
      }
    }
    bodies.push_back(body);
  }
  desc_.scene->setStatusVisuals(battle_->statusVisuals(), std::move(bodies));
}

// battle_main.c: BTL_tickAttackHits adds the damage of a hit, and of a blocked melee attack, over
// the fighter it reached (addEntityText(entity, slot, 0, dmg, 0)), BTL_tickStatusEffects the
// poison's in its own tint (colour 0xc); a miss and a blocked ranged attack show none. What a
// thrown item heals (item.c: handleHPHealingItem, handleMPHealingItem, handleRestore,
// handleDoubleFloppy) and what a support move raises (BTL_buffStats) show in green (colour 0xb)
// with the stat's icon. Each number stays where the fighter's feet showed as it was added
// (setCombatTextPosition: the battle's GAME_STATE is 1, not the versus mode's 4 that follows the
// fighter). renderEntityText draws it on the same frame.
void BattleScreen::stepNumbers(const game::battle::BattleEvents& frame) {
  using game::battle::EventKind;
  using screens::NumberColour;
  using screens::NumberIcon;
  for (const game::battle::BattleEvent& event : frame) {
    NumberColour colour = NumberColour::Gain;
    NumberIcon icon = NumberIcon::None;
    switch (event.kind) {
    case EventKind::AttackHit:
      colour = NumberColour::Plain;
      break;
    case EventKind::AttackBlocked:
      if (event.amount <= 0) {
        continue;
      }
      colour = NumberColour::Plain;
      break;
    case EventKind::PoisonDamage:
      colour = NumberColour::Poison;
      break;
    case EventKind::HpRestored:
      icon = NumberIcon::Hp;
      break;
    case EventKind::MpRestored:
      icon = NumberIcon::Mp;
      break;
    case EventKind::OffenseRaised:
      icon = NumberIcon::Offense;
      break;
    case EventKind::DefenseRaised:
      icon = NumberIcon::Defense;
      break;
    case EventKind::SpeedRaised:
      icon = NumberIcon::Speed;
      break;
    default:
      continue;
    }
    std::optional<screens::WindowFraction> feet;
    if (desc_.scene != nullptr && (event.fighter == 0 || event.fighter <= enemyNpcs_.size())) {
      const std::optional<std::int32_t> npc =
          event.fighter == 0 ? std::nullopt
                             : std::optional<std::int32_t>{enemyNpcs_[event.fighter - 1]};
      if (const std::optional<glm::vec2> shown = desc_.scene->fighterFeetOnWindow(npc)) {
        feet = screens::WindowFraction{.x = shown->x, .y = shown->y};
      }
    }
    numbers_.add(event.fighter, event.amount, colour, feet, icon);
    core::logDebug("[port:battle] addEntityText (entity_text.c:96): fighter={} value={} colour={} "
                   "icon={}{}",
                   event.fighter, event.amount, static_cast<int>(colour), static_cast<int>(icon),
                   feet ? "" : " off the camera");
  }
  numbers_.step();
  publishNumbers();
}

// After the battle the numbers left go on at the battle's rate until the last one goes.
void BattleScreen::tickNumbers(double seconds) {
  if (numbers_.empty() && numbers_.shown().empty()) {
    numbersSeconds_ = 0.0;
    return;
  }
  numbersSeconds_ += seconds;
  while (numbersSeconds_ >= IntroFrameSeconds) {
    numbersSeconds_ -= IntroFrameSeconds;
    numbers_.step();
  }
  publishNumbers();
}

void BattleScreen::publishNumbers() const {
  screens::publish(*desc_.ui, screens::settingsOf(numbers_.shown()),
                   screens::DamageRevisionSetting);
}

// battle_main.c: BTL_renderEnemyHPBars draws, every frame, the bar of each enemy whose bar shows
// (Battle::hpBarShown) beside where its model's node 1 projects (getEntityScreenPos(entity, 1)), so
// the bar follows the enemy. BTL_deinitializeCombat takes them all away (BTL_removeEnemyHPBars).
void BattleScreen::publishHpBars() const {
  std::vector<screens::EnemyHpBar> bars;
  const render::MapCamera* camera = desc_.scene != nullptr ? desc_.scene->camera() : nullptr;
  if (battle_ && camera != nullptr && camera->viewportSize().y > 0.0F) {
    const double aspect = static_cast<double>(camera->viewportSize().x) /
                          static_cast<double>(camera->viewportSize().y);
    const auto& fighters = battle_->state().fighters;
    for (game::battle::FighterId id = 1; id < fighters.size() && id <= enemyNpcs_.size(); ++id) {
      if (!battle_->hpBarShown(id)) {
        continue;
      }
      const std::optional<glm::vec2> node =
          desc_.scene->fighterNodeOnWindow(enemyNpcs_[id - 1], HpBarNode);
      if (!node) {
        continue;
      }
      const game::battle::Fighter& fighter = fighters[id];
      bars.push_back(screens::enemyHpBarOf({.enemy = id,
                                            .node = {.x = node->x, .y = node->y},
                                            .aspect = aspect,
                                            .hp = fighter.hp,
                                            .maxHp = fighter.stats.maxHp}));
    }
  }
  screens::publish(*desc_.ui, screens::settingsOf(bars), screens::HpBarsRevisionSetting);
}

// Each fighter's place, facing and clip are the battle's (battle_main.c: the fighters are the
// partner's and the NPCs' entities, which the battle moves, turns and animates itself).
void BattleScreen::takeFigures() {
  const game::battle::BattleState& state = battle_->state();
  figures_.clear();
  for (const game::battle::Fighter& fighter : state.fighters) {
    figures_.push_back(
        {.x = static_cast<float>(fighter.position.x),
         .z = static_cast<float>(fighter.position.z),
         .yaw = assets::yawOfDirection(
             {static_cast<float>(fighter.facing.x), 0.0F, static_cast<float>(fighter.facing.z)}),
         .clip = static_cast<std::uint32_t>(fighter.shown.clip),
         .starts = fighter.shown.starts,
         .frozen = fighter.shown.frozen,
         .soundSlot = PartnerSoundSlot + static_cast<std::int64_t>(figures_.size()),
         .pace = 1.0F,
         .scale = {fighter.shown.flat.across, fighter.shown.flat.up, fighter.shown.flat.across},
         .flatSprite = fighter.shown.flat.sprite});
  }
  partnerStance_ = static_cast<std::uint32_t>(game::battle::stanceClipOf(state.partner()));
}

void BattleScreen::showFighters(double seconds) {
  if (desc_.scene == nullptr || figures_.empty()) {
    return;
  }
  for (std::size_t index = 0; index < figures_.size(); ++index) {
    std::optional<std::int32_t> npc;
    if (index > 0) {
      if (index > enemyNpcs_.size()) {
        continue;
      }
      npc = enemyNpcs_[index - 1];
    }
    if (!desc_.scene->holdFighter(npc, figures_[index]) && seconds == 0.0) {
      core::logWarning("{}",
                       core::formatMessage(messages::FighterNotShown, index, npc.value_or(-1)));
    }
  }
  desc_.scene->runFighters(seconds);
}

// The partner stands again (BTL_handleVictorySequence restarts its stance around the poses).
void BattleScreen::standPartner(std::uint32_t clip, std::string_view origin) {
  if (figures_.empty()) {
    return;
  }
  FighterFigure& partner = figures_.front();
  partner.clip = clip;
  ++partner.starts;
  partner.frozen = false;
  core::logDebug("[port:battle] {}: the partner startAnimation({:#x})", origin, clip);
}

void BattleScreen::playSound(std::int64_t sound) const {
  if (game::story::SoundPort* port = desc_.session->sound(); port != nullptr) {
    port->playSound(0, sound);
  }
}

void BattleScreen::tickVictory(double seconds) {
  if (!victoryPoseSeconds_) {
    tickPoses(seconds);
    return;
  }
  *victoryPoseSeconds_ -= seconds;
  if (*victoryPoseSeconds_ > 0.0) {
    return;
  }
  victoryPoseSeconds_.reset();
  core::logDebug("[port:audio] BTL_handleVictorySequence (battle_main.c:904): the victory poses; "
                 "playSound(0, 0x18)");
  playSound(VictoryPoseSound);
  startPoses();
}

void BattleScreen::tickMatchEnd(double seconds) {
  if (!matchEndSeconds_ || matchEndHeld_) {
    return;
  }
  if (partnerPosing_ && desc_.scene != nullptr && desc_.scene->partnerClipFinished()) {
    desc_.scene->playPartnerClip(PartnerPoseClip);
  }
  *matchEndSeconds_ -= seconds;
  if (*matchEndSeconds_ > 0.0) {
    return;
  }
  matchEndSeconds_.reset();
  core::logDebug("[port:tournament] STD_deinitializeCombat (std_main.c:4572): the match is over, "
                 "with no end box");
  close();
}

void BattleScreen::startPoses() {
  if (desc_.scene == nullptr) {
    openEndBox();
    return;
  }
  desc_.scene->playPlayerSceneClip(PlayerPoseClip);
  desc_.scene->playPartnerClip(PartnerPoseClip);
  playerPosing_ = true;
  partnerPosing_ = true;
  posesSeconds_ = 0.0;
}

// battle_main.c: BTL_handleVictorySequence: each fighter whose pose has played through goes back to
// its stance, and the end box waits for both.
void BattleScreen::tickPoses(double seconds) {
  if (!posesSeconds_) {
    return;
  }
  FieldScene& scene = *desc_.scene;
  *posesSeconds_ += seconds;
  const bool timedOut = *posesSeconds_ >= PoseGuardSeconds;
  if (playerPosing_ && (scene.playerClipPlayedThrough() || timedOut)) {
    standPlayer();
    playerPosing_ = false;
  }
  if (partnerPosing_ && (scene.partnerClipFinished() || timedOut)) {
    scene.playPartnerClip(std::nullopt);
    partnerPosing_ = false;
    standPartner(partnerStance_, "BTL_handleVictorySequence (battle_main.c:910)");
  }
  if (playerPosing_ || partnerPosing_) {
    return;
  }
  posesSeconds_.reset();
  core::logDebug("[port:battle] BTL_handleVictorySequence (battle_main.c:919): the poses are over; "
                 "handleBattleEndBox");
  openEndBox();
}

void BattleScreen::releasePoses() {
  if (playerPosing_) {
    standPlayer();
  }
  if (partnerPosing_) {
    desc_.scene->playPartnerClip(std::nullopt);
  }
  playerPosing_ = false;
  partnerPosing_ = false;
  posesSeconds_.reset();
}

// The end box opens only after the poses (handleBattleEndBox follows them), so nothing pressed
// before reaches the box.
void BattleScreen::openEndBox() const {
  if (ending_) {
    desc_.ui->setSetting(BattlePhaseSetting, std::string{EndPhase});
  }
}

// The opening runs at the original's battle rate, a frame each 3 of the screen's
// (BTL_battleTickFrame waits for VSync(3)), and the battle does not run meanwhile.
void BattleScreen::tickIntro(double seconds) {
  introSeconds_ += seconds;
  while (intro_ && introSeconds_ >= IntroFrameSeconds) {
    introSeconds_ -= IntroFrameSeconds;
    stepMenu(false);
    const screens::IntroStep before = intro_->current();
    if (before == screens::IntroStep::Banner) {
      stepRetreat();
      intro_->holdBurst(!stepFlight(IntroFrameSeconds));
    }
    intro_->step();
    const screens::IntroStep after = intro_->current();
    if (after == before) {
      continue;
    }
    game::story::SoundPort* sound = desc_.session->sound();
    if (after == screens::IntroStep::Banner) {
      menu_.open();
      publishMenu();
      core::logDebug("[port:battle] BTL_initializeCommandMenu (command_menu.c:153): the command "
                     "strip drops in");
      startRetreat();
      startFlight();
      core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:226): the banner "
                     "gathers; playSound(0, 0x10)");
      if (sound != nullptr) {
        sound->playSound(0, BannerSound);
      }
    } else if (after == screens::IntroStep::Burst) {
      endRetreat();
      endFlight();
      core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:321): the banner "
                     "bursts; playSound(0, 0x11)");
      if (sound != nullptr) {
        sound->playSound(0, BurstSound);
      }
    } else if (after == screens::IntroStep::Done) {
      if (desc_.scene != nullptr) {
        desc_.scene->field().startBattleTrail(partnerWorld());
      }
      core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:328): GAME_STATE 1, "
                     "the fight starts; command strip timer {}",
                     menu_.frames());
      intro_.reset();
      desc_.ui->setSetting(BattlePhaseSetting, std::string{FightPhase});
      publish();
    }
  }
}

// Once a drawn frame, not once a tick: the banner's document redraws its hundred and more pieces
// on each published frame, and a slow frame that runs several ticks would draw them again for
// each, slowing the next frame further.
// btl.c: renderFleeBubble keeps each mark within 140 pixels of the original screen's centre across
// and 100 up and down, so the mark of an NPC at the edge still shows.
screens::AngerAnchor BattleScreen::keptOnScreen(screens::AngerAnchor anchor) const {
  const render::MapCamera* camera = desc_.scene != nullptr ? desc_.scene->camera() : nullptr;
  if (!anchor || camera == nullptr || camera->scale() <= 0.0F) {
    return anchor;
  }
  constexpr glm::vec2 MarkReach{140.0F, 100.0F};
  const glm::vec2 viewport = camera->viewportSize();
  const glm::vec2 pixel{anchor->x * viewport.x, anchor->y * viewport.y};
  const glm::vec2 fromCentre =
      glm::clamp((pixel - (viewport / 2.0F)) / camera->scale(), -MarkReach, MarkReach);
  const glm::vec2 kept = ((fromCentre * camera->scale()) + (viewport / 2.0F)) / viewport;
  return screens::WindowFraction{.x = kept.x, .y = kept.y};
}

void BattleScreen::showIntro(const EnemyAnchor& anchor) {
  if (intro_ && intro_->current() == screens::IntroStep::Anger) {
    for (std::size_t index = 0; index < introEnemies_.size(); ++index) {
      angerAnchors_.at(index) = keptOnScreen(anchor(introEnemies_[index], intro_->angerLift()));
    }
    fleeAnchors_ = runOffAnchors(intro_->angerLift());
    for (screens::AngerAnchor& flee : fleeAnchors_) {
      flee = keptOnScreen(flee);
    }
  }
  publishIntro();
}

void BattleScreen::publishIntro() const {
  const screens::BattleIntroView view =
      intro_ ? screens::battleIntroViewOf(*intro_, angerAnchors_, fleeAnchors_)
             : screens::BattleIntroView{};
  screens::publish(*desc_.ui, screens::settingsOf(view, introEnemies_.size()),
                   screens::IntroRevisionSetting);
}

bool BattleScreen::handle(const ui::UiCommand& command) {
  // Nothing is taken until the fight starts (BTL_handleBattleIntro reads no buttons).
  if (intro_ && (command.name == BattleCommandName || command.name == BattleChargeCommand ||
                 command.name == BattleBagOpenCommand || command.name == BattleThrowCommand)) {
    return true;
  }
  if (command.name == BattleCommandName) {
    const auto* number = std::get_if<double>(&command.value);
    if (battle_ && number != nullptr) {
      const auto chosen = static_cast<game::battle::Command>(static_cast<int>(*number));
      if (auto taken = battle_->command(chosen); !taken) {
        core::logInfo("{}", core::formatMessage(messages::BattleCommandRefused,
                                                game::battle::commandTextKey(chosen),
                                                taken.error().message));
      } else {
        shout(chosen);
      }
    }
    return true;
  }
  if (command.name == BattleChargeCommand) {
    if (battle_) {
      battle_->pressFinisherCharge();
    }
    return true;
  }
  if (command.name == BattleBagOpenCommand) {
    openBag();
    return true;
  }
  if (command.name == BattleBagCloseCommand) {
    if (battle_) {
      battle_->closeBag();
      publish();
    }
    return true;
  }
  // inventory.c: tickInventoryUI: in battle the bag's Use throws the item (startThrowingItem) once
  // the bag's boxes have closed, where the field's feeds it.
  if (command.name == BattleThrowCommand || (command.name == BagUseCommand && battle_)) {
    if (const auto* number = std::get_if<double>(&command.value); number != nullptr) {
      throwItem(*number);
    }
    return true;
  }
  if (command.name == BattleCloseCommand) {
    close();
    return true;
  }
  if (command.name == BattleEndCountCommand) {
    core::logDebug("[port:audio] tickBitBox (battle_ui.c:438), renderPostBattleStatsBox "
                   "(battle_ui.c:609): a point or a bit counted; playSound(0, 0x16)");
    playSound(EndCountSound);
    return true;
  }
  if (command.name == BattleEndCountedCommand) {
    core::logDebug("[port:audio] tickBitBox (battle_ui.c:448): the bits are in; "
                   "playSound(0, 0x17)");
    playSound(EndCountedSound);
    return true;
  }
  return false;
}

void BattleScreen::pressFinisher() {
  // Nothing is taken while the battle opens, as for the strip's commands.
  if (!battle_ || intro_) {
    return;
  }
  // Before the gauge is full the button does nothing, as in the original.
  if (auto taken = battle_->command(game::battle::Command::Finisher); !taken) {
    core::logDebug(
        "{}", core::formatMessage(messages::BattleCommandRefused,
                                  game::battle::commandTextKey(game::battle::Command::Finisher),
                                  taken.error().message));
  } else {
    shout(game::battle::Command::Finisher);
  }
}

screens::NameKeys BattleScreen::names() const {
  return {desc_.species, desc_.inventory != nullptr ? &desc_.inventory->items() : nullptr,
          &desc_.battle->catalog()};
}

void BattleScreen::publish() const {
  if (!battle_) {
    return;
  }
  screens::publish(*desc_.ui, screens::settingsOf(screens::battleViewOf(*battle_, names())),
                   screens::BattleRevisionSetting);
}

void BattleScreen::openBag() {
  if (!battle_) {
    return;
  }
  if (auto opened = battle_->openBag(); !opened) {
    core::logInfo("{}", core::formatMessage(messages::BattleBagRefused, opened.error().message));
    return;
  }
  // inventory.c: selectOptionMenu: in battle only the items of a battle colour can be thrown.
  screens::publish(
      *desc_.ui,
      screens::settingsOf(screens::bagViewOf(desc_.inventory->inventory(), desc_.inventory->items(),
                                             names(), game::inventory::UseContext::Battle)),
      screens::BagRevisionSetting);
  publish();
  // addInventoryUI opens the field's bag (createInventoryView), growing from the player as there.
  if (desc_.scene != nullptr) {
    publishPlayerPoint(*desc_.ui, *desc_.scene);
  }
  desc_.ui->setSetting(screens::BagResultSetting, std::string{});
  desc_.ui->setSetting(PartnerMenuStartPageSetting, std::string{PartnerMenuItemsPage});
  desc_.ui->setSetting(PartnerMenuContextSetting, std::string{PartnerMenuBattleContext});
  desc_.ui->show(PartnerMenuDocument, true);
  bagShown_ = true;
}

void BattleScreen::syncBag() {
  if (!bagShown_) {
    return;
  }
  const bool open = battle_ && battle_->bagOpen();
  if (!desc_.ui->isVisible(PartnerMenuDocument)) {
    bagShown_ = false;
    desc_.ui->setSetting(PartnerMenuContextSetting, std::string{});
    if (open) {
      battle_->closeBag();
      publish();
    }
  } else if (!open) {
    bagShown_ = false;
    desc_.ui->setSetting(PartnerMenuContextSetting, std::string{});
    desc_.ui->hide(PartnerMenuDocument);
  }
}

void BattleScreen::throwItem(double slot) {
  if (!battle_) {
    return;
  }
  const std::optional<assets::ItemHandle> item =
      itemAt(desc_.inventory->inventory().slots(), slotOf(slot));
  if (!item) {
    core::logInfo("{}",
                  core::formatMessage(messages::BattleThrowRefused, slot, messages::EmptyBagSlot));
    return;
  }
  auto thrown = desc_.battle->throwItem(*battle_, *item);
  if (!thrown) {
    core::logInfo("{}",
                  core::formatMessage(messages::BattleThrowRefused, slot, thrown.error().message));
  } else if (thrown->outcome != game::inventory::UseOutcome::Used) {
    core::logInfo(
        "{}", core::formatMessage(messages::BattleThrowRefused, slot, messages::ItemNotThrowable));
  } else {
    core::logInfo("{}", core::formatMessage(messages::BattleItemThrown,
                                            desc_.inventory->items().at(*item).id));
    startThrow(*item);
  }
  publish();
}

void BattleScreen::finish() {
  const std::optional<game::battle::BattleResult> finished =
      battle_ ? battle_->result() : std::nullopt;
  battle_.reset();
  endThrow();
  publishHpBars();
  showStatusVisuals();
  if (effects_) {
    effects_->end();
    effects_.reset();
    showEffects();
  }
  flights_.reset();
  bodies_.reset();
  const bool fled = finished && finished->outcome == game::battle::Outcome::Fled;
  endEscape();
  if (!fled && desc_.scene != nullptr) {
    // A partner down under the flee's fade ends the battle another way: the screen comes back.
    desc_.scene->field().releaseShade();
  }
  // BTL_deinitializeCombat reloads the map's bank only after a victory; a tournament's match ends
  // with the arena's bank again (std_setup.c: loadMapSounds(0x10)). A battle that never gave a
  // result ends as a won one.
  desc_.session->endBattleSounds(!finished || finished->outcome == game::battle::Outcome::Victory ||
                                 finished->kind == game::battle::BattleKind::Tournament);
  session::SessionMusic* music = desc_.session->music();
  if (music != nullptr) {
    music->endBattle();
  }
  if (!finished) {
    if (music != nullptr) {
      music->leaveBattle(false);
    }
    // No end box and no exit follow: the field takes the player back now.
    if (desc_.scene != nullptr) {
      desc_.scene->playPlayerSceneClip(std::nullopt);
    }
    return;
  }
  const game::battle::BattleResult& result = *finished;
  outcome_ = result.outcome;
  won_ = result.outcome == game::battle::Outcome::Victory;
  const bool match = result.kind == game::battle::BattleKind::Tournament;
  // battle_main.c: BTL_deinitializeCombat: without a victory the numbers go at once
  // (removeEntityText); std_main.c: STD_deinitializeCombat removes them after any match.
  if (!won_ || match) {
    numbers_.clear();
    publishNumbers();
  }
  if (match) {
    if (won_ && desc_.scene != nullptr) {
      desc_.scene->playPartnerClip(PartnerPoseClip);
      partnerPosing_ = true;
    }
    matchEndSeconds_ = (won_ ? MatchWonFrames : MatchLostFrames) * IntroFrameSeconds +
                       (MatchAfterFrames * IntroFrameSeconds);
  } else if (won_) {
    standPartner(VictoryStanceClip, "BTL_handleVictorySequence (battle_main.c:886)");
    core::logDebug("[port:audio] BTL_handleVictorySequence (battle_main.c:889): the fanfare; "
                   "playSound(0, 0x15)");
    playSound(VictorySound);
    victoryPoseSeconds_ = VictoryPoseSeconds;
  }
  core::logInfo("{}", core::formatMessage(messages::BattleFinished,
                                          game::battle::outcomeTextKey(result.outcome)));
  auto aftermath = desc_.battle->finish(*desc_.session, result);
  if (!aftermath) {
    core::logError("{}",
                   core::formatMessage(messages::BattleStartFailed, aftermath.error().message));
    aftermath = session::BattleAftermath{};
  }
  drops_ = aftermath->drops;
  if (result.outcome == game::battle::Outcome::Fled) {
    // BTL_deinitializeCombat runs BTL_handleVictorySequence (the end box with its stats and bits)
    // only for a battle not fled: the strip goes up under the black screen and the battle ends.
    core::logDebug("[port:battle] BTL_deinitializeCombat (battle_main.c:767): fled, no end box");
    ending_ = true;
    close();
    return;
  }
  screens::BattleEndView view = screens::battleEndViewOf(result, *aftermath, names());
  const game::partner::CareLimits& limits = desc_.session->partnerRules().care.limits;
  for (const game::partner::Stat stat : game::partner::AllStats) {
    view.statMaxima.at(static_cast<std::size_t>(stat)) =
        stat == game::partner::Stat::Hp || stat == game::partner::Stat::Mp ? limits.hpMpMax
                                                                           : limits.statMax;
  }
  if (desc_.inventory != nullptr) {
    view.money = desc_.inventory->inventory().money();
  }
  const game::battle::HudRules& hud = desc_.battle->rules().hud;
  view.countDelayFrames = hud.endCountDelayFrames;
  view.waitFrames = hud.endWaitFrames;
  screens::publish(*desc_.ui, screens::settingsOf(view), screens::BattleRevisionSetting);
  desc_.ui->setSetting(
      BattlePhaseSetting,
      std::string{victoryPoseSeconds_ || matchEndSeconds_ ? VictoryPhase : EndPhase});
  ending_ = true;
}

bool BattleScreen::defeatEnemies() {
  if (!battle_ || intro_ || ending_) {
    return false;
  }
  battle_->defeatEnemies();
  return true;
}

bool BattleScreen::runOutClock(std::string_view side) {
  if (!battle_ || intro_ || ending_) {
    return false;
  }
  battle_->runOutClock(side == "win"    ? std::optional{true}
                       : side == "lose" ? std::optional{false}
                                        : std::nullopt);
  return true;
}

bool BattleScreen::defeatPartner() {
  if (!battle_ || intro_ || ending_) {
    return false;
  }
  battle_->defeatPartner();
  return true;
}

std::optional<game::battle::Outcome> BattleScreen::takeOutcome() {
  if (active()) {
    return std::nullopt;
  }
  return std::exchange(outcome_, std::nullopt);
}

void BattleScreen::close() {
  if (!ending_) {
    return;
  }
  ending_ = false;
  victoryPoseSeconds_.reset();
  matchEndSeconds_.reset();
  numbers_.clear();
  publishNumbers();
  publishHpBars();
  releasePoses();
  desc_.ui->setSetting(BattlePhaseSetting, std::string{ClosingPhase});
  for (const game::battle::Drop& drop : std::exchange(drops_, {})) {
    if (desc_.fieldItems != nullptr &&
        !desc_.fieldItems->dropItem(
            map_, drop.item,
            {static_cast<float>(drop.position.x), 0.0F, static_cast<float>(drop.position.z)})) {
      core::logInfo("{}", core::formatMessage(messages::BattleDropLost, drop.item));
    }
  }
  startExit();
  if (!exit_) {
    closeMenu();
  }
}

session::BattleMusicFacts BattleScreen::musicFactsOf(const session::BattleRequest& request) const {
  session::BattleMusicFacts facts{
      .firstEnemy = {}, .firstEnemyType = std::nullopt, .scripted = request.scripted};
  if (!request.enemies.empty()) {
    facts.firstEnemy = request.enemies.front().species;
    const assets::DigimonRow* row =
        desc_.species != nullptr ? desc_.species->find(facts.firstEnemy) : nullptr;
    if (row != nullptr) {
      facts.firstEnemyType = row->index;
    }
  }
  return facts;
}

// The partner's bars tick with the strip (battle_hud.c: BTL_tickPartnerStatusBars reads
// BTL_COMMAND_MENU_ACTIVE) and go with it (BTL_removePartnerStatusBars follows its closing).
void BattleScreen::stepMenu(bool fighting) {
  if (!menu_.shown()) {
    return;
  }
  menu_.step(fighting);
  bars_.step(menu_.active());
  publishMenu();
}

void BattleScreen::publishMenu() const {
  screens::ScreenSettings settings = menu_.settings();
  std::ranges::move(bars_.settings(), std::back_inserter(settings));
  screens::publish(*desc_.ui, settings, screens::CommandMenuRevisionSetting);
}

// After the fight the strip stays, still counting its frames, through the victory, the end box and
// the enemies' exit; then it goes back up and the battle ends (battle_main.c:777:
// while (BTL_isCommandMenuClosed() == 0) BTL_battleTickFrame()).
void BattleScreen::tickMenu(double seconds) {
  if (!menu_.shown() && !closing_) {
    menuSeconds_ = 0.0;
    return;
  }
  menuSeconds_ += seconds;
  while (menuSeconds_ >= IntroFrameSeconds) {
    menuSeconds_ -= IntroFrameSeconds;
    if (closing_ && menu_.closed()) {
      closing_ = false;
      menuSeconds_ = 0.0;
      publishMenu();
      core::logDebug("[port:battle] BTL_isCommandMenuClosed (command_menu.c:328): the strip is up; "
                     "the battle ends");
      desc_.ui->hide(BattleDocument);
      endExit();
      return;
    }
    stepMenu(false);
    stepShout();
  }
}

void BattleScreen::closeMenu() {
  menu_.close();
  closing_ = true;
  menuSeconds_ = 0.0;
  core::logDebug("[port:battle] BTL_deinitializeCombat (battle_main.c:777): the command strip goes "
                 "back up");
}

// battle_tick.c (the player's battle tick): a command the partner takes (with an enemy still
// standing) is shouted at once (BTL_drawCommandShout), over where the player's node 4 shows then.
void BattleScreen::shout(game::battle::Command command) {
  if (!battle_ || battle_->state().livingEnemies() == 0) {
    return;
  }
  std::optional<screens::WindowFraction> node;
  double aspect = 1.0;
  if (desc_.scene != nullptr) {
    if (const std::optional<glm::vec2> shown = desc_.scene->playerNodeOnWindow(ShoutNode)) {
      node = screens::WindowFraction{.x = shown->x, .y = shown->y};
    }
    if (const render::MapCamera* camera = desc_.scene->camera();
        camera != nullptr && camera->viewportSize().y > 0.0F) {
      aspect = static_cast<double>(camera->viewportSize().x) /
               static_cast<double>(camera->viewportSize().y);
    }
  }
  shout_.shout(screens::shoutTextKey(command, battle_->state().partner(), names()), node, aspect);
  core::logDebug("[port:battle] BTL_drawCommandShout (command_shout.c:54): command {}{}",
                 static_cast<int>(command), node ? "" : " off the camera");
  publishShout();
}

// BTL_renderCommandShout counts its frames as the battle's frames are drawn, through the victory
// and the end box too.
void BattleScreen::stepShout() {
  if (!shout_.shown()) {
    return;
  }
  shout_.step();
  publishShout();
}

void BattleScreen::publishShout() const {
  screens::publish(*desc_.ui, shout_.settings(), screens::ShoutRevisionSetting);
}

} // namespace dwe::app
