#include <dwe/input/action_map.hpp>
#include <dwe/input/keys.hpp>

#include <algorithm>
#include <iterator>

namespace dwe::input {

using platform::GamepadAxis;
using platform::GamepadButton;
using platform::MouseButton;

void ActionMap::bind(std::string_view action, Binding binding) {
  auto entry = std::ranges::find(entries_, action, &Entry::action);
  if (entry == entries_.end()) {
    entries_.push_back(Entry{.action = std::string{action}, .bindings = {}});
    entry = std::prev(entries_.end());
  }
  entry->bindings.push_back(binding);
}

void ActionMap::clear(std::string_view action) {
  std::erase_if(entries_, [action](const Entry& entry) { return entry.action == action; });
}

const std::vector<Binding>& ActionMap::bindings(std::string_view action) const {
  static const std::vector<Binding> none;
  auto entry = std::ranges::find(entries_, action, &Entry::action);
  return entry == entries_.end() ? none : entry->bindings;
}

ActionMap defaultActionMap() {
  ActionMap map;

  // The first key of an action is the one its button prompts show: the space bar confirms. `F` is a
  // second confirm, the interact key next to `WASD` (docs/architecture/controles.md): it stays after
  // Space so the prompts keep naming the space bar.
  map.bind("confirm", KeyBinding{keys::Space});
  map.bind("confirm", KeyBinding{keys::Return});
  map.bind("confirm", KeyBinding{keys::F});
  map.bind("confirm", MouseBinding{MouseButton::Left});
  map.bind("confirm", GamepadButtonBinding{GamepadButton::South});

  map.bind("cancel", KeyBinding{keys::Escape});
  map.bind("cancel", KeyBinding{keys::Backspace});
  map.bind("cancel", MouseBinding{MouseButton::Right});
  map.bind("cancel", GamepadButtonBinding{GamepadButton::East});

  // The original's pad: Triangle opens the game menu on the field (game_menu.c: addGameMenu) and
  // Start pauses the game (utils2.c: handlePause). Where there is no game to pause (the title, a
  // movie, a naming box) Start does what the original's Start does there, through `pause`. In
  // battle the same `menu` opens the tamer's bag (battle_tick.c), as the field's menu shows it.
  map.bind("menu", KeyBinding{keys::Tab});
  map.bind("menu", GamepadButtonBinding{GamepadButton::North});

  map.bind("pause", KeyBinding{keys::P});
  map.bind("pause", GamepadButtonBinding{GamepadButton::Start});

  // The game's options while playing (docs/modulos/apps/game.md, «Opciones en partida»): the game
  // menu opens straight on its options page, also from Start's pause. The original's Select had no
  // use on the field. On the keyboard there is no shortcut: options live one `Tab` away, inside the
  // game menu, so no letter near `WASD` is spent on them.
  map.bind("settings", GamepadButtonBinding{GamepadButton::Back});

  // The island map (a dwe improvement; apps/game world_map.hpp): M, or the pad's left shoulder,
  // which the original's field leaves without a use.
  map.bind("world_map", KeyBinding{keys::M});
  map.bind("world_map", GamepadButtonBinding{GamepadButton::LeftShoulder});

  map.bind("move_left", KeyBinding{keys::A});
  map.bind("move_left", KeyBinding{keys::Left});
  map.bind("move_left", GamepadButtonBinding{GamepadButton::DpadLeft});
  map.bind("move_left", GamepadAxisBinding{.axis = GamepadAxis::LeftX, .direction = -1.0F});

  map.bind("move_right", KeyBinding{keys::D});
  map.bind("move_right", KeyBinding{keys::Right});
  map.bind("move_right", GamepadButtonBinding{GamepadButton::DpadRight});
  map.bind("move_right", GamepadAxisBinding{.axis = GamepadAxis::LeftX, .direction = 1.0F});

  map.bind("move_up", KeyBinding{keys::W});
  map.bind("move_up", KeyBinding{keys::Up});
  map.bind("move_up", GamepadButtonBinding{GamepadButton::DpadUp});
  map.bind("move_up", GamepadAxisBinding{.axis = GamepadAxis::LeftY, .direction = -1.0F});

  map.bind("move_down", KeyBinding{keys::S});
  map.bind("move_down", KeyBinding{keys::Down});
  map.bind("move_down", GamepadButtonBinding{GamepadButton::DpadDown});
  map.bind("move_down", GamepadAxisBinding{.axis = GamepadAxis::LeftY, .direction = 1.0F});

  // Held while moving: walk instead of running. Running is the default, as in the original, where
  // R1 walked. Only the shoulder (and Shift on the keyboard): the west button belongs to `extra`
  // alone, so holding it no longer walks (docs/architecture/controles.md).
  map.bind("walk", KeyBinding{keys::LeftShift});
  map.bind("walk", KeyBinding{keys::RightShift});
  map.bind("walk", GamepadButtonBinding{GamepadButton::RightShoulder});

  // The original pad's other two face buttons (dw/input.h: ALT_BUTTON, the circle on the American
  // version, and the square, 0x80). On the pad `alt` takes the north button because the east one is
  // `cancel` here. On the keyboard they live on `E` and `Q` for one-hand play: in the naming box
  // `takeTyping` runs before the actions (naming_box.cpp), so typing `e` or `q` writes a letter and
  // does not fire the shortcut; the pad keeps the shortcuts because its buttons don't type
  // (docs/architecture/controles.md, «Teclado moderno»).
  map.bind("alt", KeyBinding{keys::E});
  map.bind("alt", GamepadButtonBinding{GamepadButton::North});

  map.bind("extra", KeyBinding{keys::Q});
  map.bind("extra", GamepadButtonBinding{GamepadButton::West});

  map.bind("debug_console", KeyBinding{keys::Grave});
  map.bind("debug_console", KeyBinding{keys::F1});

  // Writes a bug report of the game being played (docs/guides/prueba-vertical.md).
  map.bind("report_bug", KeyBinding{keys::F12});

  // Development views (dwe-game --map): fade to a time of day, show the next map.
  map.bind("debug_time_day", KeyBinding{keys::Digit1});
  map.bind("debug_time_dusk", KeyBinding{keys::Digit2});
  map.bind("debug_time_night", KeyBinding{keys::Digit3});
  map.bind("debug_next_map", KeyBinding{keys::N});

  return map;
}

} // namespace dwe::input
