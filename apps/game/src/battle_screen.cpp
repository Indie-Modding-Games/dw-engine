#include "battle_screen.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/battle/text_keys.hpp>
#include <dwe/game/inventory/use.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/screens/bag_view.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "bag_slots.hpp"
#include "field_scene.hpp"
#include "messages.hpp"

namespace dwe::app {
namespace {

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
// Before the fanfare the partner takes its stance, the healthy one whatever its HP.
constexpr std::uint32_t VictoryStanceClip = 0x21;
// The port's safeguard, not the original's: a pose that never plays through holds the box no longer
// than this many battle frames.
constexpr int PoseGuardFrames = 200;
constexpr double PoseGuardSeconds = PoseGuardFrames * IntroFrameSeconds;
// battle_ui.c: tickBitBox and renderPostBattleStatsBox: the end box's count, a sound for each point
// and bit, and another once the bits are all in; both in slot 0.
constexpr std::int64_t EndCountSound = 0x16;
constexpr std::int64_t EndCountedSound = 0x17;

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
  battle_->setGround(desc_.ground);
  effects_.reset();
  enemyNpcs_ = std::move(enemyNpcs);
  bodies_.emplace(*battle_, desc_.scene, desc_.species, enemyNpcs_);
  desc_.session->useBattleSounds(true);
  if (game::story::SoundPort* sound = desc_.session->sound();
      sound != nullptr && desc_.files != nullptr) {
    effects_.emplace(*desc_.files, *sound);
    effects_->setBodies(*bodies_);
  }
  map_ = map;
  drops_.clear();
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
  introEnemies_ = std::move(introEnemies);
  angerAnchors_.assign(introEnemies_.size(), std::nullopt);
  introSeconds_ = 0.0;
  retreat_.reset();
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
  tickVictory(seconds);
  if (battle_) {
    tickBattle(seconds);
  }
  if (exit_) {
    tickExit(seconds);
  }
  showFighters(seconds);
}

void BattleScreen::tickBattle(double seconds) {
  if (intro_) {
    tickIntro(seconds);
    return;
  }
  battle_->advance(seconds, [this](const game::battle::BattleEvents& frame) {
    facePlayerToPartner();
    if (effects_) {
      effects_->frame(frame, battle_->moves());
    }
  });
  publish();
  takeFigures();
  showEffects();
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
      shown.push_back({.move = effects_->moveOf(draw.slot), .shape = draw.shape});
    }
  }
  desc_.scene->setEffects(std::move(shown));
}

// Each fighter's place, facing and clip are the battle's (battle_main.c: the fighters are the
// partner's and the NPCs' entities, which the battle moves, turns and animates itself).
void BattleScreen::takeFigures() {
  const game::battle::BattleState& state = battle_->state();
  figures_.clear();
  for (const game::battle::Fighter& fighter : state.fighters) {
    figures_.push_back({.x = static_cast<float>(fighter.position.x),
                        .z = static_cast<float>(fighter.position.z),
                        .yaw = assets::yawOfDirection({static_cast<float>(fighter.facing.x), 0.0F,
                                                       static_cast<float>(fighter.facing.z)}),
                        .clip = static_cast<std::uint32_t>(fighter.shown.clip),
                        .starts = fighter.shown.starts,
                        .frozen = fighter.shown.frozen});
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
    scene.playPlayerSceneClip(std::nullopt);
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
    desc_.scene->playPlayerSceneClip(std::nullopt);
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
    const screens::IntroStep before = intro_->current();
    if (before == screens::IntroStep::Banner) {
      stepRetreat();
    }
    intro_->step();
    const screens::IntroStep after = intro_->current();
    if (after == before) {
      continue;
    }
    game::story::SoundPort* sound = desc_.session->sound();
    if (after == screens::IntroStep::Banner) {
      startRetreat();
      core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:226): the banner "
                     "gathers; playSound(0, 0x10)");
      if (sound != nullptr) {
        sound->playSound(0, BannerSound);
      }
    } else if (after == screens::IntroStep::Burst) {
      endRetreat();
      core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:321): the banner "
                     "bursts; playSound(0, 0x11)");
      if (sound != nullptr) {
        sound->playSound(0, BurstSound);
      }
    } else if (after == screens::IntroStep::Done) {
      core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:328): GAME_STATE 1, "
                     "the fight starts");
      intro_.reset();
      desc_.ui->setSetting(BattlePhaseSetting, std::string{FightPhase});
      publish();
    }
  }
  publishIntro();
}

void BattleScreen::placeIntro(const EnemyAnchor& anchor) {
  if (!intro_ || intro_->current() != screens::IntroStep::Anger) {
    return;
  }
  for (std::size_t index = 0; index < introEnemies_.size(); ++index) {
    angerAnchors_.at(index) = anchor(introEnemies_[index], intro_->angerLift());
  }
  publishIntro();
}

void BattleScreen::publishIntro() const {
  const screens::BattleIntroView view =
      intro_ ? screens::battleIntroViewOf(*intro_, angerAnchors_) : screens::BattleIntroView{};
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
  if (command.name == BattleThrowCommand) {
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
  }
  publish();
}

void BattleScreen::finish() {
  const std::optional<game::battle::BattleResult> finished =
      battle_ ? battle_->result() : std::nullopt;
  battle_.reset();
  if (effects_) {
    effects_->end();
    effects_.reset();
    showEffects();
  }
  bodies_.reset();
  desc_.session->useBattleSounds(false);
  session::SessionMusic* music = desc_.session->music();
  if (music != nullptr) {
    music->endBattle();
  }
  if (!finished) {
    if (music != nullptr) {
      music->leaveBattle(false);
    }
    return;
  }
  const game::battle::BattleResult& result = *finished;
  outcome_ = result.outcome;
  won_ = result.outcome == game::battle::Outcome::Victory;
  if (won_) {
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
  desc_.ui->setSetting(BattlePhaseSetting,
                       std::string{victoryPoseSeconds_ ? VictoryPhase : EndPhase});
  ending_ = true;
}

bool BattleScreen::defeatEnemies() {
  if (!battle_ || intro_ || ending_) {
    return false;
  }
  battle_->defeatEnemies();
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
  releasePoses();
  desc_.ui->hide(BattleDocument);
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
    endExit();
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

} // namespace dwe::app
