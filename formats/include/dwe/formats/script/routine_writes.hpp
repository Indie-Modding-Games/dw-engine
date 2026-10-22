#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// The triggers the game's own routines may set or clear while a script runs: what call_routine and
// call_digimon_routine leave unknown to the guarded walk (guarded_walk.hpp: ForgetTerm). A routine that is not in the
// table writes no trigger.
namespace dwe::formats::script {

constexpr std::string_view CallRoutineOp = "call_routine";
constexpr std::string_view CallDigimonRoutineOp = "call_digimon_routine";

// The routine numbers the game knows: script_interp.c: scriptInstruction64to7E has a case for every call_routine up
// to this one, partner.c: callDigimonRoutine for every call_digimon_routine up to this one; others do nothing.
constexpr std::uint8_t LastCallRoutine = 0x38;
constexpr std::uint8_t LastDigimonRoutine = 9;

enum class WriteSet : std::uint8_t {
    Listed, // the triggers in `triggers`
    Any,    // a set the game code does not bound: the walk forgets every trigger
};

struct RoutineWrites {
    std::string_view op; // CallRoutineOp or CallDigimonRoutineOp
    std::uint8_t routine = 0;
    std::string_view source; // where the game writes them, file: function; for Any, why the set is not bounded
    WriteSet set = WriteSet::Listed;
    std::vector<std::uint16_t> triggers;
};

// The routines that write triggers, each once.
[[nodiscard]] const std::vector<RoutineWrites>& routineWrites();

// The entry for a routine, or nothing when it writes no trigger.
[[nodiscard]] const RoutineWrites* routineWrites(std::string_view op, std::uint8_t routine);

} // namespace dwe::formats::script
