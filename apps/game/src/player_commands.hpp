#pragma once

#include <dwe/game/field/field.hpp>
#include <dwe/input/input_state.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "walk_control.hpp"

namespace dwe::app {

// The buttons the fishing reads on a logic tick, besides what the field reads.
struct FishingButtons {
  bool confirmDown = false;
  bool cancelPressed = false;
  // The camera button (Home; gamepad North): with it held the arrows move the view.
  bool cameraDown = false;
  bool left = false;
  bool right = false;
  bool up = false;
  bool down = false;
};

// Tick input for a dialogue that affects shared story state. Pointer hover and clicks remain in
// the UI adapter because they depend on the currently presented option rows.
struct StoryButtons {
  bool confirmPressed = false;
  bool confirmPressedByKeyOrPad = false;
  bool confirmDown = false;
  bool cancelPressed = false;
  bool menuPressed = false;
  bool menuDown = false;
  bool upPressed = false;
  bool downPressed = false;
};

// A transport-independent input value for one application tick. UI navigation remains local to
// the app; this value contains only gameplay inputs that can later be routed to a player slot.
struct PlayerCommands {
  // Monotonic within this controller's lifetime; a network session may reset it on join.
  std::uint32_t sequence = 0;
  game::field::FieldInput field;
  FishingButtons fishing;
  StoryButtons story;
};

// A command source for one player. It is polled once for each application tick and never blocks.
class PlayerController {
public:
  virtual ~PlayerController() = default;
  [[nodiscard]] virtual PlayerCommands next(std::uint64_t tick) = 0;
};

// Adapts local device state and the app's resolved walk order to a gameplay command value.
class LocalController final : public PlayerController {
public:
  void sample(const input::InputState& input, const WalkOrder& walk,
              input::Vector2 directionalInput, bool clickWalksAway);
  void neutral();
  [[nodiscard]] PlayerCommands next(std::uint64_t tick) override;
  void reset() {
    nextSequence_ = 0;
    pending_.reset();
  }

private:
  std::uint32_t nextSequence_ = 0;
  std::optional<PlayerCommands> pending_;
};

// Deterministic commands for replay and simulation tests. Once the script ends it emits neutral
// commands, so a test cannot accidentally repeat the final movement or button press forever.
class ScriptedController final : public PlayerController {
public:
  explicit ScriptedController(std::vector<PlayerCommands> commands)
      : commands_(std::move(commands)) {}

  [[nodiscard]] PlayerCommands next(std::uint64_t tick) override;
  void reset() {
    cursor_ = 0;
    nextSequence_ = 0;
  }

private:
  std::vector<PlayerCommands> commands_;
  std::size_t cursor_ = 0;
  std::uint32_t nextSequence_ = 0;
};

} // namespace dwe::app
