// An engine addition: the original has no online play (docs/architecture/multiplayer.md, §6
// decision 3).
#include "coop_battles.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "coop_gameplay_bridge.hpp"
#include "fighter_figure.hpp"
#include "play_session.hpp"

namespace dwe::app {

namespace {

namespace coop = game::coop;

constexpr std::uint64_t FrameTicks = 3;
constexpr float FullTurn = 2.0F * std::numbers::pi_v<float>;
constexpr float YawSteps = 65536.0F;
constexpr float PaceSteps = 16.0F;

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

// The label of an order on the guest's document: the original's command names, the techniques'
// and the finisher's own words.
std::string orderLabelKey(game::battle::Command command) {
  if (std::string key = screens::commandLabelKey(command); !key.empty()) {
    return key;
  }
  switch (command) {
  case game::battle::Command::Technique1:
    return "battle.command.technique_1";
  case game::battle::Command::Technique2:
    return "battle.command.technique_2";
  case game::battle::Command::Technique3:
    return "battle.command.technique_3";
  case game::battle::Command::Finisher:
    return "battle.command.finisher";
  default:
    return "battle.command.none";
  }
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

// The presence seam (#351): whether the host stands on this guest's map, so a battle the guest's
// story starts is shared; elsewhere it is a solo battle of the guest's own game. It reads the
// host's last snapshot; the co-op core's player registry takes its place once it lands.
bool hostOnThisMap(PlaySession& play, const CoopGameplayBridge& bridge) {
  const game::field::Field& field = play.scene().field();
  const auto world = bridge.presentedWorld(0.0F);
  return world && !field.mapChangePending() && world->mapId == field.mapId();
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
  result.counters.attacks = spoils.counters[0];
  result.counters.bigHits = spoils.counters[1];
  result.counters.blocks = spoils.counters[2];
  result.counters.statusesTaken = spoils.counters[3];
  result.counters.anyoneHit = spoils.anyoneHit;
  result.counters.partnerHit = spoils.partnerHit;
  return result;
}

void CoopBattles::tick(PlaySession& play, CoopGameplayBridge* bridge, Role role) {
  if (play_ != &play) {
    reset();
    play_ = &play;
  }
  ++ticks_;
  play.handBattlesToHost(role == Role::Guest && bridge != nullptr &&
                         !play.battleScreen().active() && hostOnThisMap(play, *bridge));
  if (role == Role::Guest && bridge != nullptr) {
    guestTick(play, *bridge);
    return;
  }
  if (mirror_.running() || mirror_.pendingAsk() || play.heldForCoopBattle() || ownBattle_) {
    hostLeft(&play);
  }
  hostTick(play, role == Role::Host ? bridge : nullptr);
}

void CoopBattles::reset() {
  referee_ = {};
  mirror_ = {};
  frames_ = 0;
  asked_.reset();
  ownBattle_ = false;
  watching_ = false;
  shownFrame_.reset();
  fighters_.clear();
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

// ---- The host ----

void CoopBattles::hostTick(PlaySession& play, CoopGameplayBridge* bridge) {
  if (bridge != nullptr) {
    if (referee_.guestGone()) {
      referee_.guestJoined();
    }
    while (auto message = bridge->receiveBattle()) {
      if (const auto* ask = std::get_if<coop::BattleAsk>(&*message)) {
        answer(play, *bridge, *ask);
      } else if (const auto* action = std::get_if<coop::BattleAction>(&*message)) {
        apply(play, *action);
      }
    }
  }
  BattleScreen& screen = play.battleScreen();
  // A battle of the host's own began (its story's, a tournament's, the console's).
  if (screen.active() && !referee_.running()) {
    static_cast<void>(screen.takeCoopOutcome());
    static_cast<void>(screen.takeLeftMap());
    send(bridge, beginOf(play, coop::HostPlayerId, 0));
  }
  if (referee_.running() && screen.active() && ticks_ % FrameTicks == 0) {
    send(bridge, frameOf(play));
  }
  if (referee_.running() && !screen.active()) {
    const game::battle::Outcome outcome =
        screen.takeCoopOutcome().value_or(game::battle::Outcome::Fled);
    std::vector<std::int16_t> leftMap;
    for (const std::int32_t npc : screen.takeLeftMap()) {
      leftMap.push_back(static_cast<std::int16_t>(npc));
    }
    const auto visitor = screen.takeVisitorResult();
    std::optional<coop::BattleSpoils> spoils;
    if (visitor) {
      spoils = spoilsOf(*visitor);
    }
    const std::int32_t bits = visitor ? visitor->bits : 0;
    if (auto end = referee_.end(coopOutcomeOf(outcome), std::move(leftMap), bits, spoils)) {
      core::logInfo("coop: battle {} ended", end->battle);
      send(bridge, std::move(*end));
    }
    play.scene().setBattlePartner(FieldScene::BattlePartner::Own);
  }
}

void CoopBattles::answer(PlaySession& play, CoopGameplayBridge& bridge,
                         const coop::BattleAsk& ask) {
  if (const auto refusal =
          referee_.judge(ask, play.scene().field().mapId(), play.battleScreen().active())) {
    core::logInfo("coop: the guest's battle {} is refused: the world is busy or elsewhere",
                  ask.ask);
    send(&bridge, coop::BattleRefused{.ask = ask.ask, .reason = *refusal});
    return;
  }
  if (play.coopBattleBlocked()) {
    // A screen, a scene or a script holds this game: the guest's game runs the battle, solo.
    core::logInfo("coop: the guest's battle {} runs on its own game: this one is held", ask.ask);
    send(&bridge, coop::BattleRefused{.ask = ask.ask, .reason = coop::BattleRefusal::Failed});
    return;
  }
  std::vector<std::int32_t> npcs;
  for (const std::int16_t npc : ask.enemyNpcs) {
    npcs.push_back(npc);
  }
  if (auto started =
          play.startVisitorBattle(npcs, visitorOf(ask.fighter), ask.fixedFight, ask.predefined);
      !started) {
    core::logWarning("coop: the guest's battle did not start: {}", started.error().message);
    send(&bridge, coop::BattleRefused{.ask = ask.ask, .reason = coop::BattleRefusal::Failed});
    return;
  }
  core::logInfo("coop: the guest's partner fights; this player helps");
  send(&bridge, beginOf(play, coop::GuestPlayerId, ask.ask));
}

void CoopBattles::apply(PlaySession& play, const coop::BattleAction& action) {
  if (!referee_.allows(action)) {
    return;
  }
  BattleScreen& screen = play.battleScreen();
  switch (action.kind) {
  case coop::BattleActionKind::Command:
    if (screen.visiting()) {
      static_cast<void>(
          screen.commandFromVisitor(static_cast<game::battle::Command>(action.value)));
    }
    break;
  case coop::BattleActionKind::ThrowItem:
    static_cast<void>(screen.throwLentItem(action.item));
    break;
  case coop::BattleActionKind::Cheer:
    screen.cheer();
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
  if (const game::battle::Battle* battle = screen.battle()) {
    for (const auto& fighter : battle->state().fighters) {
      species.push_back(fighter.combatant.species);
    }
  }
  frames_ = 0;
  return referee_.begin(starter, ask, play.scene().field().mapId(), std::move(enemies),
                        std::move(species), screen.fixedFight());
}

coop::BattleFrame CoopBattles::frameOf(const PlaySession& play) {
  const BattleScreen& screen = play.battleScreen();
  coop::BattleFrame frame{.battle = referee_.running()->battle,
                          .frame = ++frames_,
                          .phase = screen.coopPhase(),
                          .command = 0,
                          .menu = 0,
                          .bagAvailable = false,
                          .finisherCharging = false,
                          .fighters = {},
                          .figures = {}};
  if (const game::battle::Battle* battle = screen.battle()) {
    const game::battle::BattleState& state = battle->state();
    frame.command = static_cast<std::uint8_t>(state.commands.current);
    for (const game::battle::Command command : state.commands.menu) {
      frame.menu = static_cast<std::uint16_t>(frame.menu | (1U << static_cast<unsigned>(command)));
    }
    frame.bagAvailable = battle->bagAvailable();
    frame.finisherCharging = state.finisher.charging;
    for (const auto& fighter : state.fighters) {
      if (frame.fighters.size() >= coop::MaxBattleFighters) {
        break;
      }
      frame.fighters.push_back({.hp = fighter.hp,
                                .mp = fighter.mp,
                                .maxHp = fighter.stats.maxHp,
                                .maxMp = fighter.stats.maxMp,
                                .down = fighter.hp <= 0});
    }
  }
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
      play.finishHandedBattle(game::story::BattleOutcome::NotStarted);
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
  if (const auto refused = mirror_.takeRefused()) {
    asked_.reset();
    if (refused->reason == coop::BattleRefusal::Busy) {
      // Another battle runs in the world: this player watches and helps in that one.
      core::logInfo("coop: the host refused battle {}: another one runs", refused->ask);
      play.finishHandedBattle(game::story::BattleOutcome::NotStarted);
    } else {
      // The host left the map or could not start it: the battle is this game's own, solo.
      core::logInfo("coop: the host refused battle {}: it runs here", refused->ask);
      play.runHandedBattleHere();
    }
  }
  if (const auto began = mirror_.takeBegan()) {
    startShowing(play, *began);
  }
  for (coop::BattleAction& action : std::exchange(actions_, {})) {
    send(&bridge, std::move(action));
  }
  if (mirror_.running() && watching_) {
    showFrame(play);
  }
  if (const auto ended = mirror_.takeEnded()) {
    if (watching_) {
      stopShowing(play, *ended);
    } else {
      core::logInfo("coop: battle {} ended elsewhere", ended->battle);
    }
  }
}

void CoopBattles::startShowing(PlaySession& play, const coop::BattleBegin& begin) {
  ownBattle_ = begin.starter == coop::GuestPlayerId && asked_ && *asked_ == begin.ask;
  if (ownBattle_) {
    asked_.reset();
  }
  // A battle the host started on another map, or while this game runs its own solo battle, is
  // not this player's to watch: its field goes on.
  watching_ =
      ownBattle_ || (begin.mapId == play.scene().field().mapId() &&
                     !play.scene().field().mapChangePending() && !play.battleScreen().active());
  if (!watching_) {
    core::logInfo("coop: battle {} began on the host away from this player", begin.battle);
    return;
  }
  shownFixedFight_ = begin.fixedFight;
  core::logInfo("coop: battle {} began on the host; this player {}", begin.battle,
                ownBattle_ ? "commands its partner" : "helps");
  play.holdForCoopBattle(true);
  play.scene().setBattlePartner(ownBattle_ ? FieldScene::BattlePartner::Own
                                           : FieldScene::BattlePartner::CoopHost);
  shownFrame_.reset();
  fighters_.clear();
  publish(play);
}

void CoopBattles::showFrame(PlaySession& play) {
  const auto& frame = mirror_.frame();
  if (!frame || (shownFrame_ && *shownFrame_ == frame->frame)) {
    return;
  }
  shownFrame_ = frame->frame;
  if (!frame->fighters.empty()) {
    fighters_ = frame->fighters;
  }
  for (const coop::BattleFigure& figure : frame->figures) {
    const std::optional<std::int32_t> npc = figure.slot == coop::PartnerFigureSlot
                                                ? std::nullopt
                                                : std::optional<std::int32_t>{figure.slot};
    static_cast<void>(play.scene().holdFighter(npc, fighterOf(figure)));
  }
  lookAtBattle(play, *frame);
  publish(play);
}

// The helper's camera jumps to the fighting partner once, as the host's own camera stands where
// the battle is; the starter's already looks at it.
void CoopBattles::lookAtBattle(PlaySession& play, const coop::BattleFrame& frame) {
  if (ownBattle_ || cameraOnBattle_) {
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
  play.scene().releaseFighters(shownFixedFight_);
  lookBack(play);
  for (const std::int16_t npc : end.leftMap) {
    if (npc >= 0) {
      static_cast<void>(play.scene().npcs().takeOffUntilReload(static_cast<std::size_t>(npc)));
    }
  }
  play.holdForCoopBattle(false);
  play.scene().setBattlePartner(FieldScene::BattlePartner::Own);
  if (ownBattle_) {
    if (end.spoils) {
      if (auto applied = play.applyCoopSpoils(
              resultOf(*end.spoils, end.outcome, std::string{play.partnerSpecies()}));
          !applied) {
        core::logWarning("coop: the battle's result was not applied: {}", applied.error().message);
      }
    }
    play.finishHandedBattle(storyOutcomeOf(end.outcome));
  }
  core::logInfo("coop: battle {} ended", end.battle);
  ownBattle_ = false;
  watching_ = false;
  shownFrame_.reset();
  fighters_.clear();
  hideDocument();
}

void CoopBattles::hostLeft(PlaySession* play) {
  const coop::BattleMirror::Dropped dropped = mirror_.hostLeft();
  if (play != nullptr) {
    lookBack(*play);
    if ((dropped.battle && watching_) || play->heldForCoopBattle()) {
      play->scene().releaseFighters();
      play->holdForCoopBattle(false);
      play->scene().setBattlePartner(FieldScene::BattlePartner::Own);
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
  watching_ = false;
  shownFrame_.reset();
  fighters_.clear();
  actions_.clear();
  hideDocument();
}

bool CoopBattles::handle(const ui::UiCommand& command) {
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
    // The item comes out of this player's own bag; the host's lends one to throw.
    const auto& frame = mirror_.frame();
    PlaySession* play = play_;
    if (play == nullptr || !frame || !frame->bagAvailable || !play->takeCoopItem(item)) {
      return true;
    }
  }
  if (auto action = mirror_.action(*kind, value, std::move(item))) {
    actions_.push_back(std::move(*action));
  }
  return true;
}

void CoopBattles::press(const input::InputState& input) {
  if (!mirror_.running() || !watching_ || !input.wasPressed("extra")) {
    return;
  }
  const bool starter = mirror_.role() == coop::BattleRole::Starter;
  if (auto action = mirror_.action(
          starter ? coop::BattleActionKind::Command : coop::BattleActionKind::Cheer,
          starter ? static_cast<std::int32_t>(game::battle::Command::Finisher) : 0)) {
    actions_.push_back(std::move(*action));
  }
}

// ---- The guest's document ----

void CoopBattles::publish(const PlaySession& play) {
  const auto& running = mirror_.running();
  if (!running) {
    return;
  }
  const screens::NameKeys names = play.coopNameKeys();
  const auto& frame = mirror_.frame();
  const bool starter = mirror_.role() == coop::BattleRole::Starter;
  screens::ScreenSettings settings{
      {.name = "coop_battle.shown", .value = true},
      {.name = "coop_battle.role", .value = std::string{starter ? "starter" : "helper"}},
      {.name = "coop_battle.phase",
       .value = std::string{phaseName(frame ? frame->phase : coop::BattlePhase::Intro)}},
      {.name = "coop_battle.command", .value = static_cast<double>(frame ? frame->command : 0)},
      {.name = "coop_battle.bag", .value = frame && frame->bagAvailable},
      {.name = "coop_battle.fighter_count", .value = static_cast<double>(fighters_.size())}};
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
  }
  std::size_t orders = 0;
  if (starter && frame) {
    for (unsigned number = 1; number <= static_cast<unsigned>(game::battle::Command::Finisher);
         ++number) {
      if ((frame->menu & (1U << number)) == 0) {
        continue;
      }
      const auto command = static_cast<game::battle::Command>(number);
      settings.push_back({.name = screens::listEntryName("coop_battle.order", orders, "command"),
                          .value = static_cast<double>(number)});
      settings.push_back({.name = screens::listEntryName("coop_battle.order", orders, "label_key"),
                          .value = orderLabelKey(command)});
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
  if (!documentShown_ || !ui_->isVisible(CoopBattleDocument)) {
    ui_->show(CoopBattleDocument, true);
    documentShown_ = true;
  }
}

void CoopBattles::hideDocument() {
  if (!documentShown_) {
    return;
  }
  documentShown_ = false;
  ui_->setSetting("coop_battle.shown", false);
  ui_->hide(CoopBattleDocument);
}

} // namespace dwe::app
