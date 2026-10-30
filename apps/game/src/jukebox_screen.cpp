#include "jukebox_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/session_music.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "field_scene.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view Revision = "jukebox.revision";
constexpr std::string_view RowList = "jukebox.row";
// BGM_TRACK_NAMES in main's text.
constexpr std::string_view TrackNamesBlock = "bgm_track_names";

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 4> Names{"closed", "greeting", "list", "farewell"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

} // namespace

JukeboxScreen::JukeboxScreen(JukeboxScreenDesc desc) : desc_(desc) {}

bool JukeboxScreen::active() const {
  return phase_ != Phase::Closed;
}

bool JukeboxScreen::take(const session::StoryScreenRequest& request, const FieldScene& scene) {
  if (request.screen != session::StoryScreen::Jukebox) {
    return false;
  }
  waiting_ = request.waits;
  // setupBoxOrigin and showMapHeadTextbox read the owner from variable 254: the map's NPC with that
  // script id, by its species.
  owner_.clear();
  for (const auto& npc : scene.map().npcs) {
    if (npc.scriptId == request.first && npc.digimon) {
      owner_ = *npc.digimon;
    }
  }
  session::SessionMusic* music = desc_.session != nullptr ? desc_.session->music() : nullptr;
  core::logDebug("[port:jukebox] call_routine 9 (script_interp.c:454): owner script id {}, '{}'",
                 request.first, owner_);
  if (desc_.jukebox == nullptr || music == nullptr || !desc_.jukebox->open(*music)) {
    return true;
  }
  // openJukebox state 0: the greeting first; the list comes after it (state 3).
  list_ = desc_.jukebox->startView(JukeboxVisibleRows);
  answered_ = false;
  phase_ = Phase::Greeting;
  desc_.ui->show(JukeboxDocument, true);
  return true;
}

bool JukeboxScreen::handle(const ui::UiCommand& command) {
  if (phase_ == Phase::Closed) {
    return false;
  }
  if (command.name == "open_menu") {
    // The game's menu does not open over the jukebox.
    return true;
  }
  if (command.name != "jukebox_action") {
    return command.name.starts_with("jukebox_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    act(*action);
  }
  return true;
}

void JukeboxScreen::press(const input::InputState& input) {
  if (phase_ != Phase::Closed) {
    jumpHeld_ = input.isDown("walk");
  }
}

void JukeboxScreen::act(std::string_view action) {
  switch (phase_) {
  case Phase::Closed:
    return;
  case Phase::Greeting:
    // openJukebox state 3, once the greeting is read: createJukeboxMenu and the question.
    if (action == "confirm") {
      phase_ = Phase::List;
    }
    return;
  case Phase::List:
    actInList(action);
    return;
  case Phase::Farewell:
    // openJukebox state 2, once the farewell is read.
    if (action == "confirm") {
      close();
    }
    return;
  }
}

void JukeboxScreen::actInList(std::string_view action) {
  session::Jukebox& jukebox = *desc_.jukebox;
  const auto count = static_cast<std::int32_t>(jukebox.rows().size());
  if (action == "up") {
    jumpHeld_ ? list_.toTop() : static_cast<void>(list_.up());
  } else if (action == "down") {
    jumpHeld_ ? list_.toBottom(count, JukeboxVisibleRows)
              : static_cast<void>(list_.down(count, JukeboxVisibleRows));
  } else if (action == "confirm") {
    // tickJukeboxMenu: the pick goes to variable 249, the owner answers and, with its box up, the
    // song plays (the next tick of the original).
    if (jukebox.choose(list_.index())) {
      answered_ = true;
      jukebox.playChosen();
    }
  } else if (action == "cancel") {
    // openJukebox state 4: the list closes and the owner says goodbye.
    phase_ = Phase::Farewell;
  }
}

void JukeboxScreen::close() {
  desc_.jukebox->close();
  phase_ = Phase::Closed;
  screens::publish(*desc_.ui, settings(), Revision);
  desc_.ui->hide(JukeboxDocument);
}

void JukeboxScreen::update(session::StoryScenes& scenes) {
  if (phase_ != Phase::Closed) {
    screens::publish(*desc_.ui, settings(), Revision);
    return;
  }
  if (waiting_) {
    waiting_ = false;
    scenes.screenClosed();
  }
}

screens::ScreenSettings JukeboxScreen::settings() const {
  const session::Jukebox& jukebox = *desc_.jukebox;
  const session::JukeboxLines& lines = jukebox.rules().lines;
  const screens::NameKeys names{desc_.species, nullptr, nullptr};
  std::int32_t line = lines.greeting;
  if (phase_ == Phase::List) {
    line = answered_ ? lines.chosen : lines.ask;
  } else if (phase_ == Phase::Farewell) {
    line = lines.farewell;
  }
  const std::optional<std::int32_t> chosen = jukebox.chosenTrack();
  const auto& rows = jukebox.rows();
  screens::ScreenSettings out{
      {.name = "jukebox.phase", .value = std::string{phaseName(std::to_underlying(phase_))}},
      {.name = "jukebox.speaker", .value = owner_.empty() ? std::string{} : names.species(owner_)},
      {.name = "jukebox.line_key", .value = jukebox.lineKey(line)},
      {.name = "jukebox.chosen_key",
       .value = chosen ? screens::mainLabelKey(TrackNamesBlock, static_cast<std::size_t>(*chosen))
                       : std::string{}},
      {.name = "jukebox.total", .value = static_cast<double>(rows.size())},
      {.name = "jukebox.top", .value = static_cast<double>(list_.top)},
      {.name = "jukebox.cursor", .value = static_cast<double>(list_.cursor)},
  };
  for (std::int32_t i = 0; i < JukeboxVisibleRows; ++i) {
    const auto at = static_cast<std::size_t>(list_.top + i);
    const std::optional<std::int32_t> track =
        at < rows.size() ? std::optional{rows[at]} : std::nullopt;
    const auto entry = static_cast<std::size_t>(i);
    // calculateMusicMenuStrings: the song's number from 1, its name, and colour 6 for the one that
    // plays.
    out.push_back({.name = screens::listEntryName(RowList, entry, "number"),
                   .value = static_cast<double>(track ? *track + 1 : 0)});
    out.push_back(
        {.name = screens::listEntryName(RowList, entry, "name"),
         .value = track ? screens::mainLabelKey(TrackNamesBlock, static_cast<std::size_t>(*track))
                        : std::string{}});
    out.push_back({.name = screens::listEntryName(RowList, entry, "playing"),
                   .value = track && track == chosen});
  }
  return out;
}

} // namespace dwe::app
