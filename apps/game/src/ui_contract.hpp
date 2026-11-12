#pragma once

#include <dwe/ui/ui_contract.hpp>

#include <span>

namespace dwe::app {

// What the game offers the UI's documents and takes from them, screen by screen, as the documents
// use it today: the settings they read, the commands they send and the options they change
// (docs/modding/ui-api.md, generated from this list and checked by a test). It covers the HUD, the
// pause menu and the partner's menu so far; the other screens follow.
[[nodiscard]] std::span<const ui::UiContractEntry> gameUiContract();

} // namespace dwe::app
