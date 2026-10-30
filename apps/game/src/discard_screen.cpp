#include "discard_screen.hpp"

#include <dwe/screens/bag_view.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::app {
namespace {

constexpr std::string_view Revision = "discard.revision";
constexpr std::string_view RowList = "discard.row";
// tickSingleCardShop: left is yes, right is no.
constexpr std::int32_t Yes = 0;
constexpr std::int32_t No = 1;

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 4> Names{"closed", "list", "confirm", "description"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

} // namespace

void DiscardListView::up() {
  if (index() == 0) {
    return;
  }
  if (cursor == 0) {
    --top;
  } else {
    --cursor;
  }
}

void DiscardListView::down(std::int32_t count) {
  if (index() + 1 >= count) {
    return;
  }
  if (cursor + 1 == DiscardVisibleRows) {
    ++top;
  } else {
    ++cursor;
  }
}

void DiscardListView::toTop() {
  top = 0;
  cursor = 0;
}

void DiscardListView::toBottom(std::int32_t count) {
  const std::int32_t past = count - DiscardVisibleRows;
  top = std::max(past, 0);
  cursor = std::max(std::min(count, DiscardVisibleRows) - 1, 0);
}

DiscardScreen::DiscardScreen(DiscardScreenDesc desc) : desc_(desc) {}

bool DiscardScreen::active() const {
  return phase_ != Phase::Closed;
}

bool DiscardScreen::take(const session::StoryScreenRequest& request) {
  if (request.screen != session::StoryScreen::DiscardItem) {
    return false;
  }
  waiting_ = request.waits;
  // openDiscardItem state 0: without an item to pick the menu does not open and the story goes on.
  if (desc_.menu == nullptr || !desc_.menu->open()) {
    return true;
  }
  list_ = {};
  choice_ = Yes;
  phase_ = Phase::List;
  desc_.ui->show(DiscardMenuDocument, true);
  return true;
}

bool DiscardScreen::handle(const ui::UiCommand& command) {
  if (phase_ == Phase::Closed) {
    return false;
  }
  if (command.name == "open_menu") {
    // The game's menu does not open over the item menu.
    return true;
  }
  if (command.name != "discard_action") {
    return command.name.starts_with("discard_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    act(*action);
  }
  return true;
}

void DiscardScreen::press(const input::InputState& input) {
  if (phase_ == Phase::Closed) {
    return;
  }
  jumpHeld_ = input.isDown("walk");
  if (phase_ == Phase::Description) {
    if (input.wasPressed("pause") || input.wasPressed("menu") || input.wasPressed("alt") ||
        input.wasPressed("extra")) {
      act("cancel");
    }
    return;
  }
  // tickItemMenu: Start opens the item's description box.
  if (phase_ == Phase::List && input.wasPressed("pause") && current()) {
    phase_ = Phase::Description;
  }
}

void DiscardScreen::act(std::string_view action) {
  switch (phase_) {
  case Phase::Closed:
    return;
  case Phase::List:
    actInList(action);
    return;
  case Phase::Confirm:
    if (action == "left" || action == "right") {
      choice_ = action == "left" ? Yes : No;
    } else if (action == "confirm" && choice_ == Yes) {
      // tickSingleCardShop: the item goes to the story and the menu closes (openDiscardItem 4).
      if (const auto row = current(); row && desc_.menu->choose(*row)) {
        close();
        return;
      }
      phase_ = Phase::List;
    } else if (action == "confirm" || action == "cancel") {
      // No, or cancel: the box closes and the list takes the buttons again.
      phase_ = Phase::List;
    }
    return;
  case Phase::Description:
    // tickItemMenuDescriptionBox: any button closes it. press() closes it on pause and menu.
    if (action != "menu") {
      phase_ = Phase::List;
    }
    return;
  }
}

void DiscardScreen::actInList(std::string_view action) {
  if (action == "cancel") {
    // tickItemMenu: cancel closes the menu only while the story allows it (trigger 3).
    if (desc_.menu->cancel()) {
      close();
    }
    return;
  }
  const std::int32_t count = rowCount();
  if (action == "up") {
    jumpHeld_ ? list_.toTop() : list_.up();
  } else if (action == "down") {
    jumpHeld_ ? list_.toBottom(count) : list_.down(count);
  } else if (action == "confirm") {
    // createSingleCardShopMenu: an empty slot or an item that cannot be thrown away fails.
    if (const auto row = current(); row && row->choosable) {
      choice_ = Yes;
      phase_ = Phase::Confirm;
    }
  }
}

void DiscardScreen::close() {
  desc_.ui->hide(DiscardMenuDocument);
  phase_ = Phase::Closed;
}

void DiscardScreen::update(session::StoryScenes& scenes) {
  if (phase_ != Phase::Closed) {
    screens::publish(*desc_.ui, settings(), Revision);
    return;
  }
  if (waiting_) {
    waiting_ = false;
    scenes.screenClosed();
  }
}

std::int32_t DiscardScreen::rowCount() const {
  return static_cast<std::int32_t>(desc_.menu->rows().size());
}

std::optional<session::DiscardRow> DiscardScreen::current() const {
  const auto rows = desc_.menu->rows();
  const auto at = static_cast<std::size_t>(list_.index());
  return at < rows.size() ? rows[at] : std::nullopt;
}

screens::ScreenSettings DiscardScreen::settings() const {
  const session::DiscardMenu& menu = *desc_.menu;
  const screens::NameKeys names{nullptr, &menu.items(), nullptr};
  const auto rows = menu.rows();
  // The names and icons as the bag shows them, slot for slot.
  const screens::BagView bag =
      screens::bagViewOf(desc_.inventory->inventory(), menu.items(), names);
  const std::optional<session::DiscardRow> shown = current();
  screens::ScreenSettings out{
      {.name = "discard.phase", .value = std::string{phaseName(std::to_underlying(phase_))}},
      {.name = "discard.total", .value = static_cast<double>(rows.size())},
      {.name = "discard.top", .value = static_cast<double>(list_.top)},
      {.name = "discard.cursor", .value = static_cast<double>(list_.cursor)},
      {.name = "discard.choice", .value = static_cast<double>(choice_)},
      {.name = "discard.confirm_key", .value = menu.confirmKey()},
      {.name = "discard.cancellable", .value = menu.cancellable()},
      {.name = "discard.description_key",
       .value = phase_ == Phase::Description && shown ? names.itemDescription(shown->item)
                                                      : std::string{}},
  };
  for (std::int32_t i = 0; i < DiscardVisibleRows; ++i) {
    const auto at = static_cast<std::size_t>(list_.top + i);
    const std::optional<session::DiscardRow> row = at < rows.size() ? rows[at] : std::nullopt;
    const screens::BagSlotView* slot =
        row && at < bag.slots.size() && !bag.slots[at].empty ? &bag.slots[at] : nullptr;
    const auto entry = static_cast<std::size_t>(i);
    out.push_back({.name = screens::listEntryName(RowList, entry, "name"),
                   .value = row ? names.item(row->item) : std::string{}});
    out.push_back({.name = screens::listEntryName(RowList, entry, "count"),
                   .value = static_cast<double>(row ? row->count : 0)});
    out.push_back({.name = screens::listEntryName(RowList, entry, "choosable"),
                   .value = row && row->choosable});
    out.push_back({.name = screens::listEntryName(RowList, entry, "icon"),
                   .value = slot != nullptr ? slot->icon : std::string{}});
    out.push_back({.name = screens::listEntryName(RowList, entry, "icon_rect"),
                   .value = slot != nullptr ? slot->iconRect : std::string{}});
  }
  return out;
}

} // namespace dwe::app
