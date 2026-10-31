#include "bird_transport_screen.hpp"

#include <dwe/assets/map_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/name_keys.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::app {
namespace {

constexpr std::string_view Revision = "bird.revision";
constexpr std::string_view RowList = "bird.row";
// showMapheadSelection(7, 0xfd, 2, ...): yes and no.
constexpr std::int32_t ChoiceCount = 2;
constexpr std::int32_t Yes = 0;
// The player's slot of the name table (script_value.c: getSpeakerName, speaker 0xFD).
constexpr std::size_t PlayerNameSlot = 0;

const char* phaseName(BirdTransportScreen::Phase phase) {
  constexpr std::array<const char*, 6> Names{"closed",  "greeting", "list",
                                             "confirm", "choice",   "farewell"};
  const auto at = static_cast<std::size_t>(std::to_underlying(phase));
  return at < Names.size() ? Names.at(at) : "?";
}

} // namespace

BirdTransportScreen::BirdTransportScreen(BirdTransportScreenDesc desc) : desc_(desc) {}

bool BirdTransportScreen::active() const {
  return phase_ != Phase::Closed;
}

bool BirdTransportScreen::take(const session::StoryScreenRequest& request,
                               const FieldScene& scene) {
  if (request.screen != session::StoryScreen::BirdTransport) {
    return false;
  }
  waiting_ = request.waits;
  travelSection_.reset();
  // tickBirdraTransport reads the carrier from variable 254 (showMapHeadTextbox, setupBoxOrigin):
  // the map's NPC with that script id, by its species.
  carrierSpecies_.clear();
  for (const auto& npc : scene.map().npcs) {
    if (npc.scriptId == request.first && npc.digimon) {
      carrierSpecies_ = *npc.digimon;
    }
  }
  maps_ = scene.mapTable();
  if (desc_.transport == nullptr || !desc_.transport->open()) {
    return true;
  }
  cursor_ = {};
  choice_ = Yes;
  phase_ = Phase::Greeting;
  desc_.ui->show(BirdTransportDocument, true);
  return true;
}

bool BirdTransportScreen::handle(const ui::UiCommand& command) {
  if (phase_ == Phase::Closed) {
    return false;
  }
  if (command.name == "open_menu") {
    // The game's menu does not open over the carrier.
    return true;
  }
  if (command.name != "bird_action") {
    return command.name.starts_with("bird_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    act(*action);
  }
  return true;
}

void BirdTransportScreen::press(const input::InputState& input) {
  if (phase_ != Phase::Closed) {
    jumpHeld_ = input.isDown("walk");
  }
}

void BirdTransportScreen::act(std::string_view action) {
  switch (phase_) {
  case Phase::Closed:
    return;
  case Phase::Greeting:
    // tickConfirmDialogue: only confirm goes on; state 3 opens the money box and the menu.
    if (action == "confirm") {
      phase_ = Phase::List;
    }
    return;
  case Phase::List:
    actInList(action);
    return;
  case Phase::Confirm:
    if (action == "confirm") {
      // State 4: the yes-or-no, on yes.
      choice_ = Yes;
      phase_ = Phase::Choice;
    }
    return;
  case Phase::Choice:
    if (action == "up" || action == "down") {
      // tickSelectionDialogue: the cursor wraps around.
      choice_ = (choice_ + (action == "up" ? ChoiceCount - 1 : 1)) % ChoiceCount;
    } else if (action == "confirm" && choice_ == Yes) {
      // State 5: the boxes close, the money goes and the script moves to the trip's section.
      close(desc_.transport->travel());
    } else if (action == "confirm") {
      // State 6 (SCRIPT_NEXT_STATE_2 5 plus the choice): the farewell.
      phase_ = Phase::Farewell;
    } else if (action == "cancel") {
      // tickSelectionDialogue: cancel answers the last choice, no, unless trigger 0x31 is set.
      if (desc_.transport->choiceCancellable()) {
        phase_ = Phase::Farewell;
      } else {
        refuse();
      }
    }
    return;
  case Phase::Farewell:
    // State 2: the farewell closes the visit; the script goes on after its call.
    if (action == "confirm") {
      close(std::nullopt);
    }
    return;
  }
}

void BirdTransportScreen::actInList(std::string_view action) {
  if (action == "cancel") {
    // State 6: the boxes close and the carrier says goodbye.
    phase_ = Phase::Farewell;
  } else if (action == "up") {
    jumpHeld_ ? static_cast<void>(cursor_.row = 0) : cursor_.up();
  } else if (action == "down") {
    const std::int32_t count = rowCount();
    jumpHeld_ ? static_cast<void>(cursor_.row = count > 0 ? count - 1 : 0) : cursor_.down(count);
  } else if (action == "confirm") {
    // tickBirdraTransportMenu: a place the money pays for goes on to the question; one it does
    // not only sounds (playSound(0, 0xb)).
    if (desc_.transport->pick(static_cast<std::size_t>(cursor_.row))) {
      phase_ = Phase::Confirm;
    } else {
      refuse();
    }
  }
}

void BirdTransportScreen::refuse() {
  if (desc_.sound != nullptr) {
    const session::BirdSound& sound = desc_.transport->rules().refuseSound;
    desc_.sound->playSound(sound.slot, sound.sound);
  }
}

void BirdTransportScreen::close(std::optional<std::int32_t> travelSection) {
  desc_.ui->hide(BirdTransportDocument);
  desc_.transport->close();
  travelSection_ = travelSection;
  phase_ = Phase::Closed;
}

void BirdTransportScreen::update(session::StoryScenes& scenes) {
  if (phase_ != Phase::Closed) {
    screens::publish(*desc_.ui, settings(), Revision);
    return;
  }
  if (waiting_) {
    waiting_ = false;
    std::optional<game::story::SectionRef> jump;
    if (travelSection_) {
      jump = game::story::SectionRef{.script = 0,
                                     .section = static_cast<std::uint16_t>(*travelSection_)};
    }
    travelSection_.reset();
    scenes.screenClosed(jump);
  }
}

std::int32_t BirdTransportScreen::rowCount() const {
  return static_cast<std::int32_t>(desc_.transport->rows().size());
}

std::string BirdTransportScreen::playerName() const {
  if (desc_.session != nullptr) {
    if (const std::string& name = desc_.session->names().tamer; !name.empty()) {
      return name;
    }
  }
  return localization::tr(screens::mainLabelKey(screens::SpeciesNamesBlock, PlayerNameSlot));
}

screens::ScreenSettings BirdTransportScreen::settings() const {
  const session::BirdTransport& transport = *desc_.transport;
  const session::BirdTransportRules& rules = transport.rules();
  const screens::NameKeys names{species_, nullptr, nullptr};
  std::int32_t line = rules.lines.greeting;
  switch (phase_) {
  case Phase::Confirm:
    line = rules.lines.confirm;
    break;
  case Phase::Choice:
    line = rules.lines.choices;
    break;
  case Phase::Farewell:
    line = rules.lines.farewell;
    break;
  case Phase::Closed:
  case Phase::Greeting:
  case Phase::List:
    break;
  }
  screens::ScreenSettings out{
      {.name = "bird.phase", .value = std::string{phaseName(phase_)}},
      // showMapheadSelection(..., 0xfd, ...): the yes-or-no is the player's.
      {.name = "bird.speaker",
       .value = phase_ == Phase::Choice || carrierSpecies_.empty()
                    ? std::string{}
                    : names.species(carrierSpecies_)},
      {.name = "bird.player", .value = phase_ == Phase::Choice ? playerName() : std::string{}},
      {.name = "bird.line_key", .value = transport.lineKey(line)},
      {.name = "bird.bits_key", .value = transport.bitsKey()},
      {.name = "bird.money", .value = static_cast<double>(transport.money())},
      {.name = "bird.choice", .value = static_cast<double>(choice_)},
      {.name = "bird.cursor", .value = static_cast<double>(cursor_.row)},
  };
  const std::vector<session::BirdRow>& rows = transport.rows();
  out.push_back({.name = "bird.total", .value = static_cast<double>(rows.size())});
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const session::BirdDestination& place = rules.destinations[rows[i].destination];
    // calculateBirdramonMenuStrings: MAP_ENTRIES[map].loadingName, then the price.
    std::string nameKey;
    if (maps_ != nullptr) {
      if (const assets::MapEntry* entry = maps_->findByOriginalIndex(place.map)) {
        nameKey = mapNameKeyOf(*maps_, entry->id);
      }
    }
    out.push_back({.name = screens::listEntryName(RowList, i, "name"), .value = nameKey});
    out.push_back({.name = screens::listEntryName(RowList, i, "cost"),
                   .value = static_cast<double>(place.cost)});
    out.push_back(
        {.name = screens::listEntryName(RowList, i, "affordable"), .value = rows[i].affordable});
  }
  return out;
}

} // namespace dwe::app
