// An engine addition: the original has no online play (docs/architecture/multiplayer.md, §6
// decision 3).
#include "coop_battles.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/battle/text_keys.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/screens/bag_view.hpp>
#include <dwe/screens/battle_view.hpp>
#include <dwe/screens/enemy_hp_bars.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "bag_slots.hpp"
#include "coop_gameplay_bridge.hpp"
#include "coop_world.hpp"
#include "fighter_figure.hpp"
#include "partner_menu.hpp"
#include "play_session.hpp"

namespace dwe::app {

namespace {

namespace coop = game::coop;

constexpr std::uint64_t FrameTicks = 3;
constexpr std::uint32_t PlayerThrowClip = 6;
constexpr std::uint32_t ThrowHandNode = 9;
// How often the guest checks its partner's offer for the host's battles (20 times a second at 60
// ticks).
constexpr std::uint64_t OfferTicks = 3;
constexpr float FullTurn = 2.0F * std::numbers::pi_v<float>;
constexpr float YawSteps = 65536.0F;
constexpr float PaceSteps = 16.0F;
// A number the guest's document reads as none (no target picked, no countdown shown).
constexpr double NoTarget = -1.0;
// The node the foes' bars point at (BattleScreen's), and a moved bar's show time in ticks.
constexpr std::uint32_t HudHpBarNode = 1;
constexpr std::uint64_t HudHpBarTicks = 180;

std::uint16_t yawOf(float radians) {
  float turn = std::fmod(radians, FullTurn);
  if (turn < 0.0F) {
    turn += FullTurn;
  }
  return static_cast<std::uint16_t>(
      static_cast<std::uint32_t>(std::lround(turn / FullTurn * YawSteps)) & 0xFFFFU);
}

float radiansOf(std::uint16_t yaw) {
  return static_cast<float>(yaw) / YawSteps * FullTurn;
}

coop::BattleFigure figureOf(const FieldScene::HeldFigure& held) {
  const FighterFigure& figure = held.figure;
  return {.slot = held.npc ? static_cast<std::int16_t>(*held.npc) : coop::PartnerFigureSlot,
          .x = static_cast<std::int32_t>(std::lround(figure.x)),
          .z = static_cast<std::int32_t>(std::lround(figure.z)),
          .yaw = yawOf(figure.yaw),
          .clip = static_cast<std::uint16_t>(figure.clip),
          .starts = figure.starts,
          .frozen = figure.frozen,
          .pace = static_cast<std::uint8_t>(
              std::clamp(std::lround(figure.pace * PaceSteps), 1L, 255L))};
}

FighterFigure fighterOf(const coop::BattleFigure& figure) {
  return {.x = static_cast<float>(figure.x),
          .z = static_cast<float>(figure.z),
          .yaw = radiansOf(figure.yaw),
          .clip = figure.clip,
          .starts = figure.starts,
          .frozen = figure.frozen,
          .pace = static_cast<float>(figure.pace) / PaceSteps};
}

game::story::BattleOutcome storyOutcomeOf(coop::BattleOutcome outcome) {
  switch (outcome) {
  case coop::BattleOutcome::Victory:
    return game::story::BattleOutcome::Victory;
  case coop::BattleOutcome::Fled:
    return game::story::BattleOutcome::Fled;
  case coop::BattleOutcome::Defeat:
    return game::story::BattleOutcome::Defeat;
  }
  return game::story::BattleOutcome::Victory;
}

coop::BattleFighterProfile profileOf(const session::VisitingPartner& partner) {
  coop::BattleFighterProfile profile{
      .species = partner.species,
      .stats = {},
      .hp = partner.hp,
      .mp = partner.mp,
      .chargeMode = static_cast<std::uint8_t>(std::clamp(partner.chargeMode, 0, 255)),
      .equipped = {-1, -1, -1, -1},
      .discipline = partner.condition.discipline,
      .tiredness = partner.condition.tiredness,
      .sick = partner.condition.sick,
      .injured = partner.condition.injured,
      .knownMoves = partner.condition.knownMoves};
  std::ranges::copy(partner.stats.values, profile.stats.begin());
  for (std::size_t place = 0; place < profile.equipped.size() && place < partner.equipped.size();
       ++place) {
    if (partner.equipped[place]) {
      profile.equipped[place] =
          static_cast<std::int8_t>(std::clamp(*partner.equipped[place], 0, 127));
    }
  }
  return profile;
}

session::VisitingPartner visitorOf(const coop::BattleFighterProfile& profile) {
  session::VisitingPartner partner{.species = profile.species,
                                   .stats = {},
                                   .hp = profile.hp,
                                   .mp = profile.mp,
                                   .chargeMode = profile.chargeMode,
                                   .equipped = {},
                                   .condition = {.discipline = profile.discipline,
                                                 .tiredness = profile.tiredness,
                                                 .sick = profile.sick,
                                                 .injured = profile.injured,
                                                 .knownMoves = profile.knownMoves}};
  std::ranges::copy(profile.stats, partner.stats.values.begin());
  for (std::size_t place = 0; place < profile.equipped.size() && place < partner.equipped.size();
       ++place) {
    if (profile.equipped[place] >= 0) {
      partner.equipped[place] = profile.equipped[place];
    }
  }
  return partner;
}

std::string_view phaseName(coop::BattlePhase phase) {
  switch (phase) {
  case coop::BattlePhase::Intro:
    return "intro";
  case coop::BattlePhase::Fight:
    return "fight";
  case coop::BattlePhase::Victory:
    return "victory";
  case coop::BattlePhase::End:
    return "end";
  case coop::BattlePhase::Closing:
    return "closing";
  }
  return "fight";
}

} // namespace

coop::BattleOutcome coopOutcomeOf(game::battle::Outcome outcome) {
  switch (outcome) {
  case game::battle::Outcome::Victory:
    return coop::BattleOutcome::Victory;
  case game::battle::Outcome::Fled:
    return coop::BattleOutcome::Fled;
  case game::battle::Outcome::Defeat:
    return coop::BattleOutcome::Defeat;
  }
  return coop::BattleOutcome::Victory;
}

coop::BattleSpoils spoilsOf(const game::battle::BattleResult& result) {
  coop::BattleSpoils spoils{.hp = result.partnerHp,
                            .mp = result.partnerMp,
                            .statGains = {},
                            .learnedMoves = result.learnedMoves,
                            .injured = result.injured,
                            .care = {result.care.happiness, result.care.discipline,
                                     result.care.tiredness, result.care.battles},
                            .food = result.foodChange,
                            .livesLost = result.livesLost,
                            .elapsedMinutes = result.elapsedMinutes,
                            .hourCrossed = result.hourCrossed,
                            .lifetimeHoursLost = result.lifetimeHoursLost,
                            .departure = static_cast<std::uint8_t>(result.departure),
                            .commandsLearned = {},
                            .mpDiscountLearned = result.mpDiscountLearned,
                            .counters = {result.counters.attacks, result.counters.bigHits,
                                         result.counters.blocks, result.counters.statusesTaken},
                            .anyoneHit = result.counters.anyoneHit,
                            .partnerHit = result.counters.partnerHit,
                            .frames = result.frames};
  std::ranges::copy(result.statGains, spoils.statGains.begin());
  for (const game::battle::MenuEntry entry : result.commandsLearned) {
    spoils.commandsLearned.push_back(static_cast<std::uint8_t>(entry));
  }
  return spoils;
}

game::battle::BattleResult resultOf(const coop::BattleSpoils& spoils, coop::BattleOutcome outcome,
                                    std::string partnerSpecies) {
  game::battle::BattleResult result;
  switch (outcome) {
  case coop::BattleOutcome::Victory:
    result.outcome = game::battle::Outcome::Victory;
    break;
  case coop::BattleOutcome::Fled:
    result.outcome = game::battle::Outcome::Fled;
    break;
  case coop::BattleOutcome::Defeat:
    result.outcome = game::battle::Outcome::Defeat;
    break;
  }
  result.frames = spoils.frames;
  result.partnerHp = spoils.hp;
  result.partnerMp = spoils.mp;
  std::ranges::copy(spoils.statGains, result.statGains.begin());
  result.learnedMoves = spoils.learnedMoves;
  result.injured = spoils.injured;
  for (const std::uint8_t entry : spoils.commandsLearned) {
    if (entry <= static_cast<std::uint8_t>(game::battle::MenuEntry::Techniques)) {
      result.commandsLearned.push_back(static_cast<game::battle::MenuEntry>(entry));
    }
  }
  result.mpDiscountLearned = spoils.mpDiscountLearned;
  result.partnerSpecies = std::move(partnerSpecies);
  result.care = {.happiness = spoils.care[0],
                 .discipline = spoils.care[1],
                 .tiredness = spoils.care[2],
                 .battles = spoils.care[3]};
  result.foodChange = spoils.food;
  result.livesLost = spoils.livesLost;
  result.elapsedMinutes = spoils.elapsedMinutes;
  result.hourCrossed = spoils.hourCrossed;
  result.lifetimeHoursLost = spoils.lifetimeHoursLost;
  if (spoils.departure <= static_cast<std::uint8_t>(game::battle::Departure::KnockedOut)) {
    result.departure = static_cast<game::battle::Departure>(spoils.departure);
  }
  result.counters.attacks = spoils.counters[0];
  result.counters.bigHits = spoils.counters[1];
  result.counters.blocks = spoils.counters[2];
  result.counters.statusesTaken = spoils.counters[3];
  result.counters.anyoneHit = spoils.anyoneHit;
  result.counters.partnerHit = spoils.partnerHit;
  return result;
}

void CoopBattles::tick(PlaySession& play, std::span<const Peer> peers, Role role,
                       coop::PlayerId self, std::span<const coop::PlayerId> here,
                       const LabelOf& labelOf) {
  labelOf_ = labelOf;
  here_.assign(here.begin(), here.end());
  peers_.assign(peers.begin(), peers.end());
  if (play_ != &play) {
    reset();
    play_ = &play;
  }
  ++ticks_;
  CoopGameplayBridge* host =
      role == Role::Guest && !peers_.empty() ? peers_.front().bridge : nullptr;
  // A battle the guest's story starts is shared while the host stands on its map (#351); elsewhere
  // it is a solo battle of the guest's own game.
  const bool handOver = host != nullptr && !play.battleScreen().active() &&
                        coopPlayerHere(play.scene().field(), here_, coop::HostPlayerId);
  play.handBattlesToHost(handOver);
  play.holdCoopBattles(role == Role::Host ? partnersDying(play) : handOver && play.partnerDying());
  if (host != nullptr) {
    if (mirror_.self() != self && !mirror_.running() && !mirror_.pendingAsk()) {
      mirror_ = coop::BattleMirror{self};
    }
    guestTick(play, *host);
    return;
  }
  if (mirror_.running() || mirror_.pendingAsk() || play.heldForCoopBattle() || ownBattle_) {
    hostLeft(&play);
  }
  if (role != Role::Host) {
    peers_.clear();
  }
  hostTick(play);
}

void CoopBattles::reset() {
  referee_ = {};
  mirror_ = coop::BattleMirror{mirror_.self()};
  frames_ = 0;
  asked_.reset();
  ownBattle_ = false;
  allyBattle_ = false;
  watching_ = false;
  shownFrame_.reset();
  fighters_.clear();
  target_.reset();
  purse_.reset();
  actions_.clear();
  cameraOnBattle_ = false;
  play_ = nullptr;
  hideDocument();
}

void CoopBattles::send(CoopGameplayBridge* bridge, coop::Message message) {
  if (bridge == nullptr) {
    return;
  }
  if (auto sent = bridge->sendBattle(std::move(message)); !sent) {
    core::logWarning("coop: could not send a battle message: {}", sent.error().message);
  }
}

void CoopBattles::sendTo(coop::PlayerId to, coop::Message message) {
  if (const auto peer = std::ranges::find(peers_, to, &Peer::id); peer != peers_.end()) {
    send(peer->bridge, std::move(message));
  }
}

void CoopBattles::sendAll(const coop::Message& message) {
  for (const Peer& peer : peers_) {
    send(peer.bridge, message);
  }
}

// ---- The host ----

void CoopBattles::hostTick(PlaySession& play) {
  for (const Peer& peer : peers_) {
    if (peer.bridge == nullptr || !coop::validGuest(peer.id)) {
      continue;
    }
    if (!referee_.present(peer.id)) {
      referee_.guestJoined(peer.id);
    }
    while (auto message = peer.bridge->receiveBattle()) {
      if (const auto* ask = std::get_if<coop::BattleAsk>(&*message)) {
        answer(play, peer.id, *ask);
      } else if (const auto* action = std::get_if<coop::BattleAction>(&*message)) {
        apply(play, peer.id, *action);
      } else if (const auto* offer = std::get_if<coop::BattleOffer>(&*message)) {
        referee_.receive(peer.id, *offer);
      }
    }
  }
  BattleScreen& screen = play.battleScreen();
  // A battle of the host's own began (its story's, a tournament's, the console's).
  if (screen.active() && !referee_.running()) {
    static_cast<void>(screen.takeCoopOutcome());
    static_cast<void>(screen.takeLeftMap());
    sendAll(beginOf(play, coop::HostPlayerId, 0));
  }
  // Each guest's frame shows its own partner's strip, bag and finisher.
  if (referee_.running() && screen.active() && ticks_ % FrameTicks == 0) {
    ++frames_;
    for (const Peer& peer : peers_) {
      send(peer.bridge, frameOf(play, peer.id));
    }
  }
  if (referee_.running() && !screen.active()) {
    endBattle(play);
  }
  screen.setCoopShared(referee_.running() &&
                       std::ranges::any_of(referee_.guests(), [&](coop::PlayerId guest) {
                         return referee_.roleOf(guest) != coop::BattleRole::Helper ||
                                coopPlayerHere(play.scene().field(), here_, guest);
                       }));
  for (const coop::BattleReferee::WaitingAsk& waiting :
       std::vector<coop::BattleReferee::WaitingAsk>{referee_.waiting()}) {
    answer(play, waiting.guest, waiting.ask);
  }
  offerAllies(play);
}

void CoopBattles::endBattle(PlaySession& play) {
  BattleScreen& screen = play.battleScreen();
  const coop::PlayerId starter = referee_.running()->starter;
  const game::battle::Outcome outcome =
      screen.takeCoopOutcome().value_or(game::battle::Outcome::Fled);
  std::vector<std::int16_t> leftMap;
  for (const std::int32_t npc : screen.takeLeftMap()) {
    leftMap.push_back(static_cast<std::int16_t>(npc));
  }
  // Each guest whose partner fought gets its own share back: the starter's as the visitor, the
  // others' as allies.
  std::vector<std::pair<coop::PlayerId, coop::BattleSpoils>> spoils;
  const auto visitor = screen.takeVisitorResult();
  if (visitor) {
    spoils.emplace_back(starter, spoilsOf(*visitor));
  }
  for (const game::battle::AllyResult& ally : screen.takeAllyResults()) {
    if (ally.owner != coop::HostPlayerId) {
      spoils.emplace_back(ally.owner, spoilsOf(ally.result));
    }
  }
  const std::int32_t bits = visitor ? visitor->bits : 0;
  for (coop::BattleReferee::Addressed& end :
       referee_.end(coopOutcomeOf(outcome), std::move(leftMap), bits, spoils)) {
    core::logInfo("coop: battle {} ended for player {}", end.end.battle, end.to);
    sendTo(end.to, std::move(end.end));
  }
  play.scene().setBattlePartner(std::nullopt);
}

// Blocks co-op battles while a partner here dies or waits to be reborn.
bool CoopBattles::partnersDying(const PlaySession& play) const {
  std::vector<coop::PlayerId> here;
  for (const coop::PlayerId guest : referee_.guests()) {
    if (coopPlayerHere(play.scene().field(), here_, guest)) {
      here.push_back(guest);
    }
  }
  return play.partnerDying() || referee_.partnerDying(here);
}

// The guests whose partners join a battle `starter` begins (docs/architecture/coop-combat.md §3):
// every other guest standing on this map and offering it (free of menus, scenes and battles).
std::vector<BattleScreen::CoopAlly> CoopBattles::freeAllies(const PlaySession& play,
                                                            coop::PlayerId starter) const {
  std::vector<BattleScreen::CoopAlly> allies;
  for (const coop::PlayerId guest : referee_.guests()) {
    const auto offered = referee_.offered(guest);
    if (guest != starter && offered && coopPlayerHere(play.scene().field(), here_, guest)) {
      allies.push_back({.owner = guest, .visitor = visitorOf(*offered)});
    }
  }
  return allies;
}

// How the battle scales its enemies (docs/architecture/coop-difficulty.md): the host's
// coop_difficulty, and each guest on this map whose partner does not fight helps.
game::battle::CoopScaling
CoopBattles::scalingOf(const PlaySession& play, coop::PlayerId starter,
                       std::span<const BattleScreen::CoopAlly> allies) const {
  std::int32_t helpers = 0;
  for (const coop::PlayerId guest : referee_.guests()) {
    if (guest != starter && !std::ranges::contains(allies, guest, &BattleScreen::CoopAlly::owner) &&
        coopPlayerHere(play.scene().field(), here_, guest)) {
      ++helpers;
    }
  }
  return {.level = play.coopLevel(), .helpers = helpers};
}

// The partners that join the host's next field battle, and how it scales.
void CoopBattles::offerAllies(PlaySession& play) {
  std::vector<BattleScreen::CoopAlly> allies = freeAllies(play, coop::HostPlayerId);
  BattleScreen& screen = play.battleScreen();
  screen.setCoopScaling(scalingOf(play, coop::HostPlayerId, allies));
  screen.setCoopAllies(std::move(allies));
}

void CoopBattles::guestLeft(coop::PlayerId guest) {
  referee_.guestLeft(guest);
  if (play_ != nullptr) {
    play_->battleScreen().releaseAlly(guest);
  }
}

void CoopBattles::answer(PlaySession& play, coop::PlayerId from, const coop::BattleAsk& ask) {
  const game::field::Field& field = play.scene().field();
  const coop::BattleAskVerdict verdict = referee_.judge(
      ask, {.map = field.mapChangePending() ? std::string_view{} : std::string_view{field.mapId()},
            .guestHere = coopPlayerHere(field, here_, from),
            .battling = play.battleScreen().active(),
            .held = play.coopBattleBlocked() || partnersDying(play) ||
                    referee_.partnerDying(std::array{from})});
  if (verdict.waits) {
    if (referee_.wait(from, ask, ticks_)) {
      return;
    }
    core::logInfo("coop: player {}'s battle {} is refused: this game stayed held", from, ask.ask);
    sendTo(from, coop::BattleRefused{.ask = ask.ask, .reason = coop::BattleRefusal::Busy});
    return;
  }
  referee_.forget(from);
  if (verdict.refusal) {
    core::logInfo("coop: player {}'s battle {} is refused: the world is busy or elsewhere", from,
                  ask.ask);
    sendTo(from, coop::BattleRefused{.ask = ask.ask, .reason = *verdict.refusal});
    return;
  }
  std::vector<std::int32_t> npcs;
  for (const std::int16_t npc : ask.enemyNpcs) {
    npcs.push_back(npc);
  }
  // The host stands on the guest's map and is free: its own partner fights beside the guest's, and
  // so does every other free guest's on this map.
  std::vector<BattleScreen::CoopAlly> allies{
      {.owner = coop::HostPlayerId, .visitor = std::nullopt}};
  std::ranges::move(freeAllies(play, from), std::back_inserter(allies));
  BattleScreen& screen = play.battleScreen();
  screen.setCoopScaling(scalingOf(play, from, allies));
  screen.setCoopAllies(std::move(allies));
  if (auto started = play.startVisitorBattle(from, npcs, visitorOf(ask.fighter), ask.fixedFight,
                                             ask.predefined);
      !started) {
    core::logWarning("coop: player {}'s battle did not start: {}", from, started.error().message);
    sendTo(from, coop::BattleRefused{.ask = ask.ask, .reason = coop::BattleRefusal::Failed});
    return;
  }
  core::logInfo("coop: player {}'s partner fights{}", from,
                screen.fighterOf(coop::HostPlayerId) ? ", this player's beside it"
                                                     : "; this player helps");
  sendAll(beginOf(play, from, ask.ask));
}

void CoopBattles::apply(PlaySession& play, coop::PlayerId from, const coop::BattleAction& action) {
  if (!referee_.allows(from, action)) {
    return;
  }
  BattleScreen& screen = play.battleScreen();
  switch (action.kind) {
  case coop::BattleActionKind::Command:
    static_cast<void>(
        screen.commandFromPlayer(from, static_cast<game::battle::Command>(action.value)));
    break;
  case coop::BattleActionKind::ThrowItem:
    static_cast<void>(screen.throwLentItem(
        from, action.item,
        action.value > 0
            ? std::optional<game::battle::FighterId>{static_cast<game::battle::FighterId>(
                  action.value - 1)}
            : std::nullopt));
    break;
  case coop::BattleActionKind::Cheer:
    screen.cheer(from);
    break;
  case coop::BattleActionKind::Order:
    static_cast<void>(screen.orderFromPlayer(from, action.value));
    break;
  case coop::BattleActionKind::Stance:
    static_cast<void>(
        screen.stanceFromPlayer(from, static_cast<game::battle::Stance>(action.value)));
    break;
  case coop::BattleActionKind::CloseEnd:
    if (screen.coopPhase() == coop::BattlePhase::End) {
      screen.closeEnd();
    }
    break;
  }
}

coop::BattleBegin CoopBattles::beginOf(PlaySession& play, coop::PlayerId starter,
                                       std::uint32_t ask) {
  const BattleScreen& screen = play.battleScreen();
  std::vector<std::int16_t> enemies;
  for (const std::int32_t npc : screen.enemyNpcs()) {
    enemies.push_back(static_cast<std::int16_t>(npc));
  }
  std::vector<std::string> species;
  std::vector<coop::PlayerId> allies;
  bool folder = false;
  if (const game::battle::Battle* battle = screen.battle()) {
    folder = battle->system() == game::battle::BattleSystemId::Folder;
    for (const auto& fighter : battle->state().fighters) {
      species.push_back(fighter.combatant.species);
    }
    for (const game::battle::AllyState& ally : battle->state().allies) {
      allies.push_back(ally.owner);
    }
  }
  frames_ = 0;
  coop::BattleBegin begin =
      referee_.begin(starter, ask, play.scene().field().mapId(), std::move(enemies),
                     std::move(species), screen.fixedFight(), std::move(allies), folder);
  if (const game::battle::Battle* battle = screen.battle()) {
    begin.moves = coopBattleMovesOf(*battle);
  }
  return begin;
}

coop::BattleFrame CoopBattles::frameOf(const PlaySession& play, coop::PlayerId to) {
  const BattleScreen& screen = play.battleScreen();
  // A helper shows the fighting partner's strip, bag, finisher and plate.
  const game::battle::Battle* battle = screen.battle();
  coop::BattleFrame frame =
      battle != nullptr
          ? coopBattleFrameOf(*battle, screen.fighterOf(to).value_or(game::battle::PartnerId))
          : coop::BattleFrame{};
  frame.battle = referee_.running()->battle;
  frame.frame = frames_;
  frame.phase = screen.coopPhase();
  if (const auto& outcome = screen.coopOutcome();
      outcome && frame.phase != coop::BattlePhase::Fight) {
    frame.outcome = coopOutcomeOf(*outcome);
  }
  frame.throws = screen.coopThrows();
  for (const FieldScene::HeldFigure& held : play.scene().heldFigures()) {
    if (frame.figures.size() >= coop::MaxBattleFigures) {
      break;
    }
    frame.figures.push_back(figureOf(held));
  }
  return frame;
}

// ---- The guest ----

void CoopBattles::guestTick(PlaySession& play, CoopGameplayBridge& bridge) {
  while (auto message = bridge.receiveBattle()) {
    std::visit(
        [&](const auto& received) {
          using Received = std::decay_t<decltype(received)>;
          if constexpr (std::is_same_v<Received, coop::BattleBegin> ||
                        std::is_same_v<Received, coop::BattleRefused> ||
                        std::is_same_v<Received, coop::BattleFrame> ||
                        std::is_same_v<Received, coop::BattleEnd>) {
            mirror_.receive(received);
          }
        },
        *message);
  }
  // A battle the guest's own story asked for: the host runs it, or it does not happen (one runs).
  if (auto handed = play.takeHandedBattle()) {
    if (mirror_.running() || mirror_.pendingAsk()) {
      play.finishHandedBattle(game::story::BattleOutcome::Refused);
    } else {
      std::vector<std::int16_t> enemies;
      for (const std::int32_t npc : handed->npcs) {
        enemies.push_back(static_cast<std::int16_t>(npc));
      }
      coop::BattleAsk ask =
          mirror_.ask(play.scene().field().mapId(), std::move(enemies), handed->fixedFight,
                      handed->predefined, profileOf(play.coopFighter()));
      asked_ = ask.ask;
      core::logInfo("coop: asked the host for battle {}", ask.ask);
      send(&bridge, std::move(ask));
    }
  }
  sendOffer(play, bridge);
  if (const auto refused = mirror_.takeRefused()) {
    asked_.reset();
    if (refused->reason == coop::BattleRefusal::Busy) {
      // Another battle runs in the world: this player watches and helps in that one.
      core::logInfo("coop: the host refused battle {}: another one runs", refused->ask);
      play.finishHandedBattle(game::story::BattleOutcome::Refused);
    } else {
      // The host left the map or could not start it: the battle is this game's own, solo.
      core::logInfo("coop: the host refused battle {}: it runs here", refused->ask);
      play.runHandedBattleHere();
    }
  }
  if (const auto began = mirror_.takeBegan()) {
    purse_.reset();
    startShowing(play, *began);
  }
  followPurse(play);
  for (coop::BattleAction& action : std::exchange(actions_, {})) {
    send(&bridge, std::move(action));
  }
  if (mirror_.running() && watching_) {
    showFrame(play);
    stepHud();
  }
  if (const auto ended = mirror_.takeEnded()) {
    if (watching_) {
      stopShowing(play, *ended);
    } else {
      core::logInfo("coop: battle {} ended elsewhere", ended->battle);
      applySpoils(play, *ended);
      allyBattle_ = false;
    }
  }
}

void CoopBattles::followPurse(PlaySession& play) {
  const auto& frame = mirror_.frame();
  if (!frame) {
    return;
  }
  for (const std::string& item : purse_.due(frame->throws, mirror_.self(), frame->frame)) {
    if (!play.takeCoopItem(item)) {
      core::logWarning("coop: the host threw {} for this player, which its bag no longer has",
                       item);
    }
  }
}

// The guest offers its partner for the host's battles while it stands on the host's map, free of
// menus, scenes and battles (docs/architecture/coop-combat.md §3); checked a few times a second,
// sent when it changes.
void CoopBattles::sendOffer(PlaySession& play, CoopGameplayBridge& bridge) {
  if (ticks_ % OfferTicks != 0) {
    return;
  }
  const bool available = mirror_.partnerFree(!play.coopBattleBlocked()) &&
                         !play.heldForCoopBattle() &&
                         coopPlayerHere(play.scene().field(), here_, coop::HostPlayerId);
  if (auto offer = mirror_.offer(available, profileOf(play.coopFighter()), play.partnerDying())) {
    send(&bridge, std::move(*offer));
  }
}

void CoopBattles::startShowing(PlaySession& play, const coop::BattleBegin& begin) {
  const coop::PlayerId self = mirror_.self();
  ownBattle_ = begin.starter == self && asked_ && *asked_ == begin.ask;
  if (ownBattle_) {
    asked_.reset();
  }
  allyBattle_ = std::ranges::contains(begin.allies, self);
  target_.reset();
  // A battle the host started on another map, or while this game runs its own solo battle, is
  // not this player's to watch: its field goes on (its partner, when it joined, still fights).
  watching_ =
      ownBattle_ || (begin.mapId == play.scene().field().mapId() &&
                     !play.scene().field().mapChangePending() && !play.battleScreen().active());
  if (!watching_) {
    core::logInfo("coop: battle {} began on the host away from this player", begin.battle);
    return;
  }
  shownFixedFight_ = begin.fixedFight;
  shownFolder_ = begin.folder;
  hudPartner_ = play.coopBattleFighter();
  core::logInfo("coop: battle {} began on the host; this player {}", begin.battle,
                ownBattle_    ? "commands its partner"
                : allyBattle_ ? "commands its partner beside the host's"
                              : "helps");
  play.holdForCoopBattle(true);
  // The fighting partner is the starter's: this player's own, or the one this scene draws for it.
  play.scene().setBattlePartner(
      begin.starter == self ? std::nullopt : std::optional<game::field::PlayerId>{begin.starter});
  shownFrame_.reset();
  fighters_.clear();
  sounds_ = coopBattleSoundsOf(begin, self);
  play.useCoopBattleSounds(sounds_.enemies, sounds_.partners);
  shownSpecies_ = begin.species;
  glowBodies_.clear();
  const auto bodyOf = [&play](std::optional<std::int32_t> npc, const std::string& species) {
    FieldScene::StatusBody body{.npc = npc, .height = 0, .radius = 0, .conditionFx = std::nullopt};
    if (const assets::DigimonTable* table = play.speciesTable()) {
      if (const assets::DigimonRow* row = table->find(species)) {
        body.height = row->height;
        body.radius = row->radius;
      }
    }
    return body;
  };
  const auto speciesAt = [&begin](std::size_t fighter) {
    return fighter < begin.species.size() ? begin.species[fighter] : std::string{};
  };
  glowBodies_.push_back(bodyOf(std::nullopt, speciesAt(0)));
  for (std::size_t enemy = 0; enemy < begin.enemyNpcs.size(); ++enemy) {
    glowBodies_.push_back(bodyOf(begin.enemyNpcs[enemy], speciesAt(enemy + 1)));
  }
  for (std::size_t ally = 0; ally < begin.allies.size(); ++ally) {
    glowBodies_.push_back(bodyOf(FieldScene::allyFighterSlot(begin.allies[ally]),
                                 speciesAt(1 + begin.enemyNpcs.size() + ally)));
  }
  throws_.reset();
  glows_.emplace();
  publish(play);
}

void CoopBattles::showFrame(PlaySession& play) {
  stepThrowClips(play);
  const auto& frame = mirror_.frame();
  if (!frame || (shownFrame_ && *shownFrame_ == frame->frame)) {
    return;
  }
  const std::uint32_t frames =
      shownFrame_ && frame->frame > *shownFrame_ ? std::min(frame->frame - *shownFrame_, 20U) : 1U;
  shownFrame_ = frame->frame;
  if (!frame->fighters.empty()) {
    fighters_ = frame->fighters;
  }
  for (const coop::BattleFigure& figure : frame->figures) {
    const std::optional<std::int32_t> npc = figure.slot == coop::PartnerFigureSlot
                                                ? std::nullopt
                                                : std::optional<std::int32_t>{figure.slot};
    FighterFigure held = fighterOf(figure);
    held.soundSlot = sounds_.slotOf(figure.slot);
    static_cast<void>(play.scene().holdFighter(npc, held));
  }
  followThrows(play, *frame, frames);
  lookAtBattle(play, *frame);
  publish(play);
}

void CoopBattles::followThrows(PlaySession& play, const coop::BattleFrame& frame,
                               std::uint32_t frames) {
  FieldScene& scene = play.scene();
  const coop::PlayerId self = mirror_.self();
  const CoopThrowFollower::Step step = throws_.follow(frame.throws);
  for (const coop::PlayerId thrower : step.started) {
    const bool playing = thrower == self ? (scene.playPlayerSceneClip(PlayerThrowClip), true)
                                         : scene.playCoopPlayerClip(thrower, PlayerThrowClip);
    if (playing && !std::ranges::contains(throwing_, thrower)) {
      throwing_.push_back(thrower);
    }
  }
  if (!glows_) {
    glows_.emplace();
  }
  for (std::uint32_t passed = 0; passed < frames; ++passed) {
    glows_->tick(glows_->ownRolls());
  }
  for (const coop::BattleThrow& landed : step.landed) {
    const auto figure = std::ranges::find_if(frame.figures, [&](const coop::BattleFigure& shown) {
      return landed.target < glowBodies_.size() &&
             shown.slot == glowBodies_[landed.target].npc.value_or(coop::PartnerFigureSlot);
    });
    if (figure == frame.figures.end()) {
      continue;
    }
    game::battle::Fighter body;
    body.position = {.x = figure->x, .z = figure->z};
    body.combatant.height = glowBodies_[landed.target].height;
    glows_->addItemParticles(landed.target, body);
  }
  scene.setStatusVisuals(glows_, glowBodies_);
  std::optional<FlyingItemSprite> item;
  const session::InventoryAdapter* inventory = play.coopInventory();
  if (step.shown && inventory != nullptr) {
    const std::optional<assets::ItemHandle> handle = inventory->items().handleOf(step.shown->item);
    const std::optional<assets::ItemEntry::Icon> icon =
        handle ? inventory->items().at(*handle).icon() : std::nullopt;
    std::optional<glm::vec3> at;
    if (step.shown->stage == coop::BattleThrowStage::Flight) {
      at = glm::vec3{static_cast<float>(step.shown->x), static_cast<float>(step.shown->height),
                     static_cast<float>(step.shown->z)};
    } else if (step.shown->thrower == self) {
      at = scene.playerNodePoint(ThrowHandNode, glm::vec3{0.0F});
    } else {
      at = scene.coopPlayerNodePoint(step.shown->thrower, ThrowHandNode, glm::vec3{0.0F});
    }
    if (icon && at) {
      item = FlyingItemSprite{.position = *at, .icon = icon->cell, .palette = icon->palette};
    }
  }
  scene.setFlyingItem(item);
}

void CoopBattles::stepThrowClips(PlaySession& play) {
  FieldScene& scene = play.scene();
  const coop::PlayerId self = mirror_.self();
  std::erase_if(throwing_, [&](coop::PlayerId id) {
    if (id == self) {
      if (!scene.playerClipPlayedThrough()) {
        return false;
      }
      scene.playPlayerSceneClip(std::nullopt);
      return true;
    }
    if (!scene.coopPlayerClipPlayedThrough(id)) {
      return false;
    }
    static_cast<void>(scene.playCoopPlayerClip(id, std::nullopt));
    return true;
  });
}

void CoopBattles::stopThrows(PlaySession& play) {
  FieldScene& scene = play.scene();
  for (const coop::PlayerId id : std::exchange(throwing_, {})) {
    if (id == mirror_.self()) {
      scene.playPlayerSceneClip(std::nullopt);
    } else {
      static_cast<void>(scene.playCoopPlayerClip(id, std::nullopt));
    }
  }
  scene.setFlyingItem(std::nullopt);
  scene.setStatusVisuals(std::nullopt, {});
  glows_.reset();
  glowBodies_.clear();
  throws_.reset();
  play.useCoopBattleSounds({}, {});
}

// The helper's camera jumps to the fighting partner once, as the host's own camera stands where
// the battle is; the starter's already looks at it.
void CoopBattles::lookAtBattle(PlaySession& play, const coop::BattleFrame& frame) {
  if (ownBattle_ || allyBattle_ || cameraOnBattle_) {
    return;
  }
  const render::MapCamera* camera = play.scene().camera();
  const auto partner =
      std::ranges::find(frame.figures, coop::PartnerFigureSlot, &coop::BattleFigure::slot);
  if (camera == nullptr || partner == frame.figures.end()) {
    return;
  }
  const float ground = play.scene().field().player().position.y;
  const auto point = camera->projectToBackground(
      {static_cast<float>(partner->x), ground, static_cast<float>(partner->z)});
  if (point) {
    play.scene().setCameraPixel(point->pixel);
    cameraOnBattle_ = true;
  }
}

void CoopBattles::lookBack(PlaySession& play) {
  if (cameraOnBattle_) {
    play.scene().setCameraPixel(std::nullopt);
    cameraOnBattle_ = false;
  }
}

void CoopBattles::stopShowing(PlaySession& play, const coop::BattleEnd& end) {
  stopThrows(play);
  play.scene().releaseFighters(shownFixedFight_);
  lookBack(play);
  for (const std::int16_t npc : end.leftMap) {
    if (npc >= 0) {
      static_cast<void>(play.scene().npcs().takeOffUntilReload(static_cast<std::size_t>(npc)));
    }
  }
  play.holdForCoopBattle(false);
  play.scene().setBattlePartner(std::nullopt);
  applySpoils(play, end);
  if (ownBattle_) {
    play.finishHandedBattle(storyOutcomeOf(end.outcome));
  }
  core::logInfo("coop: battle {} ended", end.battle);
  ownBattle_ = false;
  allyBattle_ = false;
  watching_ = false;
  shownFrame_.reset();
  fighters_.clear();
  target_.reset();
  hideDocument();
}

// What the battle left this player's partner, when it fought (its own battle, or beside the host's
// partner): the host's world kept the bits and the drops.
void CoopBattles::applySpoils(PlaySession& play, const coop::BattleEnd& end) {
  if (!end.spoils || (!ownBattle_ && !allyBattle_)) {
    return;
  }
  if (!coop::spoilsInRange(*end.spoils)) {
    core::logWarning("coop: battle {} left the partner values out of range, not applied",
                     end.battle);
    return;
  }
  if (auto applied = play.applyCoopSpoils(
          resultOf(*end.spoils, end.outcome, std::string{play.partnerSpecies()}));
      !applied) {
    core::logWarning("coop: the battle's result was not applied: {}", applied.error().message);
  }
}

void CoopBattles::hostLeft(PlaySession* play) {
  const coop::BattleMirror::Dropped dropped = mirror_.hostLeft();
  if (play != nullptr) {
    lookBack(*play);
    if ((dropped.battle && watching_) || play->heldForCoopBattle()) {
      stopThrows(*play);
      play->scene().releaseFighters();
      play->holdForCoopBattle(false);
      play->scene().setBattlePartner(std::nullopt);
    }
    if (ownBattle_) {
      // Its battle went with the host's game: the story goes on as if none began.
      play->finishHandedBattle(game::story::BattleOutcome::NotStarted);
    } else if (dropped.ask) {
      // Asked and not begun: this game runs it, solo.
      play->runHandedBattleHere();
    }
  }
  asked_.reset();
  ownBattle_ = false;
  allyBattle_ = false;
  watching_ = false;
  shownFrame_.reset();
  fighters_.clear();
  actions_.clear();
  purse_.reset();
  hideDocument();
}

bool CoopBattles::handle(const ui::UiCommand& command) {
  if (handleHud(command)) {
    return true;
  }
  const auto kind = [&]() -> std::optional<coop::BattleActionKind> {
    if (command.name == CoopBattleOrderCommand) {
      return coop::BattleActionKind::Command;
    }
    if (command.name == CoopBattleCheerCommand) {
      return coop::BattleActionKind::Cheer;
    }
    if (command.name == CoopBattleItemCommand) {
      return coop::BattleActionKind::ThrowItem;
    }
    if (command.name == CoopBattleCloseCommand) {
      return coop::BattleActionKind::CloseEnd;
    }
    return std::nullopt;
  }();
  if (command.name == CoopBattleTargetCommand) {
    pickTarget(command.value);
    return true;
  }
  if (!kind) {
    return false;
  }
  std::int32_t value = 0;
  std::string item;
  if (const auto* number = std::get_if<double>(&command.value)) {
    value = static_cast<std::int32_t>(*number);
  } else if (const auto* text = std::get_if<std::string>(&command.value)) {
    item = *text;
  }
  if (*kind == coop::BattleActionKind::ThrowItem) {
    // Paid from this player's bag once the host throws it (followPurse); the target is the
    // fighter's number + 1, 0 for its own or the fighting one.
    const auto& frame = mirror_.frame();
    PlaySession* play = play_;
    if (play == nullptr || !frame || !frame->bagAvailable || !purse_.canAsk() ||
        !std::ranges::any_of(play->coopThrowableItems(),
                             [&](const auto& stack) { return stack.first == item; })) {
      core::logInfo("coop: the throw of {} is refused here (bag {}, a throw awaits {})", item,
                    frame && frame->bagAvailable ? "free" : "busy", !purse_.canAsk());
      return true;
    }
    purse_.asked(frame->frame);
    value = target_.take();
  }
  if (auto action = mirror_.action(*kind, value, std::move(item))) {
    actions_.push_back(std::move(*action));
  }
  return true;
}

void CoopBattles::press(const input::InputState& input) {
  if (!mirror_.running() || !watching_) {
    return;
  }
  if (hudBag_ && (input.wasPressed("tab_previous") || input.wasPressed("tab_next"))) {
    target_.pick(
        coopNextThrowTarget(*mirror_.running(), mirror_.role().value_or(coop::BattleRole::Helper),
                            mirror_.self(), target_.lit(), input.wasPressed("tab_next") ? 1 : -1));
    if (play_ != nullptr) {
      publish(*play_);
    }
  }
  if (!input.wasPressed("extra")) {
    return;
  }
  // The square button calls this player's partner's finisher, and charges (or cheers for) a
  // finisher under way.
  const auto& frame = mirror_.frame();
  const bool commands = mirror_.role() != coop::BattleRole::Helper;
  const bool calls = commands && !(frame && frame->finisherCharging);
  if (auto action =
          mirror_.action(calls ? coop::BattleActionKind::Command : coop::BattleActionKind::Cheer,
                         calls ? static_cast<std::int32_t>(game::battle::Command::Finisher) : 0)) {
    actions_.push_back(std::move(*action));
  }
}

// ---- The guest's document ----

// The partner the guest's next items fly to: a partner's number picks it, the same again or any
// other number picks none.
void CoopBattles::pickTarget(const ui::UiValue& value) {
  const auto* number = std::get_if<double>(&value);
  const auto& running = mirror_.running();
  if (number == nullptr || !running || *number < 0.0) {
    target_.reset();
  } else {
    const auto index = static_cast<std::size_t>(*number);
    const std::size_t enemies = running->enemyNpcs.size();
    const bool partner = index < fighters_.size() && (index == 0 || index > enemies);
    target_.pick(partner && target_.lit() != index ? std::optional<std::size_t>{index}
                                                   : std::nullopt);
  }
  if (play_ != nullptr) {
    publish(*play_);
  }
}

void CoopBattles::publish(const PlaySession& play) {
  const auto& running = mirror_.running();
  if (!running) {
    return;
  }
  const screens::NameKeys names = play.coopNameKeys();
  const auto& frame = mirror_.frame();
  const std::optional<coop::BattleRole> role = mirror_.role();
  const bool commands = role && *role != coop::BattleRole::Helper;
  const std::string_view roleName = role == coop::BattleRole::Starter ? "starter"
                                    : role == coop::BattleRole::Ally  ? "ally"
                                                                      : "helper";
  // The fighters after the enemies are the allies, in begin.allies' order.
  const std::size_t enemies = running->enemyNpcs.size();
  const auto mine = [&](std::size_t index) {
    if (index == 0) {
      return role == coop::BattleRole::Starter;
    }
    const std::size_t ally = index - 1 - enemies;
    return index > enemies && ally < running->allies.size() &&
           running->allies[ally] == mirror_.self();
  };
  screens::ScreenSettings settings{
      {.name = "coop_battle.shown", .value = true},
      {.name = "coop_battle.role", .value = std::string{roleName}},
      {.name = "coop_battle.phase",
       .value = std::string{phaseName(frame ? frame->phase : coop::BattlePhase::Intro)}},
      {.name = "coop_battle.command", .value = static_cast<double>(frame ? frame->command : 0)},
      {.name = "coop_battle.bag", .value = frame && frame->bagAvailable},
      {.name = "coop_battle.fighter_count", .value = static_cast<double>(fighters_.size())},
      {.name = "coop_battle.target",
       .value = target_.lit() ? static_cast<double>(*target_.lit()) : NoTarget}};
  const std::optional<coop::PlayerId> waitingFor = mirror_.waitingFor();
  const CoopPeerLabel starter = labelOf_ ? labelOf_(running->starter) : CoopPeerLabel{};
  settings.push_back({.name = "coop_battle.waiting", .value = waitingFor.has_value()});
  settings.push_back(
      {.name = "coop_battle.outcome",
       .value = std::string{coopOutcomeName(frame ? frame->outcome : std::nullopt)}});
  settings.push_back({.name = "coop_battle.starter.name", .value = starter.name});
  settings.push_back({.name = "coop_battle.starter.name_key", .value = starter.key});
  settings.push_back(
      {.name = "coop_battle.starter.number", .value = static_cast<double>(starter.number)});
  for (std::size_t index = 0; index < fighters_.size(); ++index) {
    const coop::BattleFighterState& fighter = fighters_[index];
    const std::string species =
        index < running->species.size() ? names.species(running->species[index]) : std::string{};
    settings.push_back({.name = screens::listEntryName("coop_battle.fighter", index, "name_key"),
                        .value = species});
    settings.push_back({.name = screens::listEntryName("coop_battle.fighter", index, "hp"),
                        .value = static_cast<double>(fighter.hp)});
    settings.push_back({.name = screens::listEntryName("coop_battle.fighter", index, "max_hp"),
                        .value = static_cast<double>(fighter.maxHp)});
    settings.push_back({.name = screens::listEntryName("coop_battle.fighter", index, "down"),
                        .value = fighter.down});
    settings.push_back({.name = screens::listEntryName("coop_battle.fighter", index, "mp"),
                        .value = static_cast<double>(fighter.mp)});
    settings.push_back({.name = screens::listEntryName("coop_battle.fighter", index, "max_mp"),
                        .value = static_cast<double>(fighter.maxMp)});
    settings.push_back({.name = screens::listEntryName("coop_battle.fighter", index, "side"),
                        .value = std::string{index == 0 || index > enemies ? "partner" : "enemy"}});
    settings.push_back({.name = screens::listEntryName("coop_battle.fighter", index, "mine"),
                        .value = mine(index)});
    settings.push_back(
        {.name = screens::listEntryName("coop_battle.fighter", index, "revive_seconds"),
         .value =
             fighter.reviveSeconds > 0 ? static_cast<double>(fighter.reviveSeconds) : NoTarget});
  }
  std::size_t orders = 0;
  if (commands && frame) {
    for (unsigned number = 1; number <= static_cast<unsigned>(game::battle::Command::Finisher);
         ++number) {
      if ((frame->menu & (1U << number)) == 0) {
        continue;
      }
      const auto command = static_cast<game::battle::Command>(number);
      settings.push_back({.name = screens::listEntryName("coop_battle.order", orders, "command"),
                          .value = static_cast<double>(number)});
      settings.push_back({.name = screens::listEntryName("coop_battle.order", orders, "label_key"),
                          .value = coopOrderLabelKey(command, shownFolder_)});
      ++orders;
    }
  }
  settings.push_back({.name = "coop_battle.order_count", .value = static_cast<double>(orders)});
  const auto items = play.coopThrowableItems();
  for (std::size_t index = 0; index < items.size(); ++index) {
    settings.push_back({.name = screens::listEntryName("coop_battle.item", index, "id"),
                        .value = items[index].first});
    settings.push_back({.name = screens::listEntryName("coop_battle.item", index, "name_key"),
                        .value = names.item(items[index].first)});
    settings.push_back({.name = screens::listEntryName("coop_battle.item", index, "count"),
                        .value = static_cast<double>(items[index].second)});
  }
  settings.push_back(
      {.name = "coop_battle.item_count", .value = static_cast<double>(items.size())});
  screens::publish(*ui_, settings, CoopBattleRevisionSetting);
  publishHud(play);
  const bool endBox = frame && (frame->phase == coop::BattlePhase::End ||
                                frame->phase == coop::BattlePhase::Closing);
  if (endBox && (!documentShown_ || !ui_->isVisible(CoopBattleDocument))) {
    ui_->show(CoopBattleDocument, true);
    documentShown_ = true;
  } else if (!endBox && documentShown_) {
    documentShown_ = false;
    ui_->hide(CoopBattleDocument);
  }
}

screens::BattleView CoopBattles::hudViewOf(const PlaySession& play) const {
  const coop::BattleRole role = mirror_.role().value_or(coop::BattleRole::Helper);
  return coopBattleViewOf(
      {.begin = &*mirror_.running(),
       .frame = &*mirror_.frame(),
       .fighters = fighters_,
       .role = role,
       .self = mirror_.self(),
       .partner = role != coop::BattleRole::Helper && hudPartner_ ? &*hudPartner_ : nullptr,
       .moves = play.coopMoves(),
       .bagOpen = hudBag_,
       .throwTarget = target_.lit()},
      play.coopNameKeys());
}

void CoopBattles::publishHud(const PlaySession& play) {
  const auto& running = mirror_.running();
  const auto& frame = mirror_.frame();
  const std::optional<std::string_view> phase =
      running && frame ? coopHudPhaseOf(frame->phase) : std::nullopt;
  if (!phase) {
    hideHud();
    return;
  }
  if (!hudShown_) {
    screens::publish(*ui_,
                     screens::settingsOf(screens::BattleIntroView{}, running->enemyNpcs.size()),
                     screens::IntroRevisionSetting);
  }
  screens::publish(*ui_, screens::settingsOf(hudViewOf(play)), screens::BattleRevisionSetting);
  ui_->setSetting(BattlePhaseSetting, std::string{*phase});
  publishHudBars(play);
  if (!hudShown_) {
    hudShown_ = true;
    hudMenu_.open();
    hudBars_.reset();
    ui_->setSetting(BattleActiveSetting, true);
    ui_->show(BattleDocument, true);
  }
}

// Mirrors BattleScreen::publishHpBars.
void CoopBattles::publishHudBars(const PlaySession& play) {
  const auto& running = mirror_.running();
  std::vector<screens::EnemyHpBar> bars;
  const render::MapCamera* camera = play.scene().camera();
  if (running && camera != nullptr && camera->viewportSize().y > 0.0F) {
    const double aspect = static_cast<double>(camera->viewportSize().x) /
                          static_cast<double>(camera->viewportSize().y);
    const ui::UiValue option = ui_->setting(screens::EnemyHpBarsAlwaysSetting);
    const auto* always = std::get_if<bool>(&option);
    const std::size_t enemies = running->enemyNpcs.size();
    hudFoes_.resize(enemies + 1);
    for (std::size_t enemy = 1; enemy <= enemies && enemy < fighters_.size(); ++enemy) {
      const coop::BattleFighterState& foe = fighters_[enemy];
      FoeHp& seen = hudFoes_[enemy];
      if (foe.hp != seen.hp) {
        seen.hitAt = seen.hp != 0 || seen.hitAt ? std::optional{ticks_} : std::nullopt;
        seen.hp = foe.hp;
      }
      const bool recentlyHit = seen.hitAt && ticks_ - *seen.hitAt < HudHpBarTicks;
      if (!screens::enemyHpBarShown({.always = shownFolder_ || (always != nullptr && *always),
                                     .match = false,
                                     .recentlyHit = recentlyHit,
                                     .standing = foe.hp > 0 && !foe.down})) {
        continue;
      }
      const std::optional<glm::vec2> node =
          play.scene().fighterNodeOnWindow(running->enemyNpcs[enemy - 1], HudHpBarNode);
      if (!node) {
        continue;
      }
      bars.push_back(screens::enemyHpBarOf({.enemy = enemy,
                                            .node = {.x = node->x, .y = node->y},
                                            .aspect = aspect,
                                            .hp = foe.hp,
                                            .maxHp = foe.maxHp}));
    }
  }
  screens::publish(*ui_, screens::settingsOf(bars), screens::HpBarsRevisionSetting);
}

void CoopBattles::stepHud() {
  if (!hudShown_) {
    return;
  }
  if (hudBag_ && !ui_->isVisible(PartnerMenuDocument)) {
    hudBag_ = false;
    closeCoopHudBag(*ui_, target_);
    if (play_ != nullptr) {
      publishHud(*play_);
    }
  }
  hudMenu_.step(true);
  hudBars_.step(hudMenu_.active());
  screens::ScreenSettings settings = hudMenu_.settings();
  std::ranges::move(hudBars_.settings(), std::back_inserter(settings));
  screens::publish(*ui_, settings, screens::CommandMenuRevisionSetting);
}

void CoopBattles::hideHud() {
  if (!hudShown_) {
    return;
  }
  hudShown_ = false;
  if (hudBag_) {
    hudBag_ = false;
    closeCoopHudBag(*ui_, target_);
  }
  hudFoes_.clear();
  ui_->setSetting(BattleActiveSetting, false);
  ui_->hide(BattleDocument);
}

bool CoopBattles::handleHud(const ui::UiCommand& command) {
  if (!hudShown_) {
    return false;
  }
  if (command.name == BattleCommandName) {
    return handle({.name = std::string{CoopBattleOrderCommand}, .value = command.value});
  }
  if (command.name == BattleChargeCommand) {
    return handle({.name = std::string{CoopBattleCheerCommand}, .value = {}});
  }
  if (command.name == BattleBagOpenCommand) {
    openHudBag();
    return true;
  }
  if (command.name == BattleBagCloseCommand) {
    ui_->hide(PartnerMenuDocument);
    return true;
  }
  // The bag is closed by the time its Use comes: here the Use throws, never feeds.
  if (command.name == BattleThrowCommand || command.name == BagUseCommand) {
    session::InventoryAdapter* inventory = play_ != nullptr ? play_->coopInventory() : nullptr;
    const std::optional<std::size_t> slot = coopHudThrownSlot(command);
    if (inventory != nullptr && slot) {
      if (const std::optional<assets::ItemHandle> item =
              itemAt(inventory->inventory().slots(), slot)) {
        handle(
            {.name = std::string{CoopBattleItemCommand}, .value = inventory->items().at(*item).id});
      }
    }
    hudBag_ = false;
    closeCoopHudBag(*ui_, target_);
    if (play_ != nullptr) {
      publishHud(*play_);
    }
    return true;
  }
  return command.name == BattleCloseCommand || command.name == BattleEndCountCommand ||
         command.name == BattleEndCountedCommand;
}

void CoopBattles::openHudBag() {
  const auto& frame = mirror_.frame();
  session::InventoryAdapter* inventory = play_ != nullptr ? play_->coopInventory() : nullptr;
  if (!frame || !frame->bagAvailable || inventory == nullptr) {
    return;
  }
  hudBag_ = true;
  target_.reset();
  publishPlayerPoint(*ui_, play_->scene());
  showCoopHudBag(*ui_,
                 screens::settingsOf(screens::bagViewOf(inventory->inventory(), inventory->items(),
                                                        play_->coopNameKeys(),
                                                        game::inventory::UseContext::Battle)),
                 hudViewOf(*play_));
}

void CoopBattles::hideDocument() {
  hideHud();
  if (!documentShown_) {
    return;
  }
  documentShown_ = false;
  ui_->setSetting("coop_battle.shown", false);
  ui_->hide(CoopBattleDocument);
}

} // namespace dwe::app
