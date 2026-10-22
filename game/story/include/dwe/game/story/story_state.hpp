#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

// The state of the story (docs/architecture/interprete-historia.md, "StoryState"): the event scripts' triggers and
// variables, as the original keeps them in its script state (script.h: ScriptState).
namespace dwe::game::story {

// Triggers 0-799 (script_interp.c: getTriggerOffset indexes 100 bytes); the disc names none past 716.
inline constexpr std::int32_t TriggerCount = 800;
// Variables 0-255, one byte each (script_interp.c: readPStat).
inline constexpr std::int32_t VariableCount = 256;

class StoryState {
public:
    // Whether a trigger is set; a number outside 0-799 is an error.
    [[nodiscard]] core::Result<bool> trigger(std::int32_t trigger) const;
    // Sets or clears a trigger; a number outside 0-799 is an error and changes nothing.
    core::Result<void> setTrigger(std::int32_t trigger, bool set);

    // A variable; a number outside 0-255 is an error.
    [[nodiscard]] core::Result<std::uint8_t> variable(std::int32_t variable) const;
    core::Result<void> setVariable(std::int32_t variable, std::uint8_t value);

    // How many triggers are set, for the log.
    [[nodiscard]] std::size_t triggersSet() const { return triggers_.count(); }

    friend bool operator==(const StoryState&, const StoryState&) = default;

private:
    std::bitset<TriggerCount> triggers_;
    std::array<std::uint8_t, VariableCount> variables_{};
};

} // namespace dwe::game::story
