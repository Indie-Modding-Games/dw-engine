#include "item_keeper_screen.hpp"

#include "common.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::app {
namespace {

using session::KeeperAmount;
using session::KeeperRow;
using session::KeeperSide;

constexpr std::string_view Revision = "item_keeper.revision";
constexpr std::string_view KeptList = "item_keeper.kept";
constexpr std::string_view BagList = "item_keeper.bag";
// showMapheadSelection: yes and no.
constexpr std::int32_t ChoiceCount = 2;

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 6> Names{"closed",  "ask",         "choice",
                                             "windows", "description", "farewell"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

std::optional<std::int32_t> indexOf(const std::vector<KeeperRow>& rows, assets::ItemHandle item) {
  const auto found = std::ranges::find(rows, item, &KeeperRow::item);
  if (found == rows.end()) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(found - rows.begin());
}

std::optional<std::int32_t> indexOf(const std::vector<std::optional<KeeperRow>>& rows,
                                    assets::ItemHandle item) {
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i] && rows[i]->item == item) {
      return static_cast<std::int32_t>(i);
    }
  }
  return std::nullopt;
}

} // namespace

bool KeeperListView::up() {
  if (index() == 0) {
    return false;
  }
  if (cursor == 0) {
    --top;
  } else {
    --cursor;
  }
  return true;
}

bool KeeperListView::down(std::int32_t count) {
  if (index() + 1 >= count) {
    return false;
  }
  if (cursor + 1 == KeeperVisibleRows) {
    ++top;
  } else {
    ++cursor;
  }
  return true;
}

void KeeperListView::toTop() {
  top = 0;
  cursor = 0;
}

void KeeperListView::toBottom(std::int32_t count) {
  const std::int32_t past = count - KeeperVisibleRows;
  if (past <= 0) {
    top = 0;
    cursor = std::max(count - 1, 0);
  } else {
    top = past;
    cursor = KeeperVisibleRows - 1;
  }
}

void KeeperListView::scrollTo(std::optional<std::int32_t> found, std::int32_t count,
                              bool keepInList) {
  std::int32_t row = 0;
  if (found) {
    row = *found;
  } else {
    if (!keepInList) {
      return;
    }
    // keeperScrollLeftToItem: the item left the keeper's list.
    if (count == 0) {
      toTop();
      return;
    }
    row = std::min(index(), count - 1);
    if (top + KeeperVisibleRows >= count && top != 0) {
      --top;
      return;
    }
  }
  if (row >= top && row < top + KeeperVisibleRows) {
    cursor = row - top;
    return;
  }
  const std::int32_t past = row - KeeperVisibleRows;
  if (past < 0) {
    top = 0;
    cursor = row;
  } else {
    top = past + 1;
    cursor = KeeperVisibleRows - 1;
  }
}

ItemKeeperScreen::ItemKeeperScreen(ItemKeeperScreenDesc desc) : desc_(desc) {}

bool ItemKeeperScreen::active() const {
  return phase_ != Phase::Closed;
}

void ItemKeeperScreen::update() {
  if (desc_.keeper == nullptr) {
    return;
  }
  if (phase_ == Phase::Closed && desc_.keeper->visit()) {
    // tickItemKeeper state 0: the question.
    side_ = KeeperSide::Kept;
    kept_ = {};
    bag_ = {};
    choice_ = 0;
    described_.reset();
    phase_ = Phase::Ask;
    desc_.ui->show(ItemKeeperDocument, true);
    core::logDebug("[port:keeper] tickItemKeeper (shop/script_menu.c:892): state 0, {} kept",
                   desc_.keeper->keptRows().size());
  }
  if (phase_ != Phase::Closed) {
    screens::publish(*desc_.ui, settings(), Revision);
  }
}

bool ItemKeeperScreen::handle(const ui::UiCommand& command) {
  if (phase_ == Phase::Closed) {
    return false;
  }
  if (command.name == "open_menu") {
    // The game's menu does not open over the keeper.
    return true;
  }
  if (command.name != "item_keeper_action") {
    return command.name.starts_with("item_keeper_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    act(*action);
  }
  return true;
}

void ItemKeeperScreen::press(const input::InputState& input) {
  if (phase_ == Phase::Closed) {
    return;
  }
  jumpHeld_ = input.isDown("walk");
  if (phase_ == Phase::Description) {
    if (input.wasPressed("pause") || input.wasPressed("menu") || input.wasPressed("alt") ||
        input.wasPressed("extra")) {
      act("cancel");
    }
    confirm_.stop();
    return;
  }
  if (phase_ != Phase::Windows) {
    confirm_.stop();
    return;
  }
  // The confirm's hold: one item on the press, more as the button stays down, quickening. Done
  // before the empty-list guard so a hold that runs out of rows stops cleanly on the next tick.
  const int moves =
      confirm_.tick(input.isDown("confirm"), input.wasPressed("confirm"), TickSeconds);
  // tickItemKeeperWindow: the alternative button moves ten, square the whole stack and Start opens
  // the item's description (here alt, extra and pause).
  if (rowCount(side_) == 0) {
    confirm_.stop();
    return;
  }
  if (input.wasPressed("alt")) {
    moveRow(KeeperAmount::Ten);
    return;
  }
  if (input.wasPressed("extra")) {
    moveRow(KeeperAmount::Stack);
    return;
  }
  if (input.wasPressed("pause")) {
    openDescription();
    confirm_.stop();
    return;
  }
  for (int moved = 0; moved < moves; ++moved) {
    moveRow(KeeperAmount::One);
  }
}

void ItemKeeperScreen::act(std::string_view action) {
  switch (phase_) {
  case Phase::Closed:
    return;
  case Phase::Ask:
    if (action == "confirm") {
      // State 3: the yes-or-no choice.
      phase_ = Phase::Choice;
      choice_ = 0;
    }
    return;
  case Phase::Choice:
    if (action == "up" || action == "down") {
      choice_ = std::clamp(choice_ + (action == "up" ? -1 : 1), 0, ChoiceCount - 1);
    } else if (action == "confirm" && choice_ == 0) {
      // State 4: the windows, with what each holds.
      phase_ = Phase::Windows;
    } else if (action == "confirm" || action == "cancel") {
      phase_ = Phase::Farewell;
    }
    return;
  case Phase::Windows:
    actInWindows(action);
    return;
  case Phase::Description:
    // tickItemMenuDescriptionBox: any button closes it. The document also turns pause into "menu"
    // in the tick that opened the box; press() closes it on pause and menu instead.
    if (action == "menu") {
      return;
    }
    described_.reset();
    phase_ = Phase::Windows;
    return;
  case Phase::Farewell:
    if (action == "confirm" || action == "cancel") {
      finish();
    }
    return;
  }
}

void ItemKeeperScreen::actInWindows(std::string_view action) {
  if (action == "cancel") {
    // State 6 then 5: the windows close and the keeper says goodbye.
    phase_ = Phase::Farewell;
    return;
  }
  if (action == "left") {
    side_ = KeeperSide::Kept;
    return;
  }
  if (action == "right") {
    side_ = KeeperSide::Bag;
    return;
  }
  const std::int32_t count = rowCount(side_);
  if (count == 0) {
    return;
  }
  KeeperListView& shown = list(side_);
  if (action == "confirm") {
    // The confirm's move is the screen's press(), which also repeats while the button is held; the
    // "confirm" the document forwards does nothing here, so a tap moves once, not twice.
    return;
  }
  if (action == "up") {
    jumpHeld_ ? shown.toTop() : static_cast<void>(shown.up());
  } else if (action == "down") {
    jumpHeld_ ? shown.toBottom(count) : static_cast<void>(shown.down(count));
  }
}

void ItemKeeperScreen::moveRow(KeeperAmount amount) {
  session::ItemKeeper& keeper = *desc_.keeper;
  const std::optional<KeeperRow> row = rowAt(side_, list(side_).index());
  if (!row || !keeper.move(side_, *row, amount)) {
    return;
  }
  // keeperScrollLeftToItem and keeperScrollRightToItem: both windows show the item moved.
  const std::vector<KeeperRow> kept = keeper.keptRows();
  const std::vector<std::optional<KeeperRow>> bag = keeper.bagRows();
  kept_.scrollTo(indexOf(kept, row->item), static_cast<std::int32_t>(kept.size()), true);
  bag_.scrollTo(indexOf(bag, row->item), static_cast<std::int32_t>(bag.size()), false);
}

void ItemKeeperScreen::openDescription() {
  const std::optional<KeeperRow> row = rowAt(side_, list(side_).index());
  if (!row) {
    return;
  }
  described_ = row->item;
  phase_ = Phase::Description;
}

void ItemKeeperScreen::finish() {
  desc_.ui->hide(ItemKeeperDocument);
  desc_.keeper->close();
  phase_ = Phase::Closed;
}

std::int32_t ItemKeeperScreen::rowCount(KeeperSide side) const {
  return side == KeeperSide::Kept ? static_cast<std::int32_t>(desc_.keeper->keptRows().size())
                                  : static_cast<std::int32_t>(desc_.keeper->bagRows().size());
}

std::optional<KeeperRow> ItemKeeperScreen::rowAt(KeeperSide side, std::int32_t index) const {
  if (index < 0) {
    return std::nullopt;
  }
  const auto at = static_cast<std::size_t>(index);
  if (side == KeeperSide::Kept) {
    const std::vector<KeeperRow> rows = desc_.keeper->keptRows();
    return at < rows.size() ? std::optional{rows[at]} : std::nullopt;
  }
  const std::vector<std::optional<KeeperRow>> rows = desc_.keeper->bagRows();
  return at < rows.size() ? rows[at] : std::nullopt;
}

KeeperListView& ItemKeeperScreen::list(KeeperSide side) {
  return side == KeeperSide::Kept ? kept_ : bag_;
}

screens::ScreenSettings ItemKeeperScreen::settings() const {
  const session::ItemKeeper& keeper = *desc_.keeper;
  const session::ItemKeeperLines& lines = keeper.rules().lines;
  const screens::NameKeys names{species_, &keeper.items(), nullptr};
  std::string line;
  if (phase_ == Phase::Ask) {
    line = keeper.lineKey(lines.ask);
  } else if (phase_ == Phase::Farewell) {
    line = keeper.lineKey(lines.farewell);
  }
  const std::string& speaker = keeper.visit() ? keeper.visit()->keeperSpecies : std::string{};
  screens::ScreenSettings out{
      {.name = "item_keeper.phase", .value = std::string{phaseName(std::to_underlying(phase_))}},
      {.name = "item_keeper.speaker",
       .value = speaker.empty() ? std::string{} : names.species(speaker)},
      {.name = "item_keeper.line_key", .value = line},
      {.name = "item_keeper.choices_key", .value = keeper.lineKey(lines.choices)},
      {.name = "item_keeper.choice", .value = static_cast<double>(choice_)},
      {.name = "item_keeper.side", .value = side_ == KeeperSide::Kept ? 0.0 : 1.0},
      {.name = "item_keeper.description_key",
       .value = described_ ? names.itemDescription(*described_) : std::string{}},
  };
  const auto addList = [&](std::string_view list, const KeeperListView& view,
                           const std::vector<std::optional<KeeperRow>>& rows) {
    out.push_back(
        {.name = std::string{list} + ".total", .value = static_cast<double>(rows.size())});
    out.push_back({.name = std::string{list} + ".top", .value = static_cast<double>(view.top)});
    out.push_back(
        {.name = std::string{list} + ".cursor", .value = static_cast<double>(view.cursor)});
    for (std::int32_t i = 0; i < KeeperVisibleRows; ++i) {
      const auto at = static_cast<std::size_t>(view.top + i);
      const std::optional<KeeperRow> row = at < rows.size() ? rows[at] : std::nullopt;
      const auto entry = static_cast<std::size_t>(i);
      out.push_back({.name = screens::listEntryName(list, entry, "name"),
                     .value = row ? names.item(row->item) : std::string{}});
      out.push_back({.name = screens::listEntryName(list, entry, "count"),
                     .value = static_cast<double>(row ? row->count : 0)});
      out.push_back(
          {.name = screens::listEntryName(list, entry, "movable"), .value = row && row->movable});
    }
  };
  std::vector<std::optional<KeeperRow>> kept;
  for (const KeeperRow& row : keeper.keptRows()) {
    kept.emplace_back(row);
  }
  addList(KeptList, kept_, kept);
  addList(BagList, bag_, keeper.bagRows());
  return out;
}

} // namespace dwe::app
