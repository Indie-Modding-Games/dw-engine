#pragma once

#include <string_view>

namespace dwe::app {

// The developer tools (docs/modulos/apps/game.md, «Herramientas de desarrollo»): the debug console
// (F1 or the key left of 1), the developer menu (F2) and the map overlay (F3), behind one option of
// the Interface tab. While it is off their keys do nothing. A package (DWE_PACKAGE) starts with it
// off and a development build with it on, until the player chooses.
inline constexpr std::string_view DevToolsSetting = "dev_tools";
#ifdef DWE_PACKAGE
inline constexpr bool DevToolsDefault = false;
#else
inline constexpr bool DevToolsDefault = true;
#endif

// The UI command the developer menu sends with a console line to run (dev_menu document).
inline constexpr std::string_view DevMenuRunCommand = "dev_menu_run";

// Whether the developer menu may run a console line: a single call, name("argument"), of one of
// the commands the menu offers (give_item, story_set, partner_set, clock_speed, evolve, warp,
// learn_move, sleep, toilet), so a document cannot run any other code through it.
[[nodiscard]] bool devMenuLineAllowed(std::string_view line);

} // namespace dwe::app
