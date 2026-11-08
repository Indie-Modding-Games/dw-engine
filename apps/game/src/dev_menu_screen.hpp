#pragma once

#include <dwe/screens/screen_settings.hpp>
#include <dwe/ui/ui.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

inline constexpr std::string_view DevMenuDocument = "dev_menu";

// One entry of a list the developer menu offers: what its console line names and the text key of
// its name (from the player's packs, so no name lives in the code).
struct DevMenuEntry {
  std::string id;
  std::string nameKey;
};

// The lists the developer menu picks from: the items to give, the species to evolve to and the
// maps the adventure has visited, to warp to.
struct DevMenuLists {
  std::vector<DevMenuEntry> items;
  std::vector<DevMenuEntry> species;
  std::vector<DevMenuEntry> maps;
};

// The developer menu (docs/modulos/apps/game.md, «Herramientas de desarrollo»): F2 opens it over a
// game while the developer tools option is on. Its pages run the console's commands (give items,
// money, the clock's speed, the partner's stats and care, evolve, warp) through the dev_menu_run
// command (dev_tools.hpp), and saves with the game menu's own quick_save.
// The field and the game time wait while it is open.
class DevMenuScreen {
public:
  explicit DevMenuScreen(ui::Ui& ui) : ui_(&ui) {}

  [[nodiscard]] bool isOpen() const;
  // Opens it with the lists to pick from, published once as dev_menu.<list>.count and
  // dev_menu.<list>.<n>.id/.name (lists items, species and maps).
  void open(const DevMenuLists& lists);
  void close();
  // Takes the document's dev_menu_close; false for any other command.
  bool handle(const ui::UiCommand& command);

private:
  ui::Ui* ui_;
};

// The settings that publish the lists (open() sends them; tests read them).
[[nodiscard]] screens::ScreenSettings devMenuSettings(const DevMenuLists& lists);

} // namespace dwe::app
