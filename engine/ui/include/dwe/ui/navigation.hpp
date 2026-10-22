#pragma once

#include <dwe/input/input_state.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dwe::ui {

// One step of menu navigation. Every device produces the same steps, so a menu works the same with
// each.
enum class NavCommand : std::uint8_t { Up, Down, Left, Right, Confirm, Cancel, Menu };

// The action of each step: the menus use the same actions as the game, so rebinding them rebinds
// both.
[[nodiscard]] std::string_view actionOf(NavCommand command);

// How a held direction repeats, as a held key does.
struct NavigationTiming {
  double firstRepeatSeconds = 0.40;
  double repeatSeconds = 0.09;
};

// Turns the input's actions into navigation steps, once per logic tick. A direction steps when it
// goes down and then repeats while it is held; confirm, cancel and menu step once per press. Sticks
// count as held past the input's threshold, so the stick, the D-pad and the keys all navigate.
class Navigation {
public:
  explicit Navigation(NavigationTiming timing = {}) : timing_{timing} {}

  // Appends the steps of one tick of the given length.
  void tick(const input::InputState& input, double seconds, std::vector<NavCommand>& steps);

  // Forgets the held directions, so a direction already held when a menu opens does not repeat into
  // it.
  void reset();

private:
  struct HeldDirection {
    bool down = false;
    double untilRepeat = 0.0;
  };

  NavigationTiming timing_;
  std::array<HeldDirection, 4> directions_{};
  bool waitForRelease_ = false;
};

} // namespace dwe::ui
