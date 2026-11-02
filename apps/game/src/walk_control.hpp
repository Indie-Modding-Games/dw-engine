#pragma once

#include <dwe/game/field/field.hpp>
#include <dwe/ui/ui.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "user_settings.hpp"

namespace dwe::app {

// What walks the player in the field, an engine option (the original only has the pad): the
// keyboard and the pad (keys, the default) or the mouse, never both at once. With the keys a click
// never walks (no click-to-move, no held button steering), so a stray click cannot start a walk;
// with the mouse the directions of the keyboard and the pad do not walk. Menus and dialogs take
// keys and clicks either way. A UI setting and the pause menu's command with the same name.
inline constexpr std::string_view WalkControlSetting = "walk_control";
inline constexpr std::string_view WalkControlKeys = "keys";
inline constexpr std::string_view WalkControlMouse = "mouse";

enum class WalkControl : std::uint8_t { Keys, Mouse };

[[nodiscard]] std::optional<WalkControl> parseWalkControl(std::string_view name);
[[nodiscard]] std::string_view walkControlName(WalkControl control);

// The walk control the run starts with: the saved one, or the keys when none is saved. A saved
// value that is not a walk control is logged and starts on the keys.
[[nodiscard]] WalkControl startWalkControl(const std::optional<std::string>& saved);

// What each device asks of the player's walk this tick.
struct WalkDevices {
  // The keyboard's and the pad's directions, as a stick (game::field::FieldInput::stick).
  glm::vec2 keys{0.0F};
  // The ground point of a click, as a click-to-move target.
  std::optional<glm::vec3> clicked = std::nullopt;
  // The direction of a held mouse button, as a stick.
  std::optional<glm::vec2> steered = std::nullopt;
};

// The walk the field takes from them.
struct WalkOrder {
  glm::vec2 stick{0.0F};
  std::optional<glm::vec3> moveTarget = std::nullopt;
};

// Only the chosen devices walk the player.
[[nodiscard]] WalkOrder walkOrderOf(WalkControl control, const WalkDevices& devices);

// Changes the walk control while the game runs and saves it with the other settings
// (settings.yaml). A switch drops the field's click-to-move target, so the player never walks on
// to a click made before it.
class WalkControlChanger {
public:
  // settingsFile is where the choice is saved; nothing for runs that must not touch the user's
  // settings (autotests). The settings must outlive the changer.
  WalkControlChanger(UserSettings& settings, std::optional<std::filesystem::path> settingsFile);

  [[nodiscard]] WalkControl control() const { return control_; }

  // Takes the value of a walk_control command and returns the control now in use; a value that is
  // not a walk control changes nothing. `field` is the one being played, if any.
  WalkControl change(const ui::UiValue& value, game::field::Field* field);
  // Sets it for this run only, unsaved (an autotest's route).
  void use(WalkControl control, game::field::Field* field);

private:
  UserSettings* settings_;
  std::optional<std::filesystem::path> settingsFile_;
  WalkControl control_;
};

} // namespace dwe::app
