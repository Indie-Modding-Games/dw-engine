#include "tournament_schedule_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/partner_view.hpp>

#include <array>
#include <format>
#include <utility>
#include <variant>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

constexpr std::string_view Revision = "tournament_schedule.revision";
constexpr std::string_view ActionCommand = "tournament_schedule_action";
constexpr std::string_view Prefix = "tournament_schedule.";
// The names the cups' texts and the questions fill ({tournament:pstat=3}: tournament_names).
constexpr std::string_view TournamentNamesBlock = "tournament_names";
constexpr std::string_view PartnerArgument = "partner";
constexpr std::string_view TamerArgument = "tamer";
// The player's name is the species table's first name until the game names the player.
constexpr std::size_t PlayerNameSlot = 0;
// The choice's two options (showMapheadSelection(4, 0xfd, 2, ...)): enter, or not.
constexpr std::size_t ChoiceOptions = 2;
constexpr std::size_t DontEnter = 1;

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 5> Names{"closed", "schedule", "peek", "question", "choice"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

std::string setting(std::string_view name) {
  return std::format("{}{}", Prefix, name);
}

// The rows of a box as settings: <box>.rows, <box>.<r>.runs and <box>.<r>.run.<k>.text/.color.
void addRows(screens::ScreenSettings& out, std::string_view box, const std::vector<TextRow>& rows) {
  out.push_back(
      {.name = setting(std::format("{}.rows", box)), .value = static_cast<double>(rows.size())});
  for (std::size_t row = 0; row < rows.size(); ++row) {
    const TextRow& runs = rows[row];
    out.push_back({.name = setting(std::format("{}.{}.runs", box, row)),
                   .value = static_cast<double>(runs.size())});
    for (std::size_t run = 0; run < runs.size(); ++run) {
      out.push_back({.name = setting(std::format("{}.{}.run.{}.text", box, row, run)),
                     .value = runs[run].text});
      out.push_back({.name = setting(std::format("{}.{}.run.{}.color", box, row, run)),
                     .value = static_cast<double>(runs[run].color)});
    }
  }
}

} // namespace

bool TournamentScheduleScreen::take(const session::StoryScreenRequest& request,
                                    const FieldScene& scene) {
  if (request.screen != session::StoryScreen::TournamentSchedule) {
    return false;
  }
  waiting_ = request.waits;
  // setupBoxOrigin and showMapHeadTextbox read the receptionist from variable 254: the map's NPC
  // with that script id, by its species.
  owner_.clear();
  for (const auto& npc : scene.map().npcs) {
    if (npc.scriptId == request.first && npc.digimon) {
      owner_ = *npc.digimon;
    }
  }
  session::TournamentAdapter* tournament = desc_.tournament;
  if (tournament == nullptr || desc_.session == nullptr || tournament->scheduleRules() == nullptr) {
    core::logWarning("[port:tournament] initTournamentSchedule (dget.c:664): no registration "
                     "rules; the routine ends at once");
    return true;
  }
  // State 0: a partner registered already ends the routine at once.
  if (tournament->registered()) {
    core::logDebug("[port:tournament] initTournamentSchedule (dget.c:673): registered already");
    return true;
  }
  auto opened = tournament->openSchedule(*desc_.session);
  if (!opened) {
    core::logWarning("[port:tournament] initTournamentSchedule (dget.c:679): {}; the routine "
                     "ends at once",
                     opened.error().message);
    return true;
  }
  schedule_ = *opened;
  // buildScheduleEntries: the cursor starts on today's first row.
  column_ = 0;
  row_ = 0;
  phase_ = Phase::Schedule;
  desc_.ui->show(TournamentScheduleDocument, true);
  return true;
}

bool TournamentScheduleScreen::handle(const ui::UiCommand& command) {
  if (phase_ == Phase::Closed) {
    return false;
  }
  if (command.name == "open_menu") {
    return true;
  }
  if (command.name != ActionCommand) {
    return command.name.starts_with("tournament_schedule_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    act(*action);
  }
  return true;
}

void TournamentScheduleScreen::press(const input::InputState& input) {
  if (!input.wasPressed("pause")) {
    return;
  }
  // tickTournamentSchedule: Start shows the cup under the cursor (initTournamentInfo(0));
  // tickTournamentInfo: Start closes it again.
  if (phase_ == Phase::Schedule) {
    if (cell()) {
      phase_ = Phase::Peek;
    } else {
      sound(desc_.tournament->scheduleRules()->refuseSound);
    }
  } else if (phase_ == Phase::Peek) {
    phase_ = Phase::Schedule;
  }
}

const std::optional<game::tournament::ScheduleCell>& TournamentScheduleScreen::cell() const {
  return schedule_.days.at(column_).at(row_);
}

void TournamentScheduleScreen::act(std::string_view action) {
  switch (phase_) {
  case Phase::Closed:
    return;
  case Phase::Schedule:
    actInSchedule(action);
    return;
  case Phase::Peek:
    // tickTournamentInfo: Start, confirm or cancel close the cup's box.
    if (action == "confirm" || action == "cancel") {
      phase_ = Phase::Schedule;
    }
    return;
  case Phase::Question:
    // State 3's box waits for confirm, then state 4 asks.
    if (action == "confirm") {
      choice_ = 0;
      phase_ = Phase::Choice;
    }
    return;
  case Phase::Choice:
    if (action == "up" || action == "down") {
      choice_ = (choice_ + 1) % ChoiceOptions;
    } else if (action == "confirm") {
      answer(choice_);
    } else if (action == "cancel") {
      // Assumed: cancel takes the last option, as the story's selections do.
      answer(DontEnter);
    }
    return;
  }
}

void TournamentScheduleScreen::actInSchedule(std::string_view action) {
  const auto wrap = [](std::size_t at, std::size_t count, bool forward) {
    return forward ? (at + 1) % count : (at + count - 1) % count;
  };
  if (action == "cancel") {
    // State 3 with no cup picked: the routine ends.
    core::logDebug("[port:tournament] tickTournamentSchedule (dget.c:478): closed with no cup");
    close();
  } else if (action == "confirm") {
    if (cell() && cell()->enterable()) {
      pick();
    } else {
      sound(desc_.tournament->scheduleRules()->refuseSound);
    }
  } else if (action == "left" || action == "right") {
    column_ = wrap(column_, game::tournament::ScheduleDays, action == "right");
  } else if (action == "up" || action == "down") {
    row_ = wrap(row_, game::tournament::ScheduleRows, action == "down");
  }
}

// tickTournamentSchedule: the cup goes to variable 3; state 3 closes the schedule, shows the cup's
// text at the top and asks, the overlevel question when the partner is too strong for it.
void TournamentScheduleScreen::pick() {
  session::TournamentAdapter& tournament = *desc_.tournament;
  cup_ = cell()->cup;
  overlevel_ = cell()->overlevel;
  if (auto chosen = tournament.chooseCup(cup_); !chosen) {
    core::logWarning("[port:tournament] tickTournamentSchedule (dget.c:489): {}",
                     chosen.error().message);
  }
  if (auto marked = tournament.markOverlevel(overlevel_); !marked) {
    core::logWarning("[port:tournament] initTournamentSchedule (dget.c:705): {}",
                     marked.error().message);
  }
  core::logDebug("[port:tournament] tickTournamentSchedule (dget.c:489): cup {} on day column {}{}",
                 cup_, column_, overlevel_ ? ", overlevel" : "");
  phase_ = Phase::Question;
}

// States 5 and 6: entered, or not.
void TournamentScheduleScreen::answer(std::size_t option) {
  if (option != DontEnter) {
    if (auto entered = desc_.tournament->enter(*desc_.session, column_); !entered) {
      core::logWarning("[port:tournament] initTournamentSchedule (dget.c:722): {}",
                       entered.error().message);
    }
  }
  close();
}

void TournamentScheduleScreen::close() {
  phase_ = Phase::Closed;
  screens::publish(*desc_.ui, settings(), Revision);
  desc_.ui->hide(TournamentScheduleDocument);
}

void TournamentScheduleScreen::sound(std::int32_t sound) const {
  game::story::SoundPort* port = desc_.session != nullptr ? desc_.session->sound() : nullptr;
  if (port != nullptr) {
    port->playSound(desc_.tournament->scheduleRules()->soundSlot, sound);
  }
}

void TournamentScheduleScreen::update(session::StoryScenes& scenes) {
  if (phase_ != Phase::Closed) {
    screens::publish(*desc_.ui, settings(), Revision);
    return;
  }
  if (waiting_) {
    waiting_ = false;
    scenes.screenClosed();
  }
}

std::string TournamentScheduleScreen::partnerName() const {
  if (std::string name = desc_.session->partnerView().nickname; !name.empty()) {
    return name;
  }
  const screens::NameKeys names{desc_.species, nullptr, nullptr};
  return localization::tr(names.species(desc_.session->partnerView().species));
}

std::vector<TextRow> TournamentScheduleScreen::infoRows() const {
  std::vector<TextRow> rows;
  const std::string key =
      desc_.tournament->cupTextKey(phase_ == Phase::Peek && cell() ? cell()->cup : cup_);
  if (!key.empty()) {
    appendPageRows(rows, localization::tr(key, colorArguments()));
  }
  return rows;
}

std::vector<TextRow> TournamentScheduleScreen::talkRows() const {
  std::vector<TextRow> rows;
  const game::tournament::ScheduleRules& rules = *desc_.tournament->scheduleRules();
  const game::story::DialogueRules* dialogue =
      desc_.story != nullptr ? &desc_.story->dialogue().rules() : nullptr;
  std::string tamer = desc_.session->names().tamer;
  if (tamer.empty()) {
    tamer = localization::tr(screens::mainLabelKey(screens::SpeciesNamesBlock, PlayerNameSlot));
  }
  // showMapHeadTextbox(2 or 3, pstat 254): the receptionist speaks; showMapheadSelection(4, 0xFD):
  // the player answers.
  const bool asking = phase_ == Phase::Question;
  std::string speaker;
  std::int64_t color = PageColor;
  if (asking) {
    if (!owner_.empty()) {
      const screens::NameKeys names{desc_.species, nullptr, nullptr};
      speaker = localization::tr(names.species(owner_));
    }
    color = dialogue != nullptr ? dialogue->characterNameColor : PageColor;
  } else {
    speaker = tamer;
    color = dialogue != nullptr ? dialogue->playerNameColor : PageColor;
  }
  if (!speaker.empty()) {
    rows.push_back({TextRun{.text = std::move(speaker), .color = color}});
  }
  const std::int32_t line =
      asking ? (overlevel_ ? rules.askOverlevelLine : rules.askLine) : rules.choiceLine;
  const std::string key = desc_.tournament->lineKey(line);
  if (key.empty()) {
    return rows;
  }
  std::vector<localization::TextArgument> arguments = colorArguments();
  const std::string tournamentTag =
      std::format("tournament:pstat={}", desc_.tournament->cupVariable());
  arguments.push_back({.name = PartnerArgument, .value = partnerName()});
  arguments.push_back({.name = TamerArgument, .value = tamer});
  arguments.push_back({.name = tournamentTag,
                       .value = localization::tr(screens::mainLabelKey(
                           TournamentNamesBlock, static_cast<std::size_t>(cup_)))});
  // The three names end in NameEnd, for the space after an inserted name (story_text_rows.hpp).
  for (auto argument = arguments.end() - 3; argument != arguments.end(); ++argument) {
    argument->value = endedName(argument->value);
  }
  appendPageRows(rows, localization::tr(key, arguments), spacesAfterNames(*desc_.ui));
  return rows;
}

screens::ScreenSettings TournamentScheduleScreen::settings() const {
  screens::ScreenSettings out{
      {.name = setting("phase"), .value = std::string{phaseName(std::to_underlying(phase_))}},
  };
  if (phase_ == Phase::Closed) {
    return out;
  }
  const game::tournament::ScheduleRules& rules = *desc_.tournament->scheduleRules();
  out.push_back({.name = setting("title"),
                 .value = localization::tr(desc_.tournament->lineKey(rules.titleLine))});
  out.push_back({.name = setting("column"), .value = static_cast<double>(column_)});
  out.push_back({.name = setting("row"), .value = static_cast<double>(row_)});
  for (std::size_t column = 0; column < game::tournament::ScheduleDays; ++column) {
    // buildScheduleLabels: each day of the cycle from 1.
    const std::int32_t day =
        ((schedule_.firstDay + static_cast<std::int32_t>(column)) % rules.cycleDays) + 1;
    out.push_back({.name = setting(std::format("day.{}", column)), .value = std::to_string(day)});
    for (std::size_t row = 0; row < game::tournament::ScheduleRows; ++row) {
      const std::optional<game::tournament::ScheduleCell>& entry = schedule_.days[column][row];
      // buildScheduleEntries: colour 3 for a cup that cannot be entered, 7 for an overlevel one.
      std::string state;
      if (entry) {
        state = !entry->enterable() ? "closed" : (entry->overlevel ? "overlevel" : "open");
      }
      out.push_back({.name = setting(std::format("cell.{}.{}.grade", column, row)),
                     .value = entry ? desc_.tournament->grade(entry->cup) : std::string{}});
      out.push_back({.name = setting(std::format("cell.{}.{}.state", column, row)),
                     .value = std::move(state)});
    }
  }
  const bool infoShown = phase_ != Phase::Schedule;
  addRows(out, "info", infoShown ? infoRows() : std::vector<TextRow>{});
  const bool talking = phase_ == Phase::Question || phase_ == Phase::Choice;
  const std::vector<TextRow> talk = talking ? talkRows() : std::vector<TextRow>{};
  addRows(out, "talk", talk);
  // The selection's cursor, under the speaker's row.
  out.push_back({.name = setting("talk_cursor"),
                 .value = phase_ == Phase::Choice ? static_cast<double>(1 + choice_) : -1.0});
  return out;
}

} // namespace dwe::app
