#pragma once

#include <dwe/game/modes/mode_catalog.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "app_flow.hpp"

namespace dwe::app {

// The most modes the mode list (ui/new_game_mode) shows; the catalog's first ones, in its order.
inline constexpr std::size_t ShownModes = 6;

// The game modes a new game chooses from (docs/modulos/game/modes.md): the title screen asks only
// when the packs have more than one, so the shipped game, with only the original's story, never
// shows the list.
class NewGameModes {
public:
  explicit NewGameModes(game::modes::ModeCatalog catalog) : catalog_(std::move(catalog)) {}

  [[nodiscard]] const game::modes::ModeCatalog& catalog() const { return catalog_; }
  [[nodiscard]] bool asks() const { return catalog_.asksMode(); }

  // The list as the document reads it: new_game_mode.count, and new_game_mode.<n>.id, name and
  // description (localization keys) from 0.
  [[nodiscard]] screens::ScreenSettings settings() const;

  // What follows the choice of a mode.
  enum class Next : std::uint8_t {
    // The mode is not in the catalog: nothing changed.
    Unknown,
    // The new game's rules screen (ui/new_game_rules) chooses the ruleset: it chooses between the
    // original's rules and its hidden content, so it follows only a mode that offers both.
    AskRules,
    // The game starts with the mode's ruleset.
    Start,
  };

  // Starts a new game in a mode: the mode the save records and, when no rules screen follows, the
  // mode's ruleset.
  [[nodiscard]] Next choose(std::string_view mode, PlayStart& start) const;

private:
  game::modes::ModeCatalog catalog_;
};

} // namespace dwe::app
