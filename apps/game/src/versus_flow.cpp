#include "versus_flow.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/net/signal.hpp>
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
// The arena both machines place the fighters on: no map, the rules' arena radius holds them, the
// host's fighter south and the guest's north, and nobody standing aside.
const session::VersusPlacement Placement{.map = "",
                                         .host = {.x = 0, .z = -300},
                                         .guest = {.x = 0, .z = 300},
                                         .bystander = {.x = 0, .z = -4000}};

// The menu's entries: host, join, back.
constexpr std::int32_t MenuEntries = 3;

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
  series_.reset();
  link_.reset();
  view_ = View::Menu;
  closed_ = false;
  cursor_ = 0;
  typed_.clear();
  badEntry_ = false;
  picks_.clear();
  log_.clear();
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
  errorKey_ = std::move(key);
  errorDetail_ = std::move(detail);
  view_ = View::Error;
  cursor_ = 0;
}

void VersusFlow::startHosting() {
  link_ = desc_.host();
  const game::versus::SeriesSetup setup{.battles = battles(),
                                        .arena = 0,
                                        .seed = desc_.seed(),
                                        .inputDelay = game::versus::DefaultInputDelay};
  series_.emplace(*desc_.battles, game::versus::VersusLink::host(*link_, desc_.identity, setup),
                  Placement);
  core::logInfo("versus: hosting a series of {}", setup.battles);
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
  if (auto started = series_->startNext(); !started) {
    core::logWarning("versus: {}", started.error().message);
    fail("versus.error.lineup", started.error().message);
    return;
  }
  view_ = View::Match;
  cursor_ = 0;
  stalledFrames_ = 0;
  leaveFrames_ = 0;
  log_.clear();
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
    if (cancel) {
      open();
    } else if (confirm) {
      startHosting();
    } else {
      seriesChoice_ = std::clamp(seriesChoice_ + step, 0, std::max(choices - 1, 0));
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
    if (confirm) {
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
  const std::vector<Command> menu = localMenu();
  if (action == "left" || action == "up") {
    moveCursor(-1, static_cast<std::int32_t>(menu.size()));
  } else if (action == "right" || action == "down") {
    moveCursor(1, static_cast<std::int32_t>(menu.size()));
  } else if (action == "confirm" && cursor_ >= 0 &&
             static_cast<std::size_t>(cursor_) < menu.size()) {
    series_->command(menu.at(static_cast<std::size_t>(cursor_)));
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
  if (link.phase() == LinkPhase::Failed) {
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
    startBattle();
  }
  if (ticks_ % TicksPerFrame != 0) {
    return;
  }
  if (view_ == View::Match) {
    leaveFrames_ = std::max(leaveFrames_ - 1, 0);
    const std::int64_t before = series_->battle()->state().frame;
    takeEvents(series_->frame());
    stalledFrames_ = series_->battle()->state().frame == before ? stalledFrames_ + 1 : 0;
    if (series_->roundSettled()) {
      view_ = link.phase() == LinkPhase::Finished ? View::Result : View::Intermission;
      intermissionFrames_ = IntermissionFrames;
    }
  } else if (view_ == View::Intermission) {
    intermissionFrames_ = std::max(intermissionFrames_ - 1, 0);
    if (intermissionFrames_ == 0) {
      startBattle();
    }
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
    add("versus.choice", static_cast<double>(seriesChoice_));
    add("versus.choices", static_cast<double>(choices));
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
    for (game::battle::FighterId id = 0; id < state.fighters.size() && id < 2; ++id) {
      const game::battle::Fighter& fighter = state.fighters.at(id);
      const auto& lineup = link.lineup(id == game::battle::PartnerId ? Side::Host : Side::Guest);
      const std::string prefix = std::format("versus.f.{}.", id);
      add(prefix + "name", lineup && shown < lineup->size() ? lineup->at(shown).name : "");
      add(prefix + "species", names.species(fighter.combatant.species));
      add(prefix + "hp", static_cast<double>(std::max(fighter.hp, 0)));
      add(prefix + "max_hp", static_cast<double>(fighter.combatant.stats.maxHp));
      add(prefix + "mp", static_cast<double>(std::max(fighter.mp, 0)));
      add(prefix + "max_mp", static_cast<double>(fighter.combatant.stats.maxMp));
      add(prefix + "finisher",
          fighter.finisherGoal > 0
              ? std::min(100.0, 100.0 * fighter.finisherProgress / fighter.finisherGoal)
              : 0.0);
      add(prefix + "command", screens::commandLabelKey(state.commandsOf(id).current));
    }
    const std::vector<Command> menu = localMenu();
    const game::battle::Fighter& own = state.fighters.at(series_->localFighter());
    add("versus.menu", static_cast<double>(menu.size()));
    for (std::size_t index = 0; index < menu.size(); ++index) {
      const Command command = menu.at(index);
      std::string key = screens::commandLabelKey(command);
      if (key.empty()) {
        // A technique's or the finisher's entry: the move's name.
        const std::size_t slot =
            command == Command::Finisher
                ? game::battle::FinisherSlot
                : static_cast<std::size_t>(command) - static_cast<std::size_t>(Command::Technique1);
        if (slot < own.moves.size() && own.moves.at(slot)) {
          key = names.move(*own.moves.at(slot));
        }
      }
      add(screens::listEntryName("versus.menu", index, "key"), key);
      add(screens::listEntryName("versus.menu", index, "finisher"), command == Command::Finisher);
    }
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
  return out;
}

} // namespace dwe::app
