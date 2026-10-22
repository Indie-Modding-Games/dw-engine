#pragma once

#include <dwe/ui/ui.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

#include "field_scene.hpp"
#include "user_settings.hpp"

namespace dwe::app {

// Free exploration (docs/architecture/warps-de-script.md): a UI setting and the pause menu's
// command with the same name (true or false), off by default.
inline constexpr std::string_view FreeExplorationSetting = "free_exploration";
// The chooser's document and its commands: a place of the choice, from 0, or staying on the cell.
inline constexpr std::string_view DestinationChoiceDocument = "destination_choice";
inline constexpr std::string_view ChooseDestinationCommand = "choose_destination";
inline constexpr std::string_view StayHereCommand = "stay_here";
// What the chooser reads (destination_choice/logic.lua): the count of places, the story's place
// (from 0, or -1), and the name key and the map id of each place, by its number from 0 after the
// prefix.
inline constexpr std::string_view ExplorationPlacesSetting = "exploration_places";
inline constexpr std::string_view ExplorationStorySetting = "exploration_story";
inline constexpr std::string_view ExplorationPlaceNamePrefix = "exploration_place_name.";
inline constexpr std::string_view ExplorationPlaceMapPrefix = "exploration_place_map.";

// Whether the run starts with free exploration: --explore turns it on for the run; otherwise the
// saved setting.
[[nodiscard]] bool startFreeExploration(bool option, const std::optional<bool>& saved);

struct FreeExplorationDesc {
  ui::Ui* ui = nullptr;
  FieldScene* scene = nullptr;
  // The player's settings, where the pause menu's choice is kept; they must outlive this.
  UserSettings* settings = nullptr;
  // Where the settings are saved; nothing for runs that must not touch them (autotests).
  std::optional<std::filesystem::path> settingsFile;
};

// Free exploration while the game runs: the pause menu's switch, and the chooser of the places a
// cell's scripts lead to when there are several (game::field::DestinationChoice).
class FreeExploration {
public:
  // Publishes the setting and gives it to the field.
  FreeExploration(FreeExplorationDesc desc, bool explore);

  // Takes the switch's and the chooser's commands; false for any other.
  bool handle(const ui::UiCommand& command);

  // Shows the chooser while the field waits for a choice, and hides it once answered.
  void update() const;

private:
  FreeExplorationDesc desc_;
};

} // namespace dwe::app
