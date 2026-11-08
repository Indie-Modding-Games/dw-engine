#include "dev_menu_screen.hpp"

#include <dwe/core/log.hpp>

#include <cstddef>
#include <format>
#include <utility>

namespace dwe::app {
namespace {

constexpr std::string_view CloseCommand = "dev_menu_close";

void addList(screens::ScreenSettings& out, std::string_view list,
             const std::vector<DevMenuEntry>& entries) {
  out.push_back({.name = std::format("dev_menu.{}.count", list),
                 .value = static_cast<double>(entries.size())});
  for (std::size_t i = 0; i < entries.size(); ++i) {
    out.push_back({.name = std::format("dev_menu.{}.{}.id", list, i), .value = entries[i].id});
    out.push_back(
        {.name = std::format("dev_menu.{}.{}.name", list, i), .value = entries[i].nameKey});
  }
}

} // namespace

screens::ScreenSettings devMenuSettings(const DevMenuLists& lists) {
  screens::ScreenSettings out;
  addList(out, "items", lists.items);
  addList(out, "species", lists.species);
  addList(out, "maps", lists.maps);
  return out;
}

bool DevMenuScreen::isOpen() const {
  return ui_->isVisible(DevMenuDocument);
}

void DevMenuScreen::open(const DevMenuLists& lists) {
  for (const auto& setting : devMenuSettings(lists)) {
    ui_->setSetting(setting.name, setting.value);
  }
  core::logInfo("game: developer menu open ({} items, {} species, {} maps)", lists.items.size(),
                lists.species.size(), lists.maps.size());
  ui_->show(DevMenuDocument, true);
}

void DevMenuScreen::close() {
  ui_->hide(DevMenuDocument);
}

bool DevMenuScreen::handle(const ui::UiCommand& command) {
  if (command.name != CloseCommand) {
    return false;
  }
  close();
  return true;
}

} // namespace dwe::app
