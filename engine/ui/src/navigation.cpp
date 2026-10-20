#include <dwe/ui/navigation.hpp>

#include <cstddef>

namespace dwe::ui {
namespace {

constexpr std::array Directions{NavCommand::Up, NavCommand::Down, NavCommand::Left, NavCommand::Right};
constexpr std::array Presses{NavCommand::Confirm, NavCommand::Cancel, NavCommand::Menu};

} // namespace

std::string_view actionOf(NavCommand command) {
    switch (command) {
    case NavCommand::Up:
        return "move_up";
    case NavCommand::Down:
        return "move_down";
    case NavCommand::Left:
        return "move_left";
    case NavCommand::Right:
        return "move_right";
    case NavCommand::Confirm:
        return "confirm";
    case NavCommand::Cancel:
        return "cancel";
    case NavCommand::Menu:
        return "menu";
    }
    return "";
}

void Navigation::tick(const input::InputState& input, double seconds, std::vector<NavCommand>& steps) {
    if (waitForRelease_) {
        bool anyDown = false;
        for (const NavCommand direction : Directions) {
            anyDown = anyDown || input.isDown(actionOf(direction));
        }
        waitForRelease_ = anyDown;
    }
    for (std::size_t index = 0; index < Directions.size(); ++index) {
        const std::string_view action = actionOf(Directions.at(index));
        HeldDirection& held = directions_.at(index);
        // A tap shorter than a tick is down and up again before the tick runs; wasPressed still reports it.
        const bool pressed = input.wasPressed(action) && !waitForRelease_;
        const bool down = input.isDown(action) && !waitForRelease_;
        if (pressed || (down && !held.down)) {
            steps.push_back(Directions.at(index));
            held.untilRepeat = timing_.firstRepeatSeconds;
        } else if (down) {
            held.untilRepeat -= seconds;
            if (held.untilRepeat <= 0.0) {
                steps.push_back(Directions.at(index));
                held.untilRepeat += timing_.repeatSeconds;
            }
        }
        held.down = down;
    }
    for (const NavCommand press : Presses) {
        if (input.wasPressed(actionOf(press))) {
            steps.push_back(press);
        }
    }
}

void Navigation::reset() {
    directions_ = {};
    waitForRelease_ = true;
}

} // namespace dwe::ui
