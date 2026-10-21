#pragma once

#include <dwe/core/error.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/game_session.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

// The console's and the scripts' debug commands for the partner's care, until the field's toilets and beds, the
// training screens and the clock options exist (phase 4). Each one calls the session's command as the player's would
// and adds no rule of its own; the autotest routes play the raising loop with them.
inline constexpr std::string_view ToiletCommand = "toilet";          // toilet(""): useToilet
inline constexpr std::string_view SleepCommand = "sleep";            // sleep(""): sleep, if the partner can
inline constexpr std::string_view TrainCommand = "train";            // train("<place> <course>"): one session
inline constexpr std::string_view ScoldDebugCommand = "scold";       // scold(""): scold, as the care tab's Scold
inline constexpr std::string_view ClockSpeedCommand = "clock_speed"; // clock_speed("<multiplier>"): setSpeed

// Queues the care commands the console and the scripts send, and runs them between frames, like the menus' commands.
class CareCommands {
public:
    // Binds the commands to the script VM; the commands stay valid while this object lives.
    void bind(script::ScriptVm& scripts);

    // Runs the queued commands on the session, in order; a command that fails is logged and the rest still run.
    void apply(session::GameSession& session);

private:
    enum class Kind : std::uint8_t { Toilet, Sleep, Train, Scold, ClockSpeed };
    struct Queued {
        Kind kind = Kind::Toilet;
        std::string argument;
    };

    static core::Result<void> run(session::GameSession& session, const Queued& command);
    static core::Result<void> train(session::GameSession& session, std::string_view argument);
    static core::Result<void> setClockSpeed(session::GameSession& session, std::string_view argument);

    std::vector<Queued> queued_;
};

} // namespace dwe::app
