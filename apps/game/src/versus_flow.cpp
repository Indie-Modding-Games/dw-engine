#include "versus_flow.hpp"

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/net/signal.hpp>
#include <dwe/screens/battle_view.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/versus_match.hpp>
#include <dwe/ui/ui.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <utility>

namespace dwe::app {

namespace {

using game::battle::Command;
using game::versus::LinkFailure;
using game::versus::LinkPhase;
using game::versus::RoundResult;
using game::versus::Side;

// The game ticks 60 times a second and the battle 20 (battle_main.c: BTL_battleTickFrame).
constexpr std::int64_t TicksPerFrame = 3;
// A second of battle frames with nothing from the other machine shows that it waits for it.
constexpr std::int32_t StalledFrames = 20;
// How long a battle's result stays before the next battle (vs_scene.c: VS_playVersusIntroSequence
// shows the score between battles): 3 seconds of battle frames, or a confirm.
constexpr std::int32_t IntermissionFrames = 60;
// A second cancel within 2 seconds leaves the series.
constexpr std::int32_t LeaveFrames = 40;
// The lines of the battle's log on screen.
constexpr std::size_t LogLines = 4;
// Where both machines place the fighters without an arena's start: no map, the rules' arena
// radius holds them, the host's fighter south and the guest's north, and nobody standing aside.
const session::VersusPlacement Placement{.map = "",
                                         .host = {.x = 0, .z = -300},
                                         .guest = {.x = 0, .z = 300},
                                         .bystander = {.x = 0, .z = -4000}};

// The menu's entries: host, join, back.
constexpr std::int32_t MenuEntries = 3;
// The host's setup rows: the series' length and its arena.
constexpr std::int32_t SetupRows = 2;
// vs_combat.c: VS__tickInput: a move along the command strip plays playSound(0, 2) and a pick
// playSound(0, 3).
constexpr std::int32_t MoveSound = 2;
constexpr std::int32_t PickSound = 3;
// vs_hud.c: VS_tickCommandMenu turns VS_COMMAND_MENU_BLINK every 8 frames.
constexpr std::int64_t BlinkFrames = 8;

std::string_view viewName(VersusFlow::View view) {
  switch (view) {
  case VersusFlow::View::Menu:
    return "menu";
  case VersusFlow::View::HostSetup:
    return "host_setup";
  case VersusFlow::View::Hosting:
    return "hosting";
  case VersusFlow::View::JoinEntry:
    return "join_entry";
  case VersusFlow::View::Joining:
    return "joining";
  case VersusFlow::View::SavePick:
    return "save_pick";
  case VersusFlow::View::Lineup:
    return "lineup";
  case VersusFlow::View::WaitingLineup:
    return "waiting_lineup";
  case VersusFlow::View::Match:
    return "match";
  case VersusFlow::View::Intermission:
    return "intermission";
  case VersusFlow::View::Result:
    return "result";
  case VersusFlow::View::Error:
    return "error";
  }
  return "menu";
}

// The text key that tells the player why the series ended.
std::string failureKey(LinkFailure failure) {
  switch (failure) {
  case LinkFailure::None:
    return "versus.error.unknown";
  case LinkFailure::NoAnswer:
    return "versus.error.no_answer";
  case LinkFailure::Silence:
    return "versus.error.silence";
  case LinkFailure::Left:
    return "versus.error.left";
  case LinkFailure::ProtocolMismatch:
  case LinkFailure::VersionMismatch:
    return "versus.error.version";
  case LinkFailure::ContentMismatch:
    return "versus.error.content";
  case LinkFailure::BadLineup:
    return "versus.error.lineup";
  case LinkFailure::Desync:
    return "versus.error.desync";
  case LinkFailure::Damaged:
    return "versus.error.damaged";
  case LinkFailure::NoServer:
    return "versus.error.no_server";
  case LinkFailure::ServerUnreachable:
    return "versus.error.server_unreachable";
  case LinkFailure::WrongCode:
    return "versus.error.wrong_code";
  case LinkFailure::RoomFull:
    return "versus.error.room_full";
  case LinkFailure::TimedOut:
    return "versus.error.timed_out";
  case LinkFailure::NoRoute:
    return "versus.error.no_route";
  case LinkFailure::Disconnected:
    return "versus.error.disconnected";
  }
  return "versus.error.unknown";
}

// How a battle's result reads for this side.
std::string_view resultFor(RoundResult result, Side side) {
  if (result == RoundResult::BothLost) {
    return "draw";
  }
  const bool hostWon = result == RoundResult::HostWon;
  return hostWon == (side == Side::Host) ? "won" : "lost";
}

// The keyboard's characters a room code has; the code itself is checked on Enter.
bool entryCharacter(char letter) {
  return (letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'z') ||
         (letter >= 'A' && letter <= 'Z');
}

} // namespace

VersusFlow::VersusFlow(VersusFlowDesc desc) : desc_{std::move(desc)} {}

VersusFlow::~VersusFlow() = default;

void VersusFlow::open() {
  // The result comes after the view has already shown the series' end.
  if (series_ && desc_.view != nullptr && view_ != View::Result) {
    desc_.view->battlesOver();
  }
  series_.reset();
  link_.reset();
  view_ = View::Menu;
  closed_ = false;
  cursor_ = 0;
  typed_.clear();
  badEntry_ = false;
  picks_.clear();
  log_.clear();
  score_.reset();
  scoring_ = false;
  confirmed_ = false;
}

std::int32_t VersusFlow::battles() const {
  if (series_ && series_->link().setup()) {
    return series_->link().setup()->battles;
  }
  const auto* rules = desc_.battles != nullptr ? &desc_.battles->rules() : nullptr;
  if (rules == nullptr || !rules->versus || rules->versus->series.empty()) {
    return 1;
  }
  return rules->versus->series.at(static_cast<std::size_t>(seriesChoice_));
}

std::int32_t VersusFlow::arenaOf() const {
  if (series_ && series_->link().setup()) {
    return series_->link().setup()->arena;
  }
  const auto* rules = desc_.battles != nullptr ? &desc_.battles->rules() : nullptr;
  if (rules == nullptr || !rules->versus || rules->versus->arenas.empty()) {
    return 0;
  }
  return rules->versus->arenas.at(static_cast<std::size_t>(arenaChoice_));
}

bool VersusFlow::enoughPartners(const VersusSave& save) const {
  // vs_select.c: VS__tickSelectMode offers a series only when both players registered as many
  // partners as it has battles. Online the host picks the length first, so a save with fewer
  // registered partners than the series' battles cannot be picked.
  return static_cast<std::int32_t>(save.partners.size()) >= battles();
}

void VersusFlow::moveCursor(std::int32_t step, std::int32_t count) {
  if (count > 0) {
    cursor_ = std::clamp(cursor_ + step, 0, count - 1);
  }
}

void VersusFlow::leave() {
  if (series_ && series_->link().phase() != LinkPhase::Failed) {
    series_->link().leave();
  }
  open();
}

void VersusFlow::fail(std::string key, std::string detail) {
  if (desc_.view != nullptr) {
    desc_.view->battlesOver();
  }
  errorKey_ = std::move(key);
  errorDetail_ = std::move(detail);
  view_ = View::Error;
  cursor_ = 0;
}

void VersusFlow::startHosting() {
  link_ = desc_.host();
  const game::versus::SeriesSetup setup{.battles = battles(),
                                        .arena = arenaOf(),
                                        .seed = desc_.seed(),
                                        .inputDelay = game::versus::DefaultInputDelay};
  series_.emplace(*desc_.battles, game::versus::VersusLink::host(*link_, desc_.identity, setup),
                  Placement);
  core::logInfo("versus: hosting a series of {} on arena {}", setup.battles, setup.arena);
  view_ = View::Hosting;
}

void VersusFlow::startJoining() {
  const std::optional<std::string> code = net::normalizedRoomCode(typed_);
  if (!code) {
    badEntry_ = true;
    return;
  }
  link_ = desc_.join(*code);
  series_.emplace(*desc_.battles, game::versus::VersusLink::guest(*link_, desc_.identity),
                  Placement);
  core::logInfo("versus: joining room {}", *code);
  view_ = View::Joining;
}

void VersusFlow::enterSavePick() {
  saves_ = desc_.saves ? desc_.saves() : std::vector<VersusSave>{};
  view_ = View::SavePick;
  cursor_ = 0;
}

void VersusFlow::pickPartner() {
  const std::vector<session::RegisteredPartner>& partners =
      saves_.at(static_cast<std::size_t>(save_)).partners;
  if (cursor_ < 0 || static_cast<std::size_t>(cursor_) >= partners.size()) {
    return;
  }
  // vs_select.c: VS__isAlreadySelected: a partner goes into the lineup once.
  if (std::ranges::contains(picks_, cursor_)) {
    return;
  }
  picks_.push_back(cursor_);
  if (static_cast<std::int32_t>(picks_.size()) < battles()) {
    return;
  }
  std::vector<game::versus::VersusFighter> lineup;
  for (const std::int32_t pick : picks_) {
    lineup.push_back(session::versusFighterOf(partners.at(static_cast<std::size_t>(pick))));
  }
  // The guest's partners are checked by the host's battles too: one this content cannot build
  // ends the series for both (BadLineup) as the first battle starts.
  if (auto sent = series_->link().sendLineup(std::move(lineup)); !sent) {
    fail("versus.error.lineup", sent.error().message);
    return;
  }
  view_ = View::WaitingLineup;
}

void VersusFlow::startBattle() {
  VersusBattleView* view = desc_.view;
  // vs_scene.c: VS_initializeVS: VS_loadVSAssets(VS_BATTLE_SETUP.stage); vs_intro.c:
  // VS__placePlayer1 and VS__placePlayer2 put the fighters at the arena's start.
  const std::int32_t arena = arenaOf();
  if (desc_.placement) {
    if (const std::optional<session::VersusPlacement> placement = desc_.placement(arena)) {
      series_->setPlacement(*placement);
    }
  }
  if (view != nullptr) {
    view->showScore(false, false, std::nullopt);
    view->useArena(arena);
  }
  auto started = series_->startNext([view](game::battle::Battle& battle) {
    if (view != nullptr) {
      view->battleStarted(battle);
    }
  });
  if (!started) {
    core::logWarning("versus: {}", started.error().message);
    fail("versus.error.lineup", started.error().message);
    return;
  }
  view_ = View::Match;
  // vs_combat.c: VS__combatInit: the cursor starts on the last command (your call).
  cursor_ = std::max(static_cast<std::int32_t>(localMenu().size()) - 1, 0);
  confirmed_ = false;
  shownFrames_ = 0;
  stalledFrames_ = 0;
  leaveFrames_ = 0;
  log_.clear();
}

// vs_scene.c: VS_initializeVS: VS_playVersusIntroSequence before the first battle and after each
// one; the arena hides while it shows, and after the last battle the winner's letters show over it.
void VersusFlow::startScore(game::tournament::VersusScoreShow::Moment moment) {
  using Moment = game::tournament::VersusScoreShow::Moment;
  if (!score_) {
    score_.emplace(battles());
  }
  score_->start(moment);
  std::optional<std::size_t> winner;
  if (moment == Moment::Last) {
    if (const auto side = series_->link().series().winner()) {
      winner = *side == Side::Host ? 0 : 1;
    }
  }
  desc_.view->showScore(true, moment == Moment::Last, winner);
  scoring_ = true;
  view_ = View::Intermission;
  cursor_ = 0;
}

std::vector<Command> VersusFlow::localMenu() const {
  if (!series_ || series_->battle() == nullptr) {
    return {};
  }
  return series_->battle()->menuOf(series_->localFighter());
}

void VersusFlow::act(std::string_view action) {
  const bool confirm = action == "confirm";
  const bool cancel = action == "cancel" || action == "menu";
  const std::int32_t step = (action == "up" || action == "left")      ? -1
                            : (action == "down" || action == "right") ? 1
                                                                      : 0;
  if (action.starts_with("pick:")) {
    std::int32_t row = 0;
    const std::string_view number = action.substr(5);
    std::from_chars(number.data(), number.data() + number.size(), row);
    cursor_ = row;
    act("confirm");
    return;
  }
  switch (view_) {
  case View::Menu:
    if (cancel) {
      closed_ = true;
    } else if (confirm) {
      if (cursor_ == 0) {
        view_ = View::HostSetup;
      } else if (cursor_ == 1) {
        view_ = View::JoinEntry;
        typed_.clear();
        badEntry_ = false;
      } else {
        closed_ = true;
      }
      cursor_ = 0;
    } else {
      moveCursor(step, MenuEntries);
    }
    return;
  case View::HostSetup: {
    const auto* rules = desc_.battles != nullptr ? &desc_.battles->rules() : nullptr;
    const auto choices = rules != nullptr && rules->versus
                             ? static_cast<std::int32_t>(rules->versus->series.size())
                             : 1;
    const auto arenas = rules != nullptr && rules->versus
                            ? static_cast<std::int32_t>(rules->versus->arenas.size())
                            : 1;
    const bool across = action == "left" || action == "right";
    if (cancel) {
      open();
    } else if (confirm) {
      startHosting();
    } else if (!across) {
      moveCursor(step, SetupRows);
    } else if (cursor_ == 0) {
      seriesChoice_ = std::clamp(seriesChoice_ + step, 0, std::max(choices - 1, 0));
    } else {
      // vs_select.c: VS__tickSelectMap: left and right go round the stages.
      arenaChoice_ = (arenaChoice_ + step + std::max(arenas, 1)) % std::max(arenas, 1);
    }
    return;
  }
  case View::JoinEntry:
    if (cancel) {
      if (typed_.empty()) {
        open();
      } else {
        typed_.pop_back();
        badEntry_ = false;
      }
    } else if (confirm) {
      startJoining();
    }
    return;
  case View::Hosting:
  case View::Joining:
  case View::WaitingLineup:
    if (cancel) {
      leave();
    }
    return;
  case View::SavePick:
    if (cancel) {
      leave();
    } else if (confirm) {
      if (cursor_ >= 0 && static_cast<std::size_t>(cursor_) < saves_.size() &&
          enoughPartners(saves_.at(static_cast<std::size_t>(cursor_)))) {
        save_ = cursor_;
        picks_.clear();
        view_ = View::Lineup;
        cursor_ = 0;
      }
    } else {
      moveCursor(step, static_cast<std::int32_t>(saves_.size()));
    }
    return;
  case View::Lineup:
    if (cancel) {
      if (picks_.empty()) {
        view_ = View::SavePick;
        cursor_ = save_;
      } else {
        picks_.pop_back();
      }
    } else if (confirm) {
      pickPartner();
    } else {
      moveCursor(step, static_cast<std::int32_t>(
                           saves_.at(static_cast<std::size_t>(save_)).partners.size()));
    }
    return;
  case View::Match:
    actInMatch(action);
    return;
  case View::Intermission:
    // The score screen reads no button (VS_tickVersusIntro); without it a confirm cuts the wait.
    if (confirm && !score_) {
      intermissionFrames_ = 0;
    } else if (cancel) {
      leave();
    }
    return;
  case View::Result:
  case View::Error:
    if (confirm || cancel) {
      leave();
    }
    return;
  }
}

void VersusFlow::actInMatch(std::string_view action) {
  if (action == "cancel" || action == "menu") {
    if (leaveFrames_ > 0) {
      leave();
    } else {
      leaveFrames_ = LeaveFrames;
    }
    return;
  }
  VersusBattleView* view = desc_.view;
  if (view != nullptr && view->presenting()) {
    // The entrance and the banner take the confirm (vs_intro.c: VS__runIntro cuts a fighter's
    // run short); no command goes before the fight.
    confirmed_ = confirmed_ || action == "confirm";
    return;
  }
  const std::vector<Command> menu = localMenu();
  const auto count = static_cast<std::int32_t>(menu.size());
  // vs_combat.c: VS__tickInput: the strip runs right to left from the second command (the first is
  // the finisher, which has its own button): left goes to the next one, right to the one before,
  // and each end goes round to the other.
  if (count > 1 && (action == "left" || action == "right")) {
    const std::int32_t step = action == "left" ? 1 : -1;
    cursor_ = cursor_ + step > count - 1 ? 1 : (cursor_ + step < 1 ? count - 1 : cursor_ + step);
    if (view != nullptr) {
      view->menuSound(MoveSound);
    }
  } else if (action == "confirm" && cursor_ >= 1 && cursor_ < count) {
    series_->command(menu.at(static_cast<std::size_t>(cursor_)));
    if (view != nullptr) {
      view->menuSound(PickSound);
    }
  } else if (action == "finisher" && series_->battle() != nullptr) {
    // The square button calls the finisher once its gauge is full (VS__tickInput).
    const game::battle::Fighter& own =
        series_->battle()->state().fighters.at(series_->localFighter());
    if (own.finisherGoal > 0 && own.finisherProgress >= own.finisherGoal) {
      series_->command(Command::Finisher);
      if (view != nullptr) {
        view->menuSound(PickSound);
      }
    }
  }
}

void VersusFlow::type(std::string_view text, bool enter) {
  if (view_ != View::JoinEntry) {
    return;
  }
  for (const char letter : text) {
    if (entryCharacter(letter) && typed_.size() < net::RoomCodeLength) {
      typed_.push_back(letter);
      badEntry_ = false;
    }
  }
  if (enter) {
    startJoining();
  }
}

void VersusFlow::takeEvents(const game::battle::BattleEvents& events) {
  const session::BattleAdapter& battles = *desc_.battles;
  const screens::NameKeys names{battles.digimonTable(), nullptr, &battles.catalog()};
  const auto& host = series_->link().lineup(Side::Host);
  const auto& guest = series_->link().lineup(Side::Guest);
  const auto round = static_cast<std::size_t>(series_->link().series().round());
  const auto nameOf = [&](game::battle::FighterId id) {
    const auto& lineup = id == game::battle::PartnerId ? host : guest;
    return lineup && round < lineup->size() ? lineup->at(round).name : std::string{};
  };
  for (const game::battle::BattleEvent& event : events) {
    LogLine line{.key = {},
                 .who = nameOf(event.fighter),
                 .what = event.move ? names.move(*event.move) : std::string{},
                 .amount = event.amount};
    switch (event.kind) {
    case game::battle::EventKind::AttackStarted:
      line.key = "versus.log.attack";
      break;
    case game::battle::EventKind::AttackHit:
      line.key = "versus.log.hit";
      break;
    case game::battle::EventKind::FinisherStarted:
      line.key = "versus.log.finisher";
      break;
    case game::battle::EventKind::Disobeyed:
      line.key = "versus.log.disobey";
      break;
    case game::battle::EventKind::Fainted:
      line.key = "versus.log.faint";
      break;
    default:
      continue;
    }
    log_.push_back(std::move(line));
    if (log_.size() > LogLines) {
      log_.pop_front();
    }
  }
}

void VersusFlow::tick(net::Millis now) {
  ++ticks_;
  if (!series_) {
    return;
  }
  series_->poll(now);
  game::versus::VersusLink& link = series_->link();
  // Once the series is decided the other player may leave while this one still shows its end.
  if (link.phase() == LinkPhase::Failed && !link.series().finished()) {
    if (view_ != View::Error && view_ != View::Result) {
      core::logWarning("versus: the series ended: {}", failureKey(link.failure()));
      fail(failureKey(link.failure()));
    }
    return;
  }
  if ((view_ == View::Hosting || view_ == View::Joining) && link.phase() != LinkPhase::Connecting) {
    enterSavePick();
  }
  if (view_ == View::WaitingLineup && link.phase() == LinkPhase::Playing) {
    if (desc_.view != nullptr) {
      // VS_CURRENT_BATTLE -1: the score before the first battle, on the series' arena.
      desc_.view->useArena(arenaOf());
      startScore(game::tournament::VersusScoreShow::Moment::First);
    } else {
      startBattle();
    }
  }
  if (ticks_ % TicksPerFrame != 0) {
    return;
  }
  // A finished battle plays on until both machines agree on its result; its end shows after.
  if (view_ == View::Match && desc_.view != nullptr && desc_.view->presenting() &&
      !series_->battle()->finished()) {
    leaveFrames_ = std::max(leaveFrames_ - 1, 0);
    desc_.view->presentFrame(std::exchange(confirmed_, false));
  } else if (view_ == View::Match) {
    leaveFrames_ = std::max(leaveFrames_ - 1, 0);
    ++shownFrames_;
    const std::int64_t before = series_->battle()->state().frame;
    const game::battle::BattleEvents events = series_->frame();
    takeEvents(events);
    if (desc_.view != nullptr) {
      desc_.view->battleFrame(*series_->battle(), events);
    }
    stalledFrames_ = series_->battle()->state().frame == before ? stalledFrames_ + 1 : 0;
    if (series_->roundSettled()) {
      view_ = link.phase() == LinkPhase::Finished && desc_.view == nullptr ? View::Result
                                                                           : View::Intermission;
      intermissionFrames_ = IntermissionFrames;
    }
  } else if (view_ == View::Intermission) {
    tickPresentation();
  }
}

// Between two battles: without a view, a short wait; with one, the battle's end as the view shows
// it, then the score screen, then the next battle or, after the last, the result.
void VersusFlow::tickPresentation() {
  using Moment = game::tournament::VersusScoreShow::Moment;
  VersusBattleView* view = desc_.view;
  if (view == nullptr) {
    intermissionFrames_ = std::max(intermissionFrames_ - 1, 0);
    if (intermissionFrames_ == 0) {
      startBattle();
    }
    return;
  }
  if (view->presenting()) {
    view->presentFrame(std::exchange(confirmed_, false));
    return;
  }
  if (!scoring_) {
    // The battle's end has shown: the score after it.
    startScore(series_->link().series().finished() ? Moment::Last : Moment::Between);
    return;
  }
  score_->tick();
  if (!score_->done()) {
    return;
  }
  scoring_ = false;
  view->showScore(false, false, std::nullopt);
  if (score_->moment() == Moment::Last) {
    view->battlesOver();
    view_ = View::Result;
  } else {
    startBattle();
  }
}

screens::ScreenSettings VersusFlow::settings() const {
  screens::ScreenSettings out{
      {.name = "versus.view", .value = std::string{viewName(view_)}},
      {.name = "versus.cursor", .value = static_cast<double>(cursor_)},
      {.name = "versus.battles", .value = static_cast<double>(battles())},
  };
  const auto add = [&](std::string name, ui::UiValue value) {
    out.push_back({.name = std::move(name), .value = std::move(value)});
  };
  const session::BattleAdapter* battles = desc_.battles;
  const screens::NameKeys names{battles != nullptr ? battles->digimonTable() : nullptr, nullptr,
                                battles != nullptr ? &battles->catalog() : nullptr};
  switch (view_) {
  case View::HostSetup: {
    const auto* rules = battles != nullptr ? &battles->rules() : nullptr;
    const auto choices = rules != nullptr && rules->versus ? rules->versus->series.size() : 0;
    const auto arenas = rules != nullptr && rules->versus ? rules->versus->arenas.size() : 0;
    add("versus.choice", static_cast<double>(seriesChoice_));
    add("versus.choices", static_cast<double>(choices));
    add("versus.arena", static_cast<double>(arenaOf()));
    add("versus.arena_choice", static_cast<double>(arenaChoice_));
    add("versus.arenas", static_cast<double>(arenas));
    break;
  }
  case View::Hosting:
  case View::Joining: {
    // How far the connection got: the host's room, the guest that arrived, the route.
    const net::PeerLink* link = link_.get();
    const std::string code = link != nullptr ? link->roomCode() : std::string{};
    std::string stage = "connecting";
    if (view_ == View::Hosting && code.empty()) {
      stage = "creating";
    } else if (view_ == View::Hosting && link != nullptr && !link->peerFound()) {
      stage = "waiting";
    }
    add("versus.code", code);
    add("versus.stage", stage);
    break;
  }
  case View::JoinEntry:
    add("versus.typed", typed_);
    add("versus.bad_entry", badEntry_);
    break;
  case View::SavePick:
    add("versus.saves", static_cast<double>(saves_.size()));
    for (std::size_t index = 0; index < saves_.size(); ++index) {
      const VersusSave& save = saves_.at(index);
      add(screens::listEntryName("versus.save", index, "player"), save.player);
      add(screens::listEntryName("versus.save", index, "nickname"), save.nickname);
      add(screens::listEntryName("versus.save", index, "partners"),
          static_cast<double>(save.partners.size()));
      add(screens::listEntryName("versus.save", index, "usable"), enoughPartners(save));
    }
    break;
  case View::Lineup: {
    const auto& partners = saves_.at(static_cast<std::size_t>(save_)).partners;
    add("versus.partners", static_cast<double>(partners.size()));
    for (std::size_t index = 0; index < partners.size(); ++index) {
      const session::RegisteredPartner& partner = partners.at(index);
      add(screens::listEntryName("versus.partner", index, "name"), partner.name);
      add(screens::listEntryName("versus.partner", index, "species"),
          names.species(partner.species));
      add(screens::listEntryName("versus.partner", index, "stats"),
          std::format("{} / {} / {} / {} / {} / {}", partner.hp, partner.mp, partner.offense,
                      partner.defense, partner.speed, partner.brains));
      add(screens::listEntryName("versus.partner", index, "picked"),
          std::ranges::contains(picks_, static_cast<std::int32_t>(index)));
    }
    add("versus.picks", static_cast<double>(picks_.size()));
    for (std::size_t index = 0; index < picks_.size(); ++index) {
      add(screens::listEntryName("versus.pick", index, "name"),
          partners.at(static_cast<std::size_t>(picks_.at(index))).name);
    }
    break;
  }
  case View::Match:
  case View::Intermission:
  case View::Result: {
    if (!series_ || series_->battle() == nullptr) {
      break;
    }
    const game::versus::VersusLink& link = series_->link();
    const game::battle::Battle& battle = *series_->battle();
    const game::battle::BattleState& state = battle.state();
    const Side side = link.side();
    const auto& results = link.series().results();
    const auto round = static_cast<std::size_t>(link.series().round());
    // The battle on screen: the one that runs, or the one just counted.
    const std::size_t shown = view_ == View::Match ? round : (round == 0 ? 0 : round - 1);
    add("versus.local", static_cast<double>(series_->localFighter()));
    add("versus.round", static_cast<double>(shown + 1));
    add("versus.wins.0", static_cast<double>(link.series().wins(Side::Host)));
    add("versus.wins.1", static_cast<double>(link.series().wins(Side::Guest)));
    add("versus.clock",
        static_cast<double>(state.matchClock ? state.matchClock->seconds : std::int32_t{0}));
    add("versus.stalled", stalledFrames_ >= StalledFrames);
    add("versus.leave_asked", leaveFrames_ > 0);
    addHud(out);
    add("versus.log", static_cast<double>(log_.size()));
    for (std::size_t index = 0; index < log_.size(); ++index) {
      const LogLine& line = log_.at(index);
      add(screens::listEntryName("versus.log", index, "key"), line.key);
      add(screens::listEntryName("versus.log", index, "who"), line.who);
      add(screens::listEntryName("versus.log", index, "what"), line.what);
      add(screens::listEntryName("versus.log", index, "amount"), static_cast<double>(line.amount));
    }
    add("versus.results", static_cast<double>(results.size()));
    for (std::size_t index = 0; index < results.size(); ++index) {
      add(screens::listEntryName("versus.result", index, "outcome"),
          std::string{resultFor(results.at(index), side)});
    }
    if (view_ == View::Result) {
      const auto winner = link.series().winner();
      add("versus.winner", std::string{!winner ? "draw" : *winner == side ? "you" : "rival"});
    }
    break;
  }
  case View::Error:
    add("versus.error_key", errorKey_);
    add("versus.error_detail", errorDetail_);
    break;
  case View::Menu:
  case View::WaitingLineup:
    break;
  }
  if (view_ == View::Intermission && scoring_) {
    addScore(out);
  }
  if (desc_.view != nullptr) {
    desc_.view->addSettings(out);
  }
  return out;
}

// vs_hud.c: what each side's HUD shows (VS_renderFighterStatusBars, VS_renderFighterHPBar,
// VS_renderCommandMenu, VS_renderMoveName): the bars and their numbers, the finisher's letters, and
// the command strip with its icons, the hovered one and its name. The rival's hovered command is
// not sent over the network: its strip shows its current command hovered.
void VersusFlow::addHud(screens::ScreenSettings& out) const {
  const auto add = [&](std::string name, ui::UiValue value) {
    out.push_back({.name = std::move(name), .value = std::move(value)});
  };
  const session::BattleAdapter& battles = *desc_.battles;
  const screens::NameKeys names{battles.digimonTable(), nullptr, &battles.catalog()};
  const game::battle::Battle& battle = *series_->battle();
  const game::battle::BattleState& state = battle.state();
  add("versus.blink", (shownFrames_ / BlinkFrames) % 2 == 1);
  for (game::battle::FighterId id = 0; id < state.fighters.size() && id < 2; ++id) {
    const game::battle::Fighter& fighter = state.fighters.at(id);
    const std::string prefix = std::format("versus.f.{}.", id);
    add(prefix + "hp", static_cast<double>(std::max(fighter.hp, 0)));
    add(prefix + "max_hp", static_cast<double>(fighter.combatant.stats.maxHp));
    add(prefix + "mp", static_cast<double>(std::max(fighter.mp, 0)));
    add(prefix + "max_mp", static_cast<double>(fighter.combatant.stats.maxMp));
    // VS_renderFighterHPBar: one letter of "FINISH!!" per sixth of the gauge.
    add(prefix + "segments",
        static_cast<double>(
            fighter.finisherGoal > 0
                ? std::clamp(fighter.finisherProgress * 6 / fighter.finisherGoal, 0, 6)
                : 0));
    add(prefix + "finisher_ready",
        fighter.finisherGoal > 0 && fighter.finisherProgress >= fighter.finisherGoal);
    const std::vector<Command>& menu = battle.menuOf(id);
    const bool local = id == series_->localFighter();
    std::int32_t hovered = static_cast<std::int32_t>(menu.size()) - 1;
    if (local) {
      hovered = cursor_;
    } else if (const auto found = std::ranges::find(menu, state.commandsOf(id).current);
               found != menu.end() && found != menu.begin()) {
      hovered = static_cast<std::int32_t>(std::distance(menu.begin(), found));
    }
    add(prefix + "menu", static_cast<double>(menu.size()));
    add(prefix + "hover", static_cast<double>(hovered));
    for (std::size_t index = 0; index < menu.size(); ++index) {
      add(std::format("{}menu.{}.icon", prefix, index),
          screens::commandIconOf(menu.at(index), fighter, battle.moves()));
    }
    std::string name;
    if (hovered >= 0 && static_cast<std::size_t>(hovered) < menu.size()) {
      const Command command = menu.at(static_cast<std::size_t>(hovered));
      // vs_main.c: VS_COMMAND_NAMES: the versus' own names of the commands ("Auto",
      // "Defensive"); a technique goes by its move's name.
      name = command < Command::Technique1
                 ? std::format("versus.command.{}", static_cast<std::int32_t>(command))
                 : screens::commandLabelOf(command, fighter, names);
    }
    add(prefix + "command_name", name);
  }
}

// vs_scene.c: VS_renderVersusIntro: the score screen's rows (each battle's fighters and result),
// the sliding row, the round's flash and each side's wins and losses.
void VersusFlow::addScore(screens::ScreenSettings& out) const {
  const auto add = [&](std::string name, ui::UiValue value) {
    out.push_back({.name = std::move(name), .value = std::move(value)});
  };
  const game::versus::VersusLink& link = series_->link();
  const auto& results = link.series().results();
  add("versus.score.round", static_cast<double>(score_->round()));
  add("versus.score.offset", static_cast<double>(score_->offset()));
  add("versus.score.flash", score_->flash());
  add("versus.score.black", static_cast<double>(score_->black()));
  add("versus.score.results", static_cast<double>(results.size()));
  for (std::size_t index = 0; index < results.size(); ++index) {
    const RoundResult result = results.at(index);
    add(std::format("versus.score.result.{}", index),
        std::string{result == RoundResult::HostWon    ? "host"
                    : result == RoundResult::GuestWon ? "guest"
                                                      : "draw"});
  }
  const assets::DigimonTable* species = desc_.battles->digimonTable();
  for (const Side side : {Side::Host, Side::Guest}) {
    const std::size_t slot = side == Side::Host ? 0 : 1;
    std::int32_t losses = 0;
    for (const RoundResult result : results) {
      const bool won = (result == RoundResult::HostWon) == (side == Side::Host) &&
                       result != RoundResult::BothLost;
      losses += won ? 0 : 1;
    }
    add(std::format("versus.score.wins.{}", slot), static_cast<double>(link.series().wins(side)));
    add(std::format("versus.score.losses.{}", slot), static_cast<double>(losses));
    // VS_renderFighterNamePlate: each battle's fighter by its face (STDDAT\16TAISEN.TIM, by the
    // species' number).
    const auto& lineup = link.lineup(side);
    for (std::int32_t index = 0; lineup && index < battles(); ++index) {
      std::int32_t number = 0;
      if (static_cast<std::size_t>(index) < lineup->size() && species != nullptr) {
        const std::string& id = lineup->at(static_cast<std::size_t>(index)).species;
        if (const assets::DigimonRow* row = species->find(id)) {
          number = row->index;
        }
      }
      add(std::format("versus.score.face.{}.{}", slot, index), static_cast<double>(number));
    }
  }
}

} // namespace dwe::app
