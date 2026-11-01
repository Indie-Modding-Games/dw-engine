#include "trade_screen.hpp"

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

constexpr std::string_view Revision = "trade.revision";
constexpr std::string_view RowList = "trade.row";
// The column titles the original draws over the menu (renderMojyaTradeMenu: MAIN_D_801307CC).
constexpr std::string_view LabelBlock = "script_menu_trade_label";
// showMapheadSelection(0xc, 0xfd, 2, ...): yes and no.
constexpr std::int32_t ChoiceCount = 2;
constexpr std::int32_t Yes = 0;
// The player's slot of the name table (script_value.c: getSpeakerName, speaker 0xFD).
constexpr std::size_t PlayerNameSlot = 0;

const char* phaseName(TradeScreen::Phase phase) {
  constexpr std::array<const char*, 9> Names{"closed", "greeting", "list",     "confirm", "choice",
                                             "thanks", "bag_full", "farewell", "amount"};
  const auto at = static_cast<std::size_t>(std::to_underlying(phase));
  return at < Names.size() ? Names.at(at) : "?";
}

} // namespace

TradeScreen::TradeScreen(TradeScreenDesc desc) : desc_(desc) {}

bool TradeScreen::active() const {
  return phase_ != Phase::Closed;
}

bool TradeScreen::take(const session::StoryScreenRequest& request, const FieldScene& scene) {
  if (request.screen != session::StoryScreen::TradeShop) {
    return false;
  }
  waiting_ = request.waits;
  // openMojyamonShop reads the trader from variable 254 (setupBoxOrigin, showMapHeadTextbox): the
  // map's NPC with that script id, by its species.
  session::ItemTraderVisit visit;
  for (const auto& npc : scene.map().npcs) {
    if (npc.scriptId == request.first && npc.digimon) {
      visit.traderSpecies = *npc.digimon;
    }
  }
  if (desc_.trader == nullptr || !desc_.trader->open(std::move(visit))) {
    return true;
  }
  cursor_ = {};
  choice_ = Yes;
  count_ = 1;
  phase_ = Phase::Greeting;
  desc_.ui->show(TradeMenuDocument, true);
  return true;
}

bool TradeScreen::handle(const ui::UiCommand& command) {
  if (phase_ == Phase::Closed) {
    return false;
  }
  if (command.name == "open_menu") {
    // The game's menu does not open over the trader.
    return true;
  }
  if (command.name != "trade_action") {
    return command.name.starts_with("trade_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    act(*action);
  }
  return true;
}

void TradeScreen::press(const input::InputState& input) {
  if (phase_ != Phase::Closed) {
    jumpHeld_ = input.isDown("walk");
  }
  if (phase_ == Phase::Amount && input.wasPressed("extra")) {
    amount_.act("extra");
  }
}

void TradeScreen::act(std::string_view action) {
  switch (phase_) {
  case Phase::Closed:
    return;
  case Phase::Greeting:
    // tickConfirmDialogue: only confirm goes on; state 3 opens the menu.
    if (action == "confirm") {
      phase_ = Phase::List;
    }
    return;
  case Phase::List:
    actInList(action);
    return;
  case Phase::Amount:
    actInAmount(action);
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
    } else if (action == "confirm" && choice_ == Yes && count_ > 1) {
      // The bulk trade: that many state 5s at once; the first the bag refuses ends them.
      phase_ = desc_.trader->tradeMany(count_) > 0 ? Phase::Thanks : Phase::BagFull;
    } else if (action == "confirm" && choice_ == Yes) {
      // State 5: the trade; the menu closes.
      phase_ =
          desc_.trader->trade() == session::TradeOutcome::Traded ? Phase::Thanks : Phase::BagFull;
    } else if (action == "confirm") {
      phase_ = Phase::Farewell;
    } else if (action == "cancel") {
      // tickSelectionDialogue: cancel answers the last choice, no, unless trigger 0x31 is set.
      if (desc_.trader->choiceCancellable()) {
        phase_ = Phase::Farewell;
      } else if (desc_.sound != nullptr) {
        const session::TraderSound& refuse = desc_.trader->rules().refuseSound;
        desc_.sound->playSound(refuse.slot, refuse.sound);
      }
    }
    return;
  case Phase::Thanks:
  case Phase::BagFull:
  case Phase::Farewell:
    // State 2: the last line closes the visit.
    if (action == "confirm") {
      close();
    }
    return;
  }
}

void TradeScreen::actInList(std::string_view action) {
  const std::int32_t count = rowCount();
  if (action == "cancel") {
    // State 6: the menu closes and the trader says goodbye.
    phase_ = Phase::Farewell;
  } else if (action == "up") {
    jumpHeld_ ? static_cast<void>(cursor_.row = 0) : cursor_.up();
  } else if (action == "down") {
    jumpHeld_ ? static_cast<void>(cursor_.row = count > 0 ? count - 1 : 0) : cursor_.down(count);
  } else if (action == "confirm") {
    // tickMojyaTradeMenu: an item the bag holds goes on to the question; one it does not hold
    // only sounds (playSound(0, 0xb)).
    if (desc_.trader->offer(static_cast<std::size_t>(cursor_.row))) {
      count_ = 1;
      // The bulk trade: an item the bag could trade more than once asks how many first.
      if (const std::int32_t limit = bulk_ ? desc_.trader->tradeLimit() : 0; limit > 1) {
        amount_.reset(limit);
        phase_ = Phase::Amount;
        return;
      }
      phase_ = Phase::Confirm;
    } else if (desc_.sound != nullptr) {
      const session::TraderSound& refuse = desc_.trader->rules().refuseSound;
      desc_.sound->playSound(refuse.slot, refuse.sound);
    }
  }
}

void TradeScreen::actInAmount(std::string_view action) {
  if (amount_.act(action)) {
    return;
  }
  if (action == "confirm") {
    count_ = amount_.value;
    phase_ = Phase::Confirm;
  } else if (action == "cancel") {
    // Back to the list, as before the item was picked.
    count_ = 1;
    phase_ = Phase::List;
  }
}

void TradeScreen::close() {
  desc_.ui->hide(TradeMenuDocument);
  desc_.trader->close();
  phase_ = Phase::Closed;
}

void TradeScreen::update(session::StoryScenes& scenes) {
  if (phase_ != Phase::Closed) {
    screens::publish(*desc_.ui, settings(), Revision);
    return;
  }
  if (waiting_) {
    waiting_ = false;
    scenes.screenClosed();
  }
}

std::int32_t TradeScreen::rowCount() const {
  return static_cast<std::int32_t>(desc_.trader->rows().size());
}

std::string TradeScreen::playerName() const {
  if (desc_.session != nullptr) {
    if (const std::string& name = desc_.session->names().tamer; !name.empty()) {
      return name;
    }
  }
  return localization::tr(screens::mainLabelKey(screens::SpeciesNamesBlock, PlayerNameSlot));
}

screens::ScreenSettings TradeScreen::settings() const {
  const session::ItemTrader& trader = *desc_.trader;
  const session::ItemTraderLines& lines = trader.rules().lines;
  const screens::NameKeys names{species_, &trader.items(), nullptr};
  std::int32_t line = lines.ask;
  switch (phase_) {
  case Phase::Greeting:
    line = lines.greeting;
    break;
  case Phase::Confirm:
    line = lines.confirm;
    break;
  case Phase::Choice:
    line = lines.choices;
    break;
  case Phase::Thanks:
    line = lines.thanks;
    break;
  case Phase::BagFull:
    line = lines.bagFull;
    break;
  case Phase::Farewell:
    line = lines.farewell;
    break;
  case Phase::Closed:
  case Phase::List:
  case Phase::Amount:
    break;
  }
  const std::string& speaker = trader.visit() ? trader.visit()->traderSpecies : std::string{};
  const auto offered = trader.offered();
  screens::ScreenSettings out{
      {.name = "trade.phase", .value = std::string{phaseName(phase_)}},
      // showMapheadSelection(..., 0xfd, ...): the yes-or-no is the player's.
      {.name = "trade.speaker",
       .value = phase_ == Phase::Choice ? std::string{}
                : speaker.empty()       ? std::string{}
                                        : names.species(speaker)},
      {.name = "trade.player", .value = phase_ == Phase::Choice ? playerName() : std::string{}},
      {.name = "trade.line_key", .value = trader.lineKey(line)},
      {.name = "trade.offered_key", .value = offered ? names.item(*offered) : std::string{}},
      {.name = "trade.label_key", .value = screens::mainLabelKey(LabelBlock, 0)},
      {.name = "trade.choice", .value = static_cast<double>(choice_)},
      {.name = "trade.cursor", .value = static_cast<double>(cursor_.row)},
      // The bulk trade: the count while it is picked, then the count the yes trades.
      {.name = "trade.amount.value",
       .value = static_cast<double>(phase_ == Phase::Amount ? amount_.value : count_)},
      {.name = "trade.amount.most", .value = static_cast<double>(amount_.most)},
  };
  const std::vector<session::TradeRow> rows = trader.rows();
  out.push_back({.name = "trade.total", .value = static_cast<double>(rows.size())});
  for (std::size_t i = 0; i < rows.size(); ++i) {
    out.push_back(
        {.name = screens::listEntryName(RowList, i, "give"), .value = names.item(rows[i].give)});
    out.push_back(
        {.name = screens::listEntryName(RowList, i, "get"), .value = names.item(rows[i].get)});
    out.push_back({.name = screens::listEntryName(RowList, i, "owned"), .value = rows[i].owned});
    out.push_back({.name = screens::listEntryName(RowList, i, "traded"), .value = rows[i].traded});
  }
  return out;
}

} // namespace dwe::app
