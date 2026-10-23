#include "battle_screen.hpp"

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
// battle_ui.c: tickBitBox and renderPostBattleStatsBox: the end box's count, a sound for each point
// and bit, and another once the bits are all in; both in slot 0.
constexpr std::int64_t EndCountSound = 0x16;
constexpr std::int64_t EndCountedSound = 0x17;

} // namespace

core::Result<void> BattleScreen::start(session::BattleRequest request) {
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
  map_ = map;
  drops_.clear();
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
  if (!battle_) {
    return;
  }
  if (intro_) {
    tickIntro(seconds);
    return;
  }
  battle_->advance(seconds);
  publish();
  if (battle_->finished()) {
    finish();
  }
}

void BattleScreen::playSound(std::int64_t sound) const {
  if (game::story::SoundPort* port = desc_.session->sound(); port != nullptr) {
    port->playSound(0, sound);
  }
}

void BattleScreen::tickVictory(double seconds) {
  if (!victoryPoseSeconds_) {
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
}

// The opening runs at the original's battle rate, a frame each 3 of the screen's
// (BTL_battleTickFrame waits for VSync(3)), and the battle does not run meanwhile.
void BattleScreen::tickIntro(double seconds) {
  introSeconds_ += seconds;
  while (intro_ && introSeconds_ >= IntroFrameSeconds) {
    introSeconds_ -= IntroFrameSeconds;
    const screens::IntroStep before = intro_->current();
    intro_->step();
    const screens::IntroStep after = intro_->current();
    if (after == before) {
      continue;
    }
    game::story::SoundPort* sound = desc_.session->sound();
    if (after == screens::IntroStep::Banner) {
      core::logDebug("[port:battle] BTL_handleBattleIntro (battle_setup.c:226): the banner "
                     "gathers; playSound(0, 0x10)");
      if (sound != nullptr) {
        sound->playSound(0, BannerSound);
      }
    } else if (after == screens::IntroStep::Burst) {
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
  desc_.ui->setSetting(BattlePhaseSetting, std::string{EndPhase});
  ending_ = true;
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
  desc_.ui->hide(BattleDocument);
  for (const game::battle::Drop& drop : std::exchange(drops_, {})) {
    if (desc_.fieldItems != nullptr &&
        !desc_.fieldItems->dropItem(
            map_, drop.item,
            {static_cast<float>(drop.position.x), 0.0F, static_cast<float>(drop.position.z)})) {
      core::logInfo("{}", core::formatMessage(messages::BattleDropLost, drop.item));
    }
  }
  if (session::SessionMusic* music = desc_.session->music(); music != nullptr) {
    music->leaveBattle(std::exchange(won_, false));
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
