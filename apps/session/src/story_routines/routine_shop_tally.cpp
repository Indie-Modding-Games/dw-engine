#include <dwe/core/log.hpp>

#include <cstdint>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber ClearTally = 30;
constexpr game::story::RoutineNumber AddToTally = 31;
constexpr game::story::RoutineNumber StoreTally = 56;
constexpr std::int64_t ByteBits = 8;
constexpr std::int64_t ByteMask = 0xFF;

core::Result<void> writeVariable(StoryScenesContext& scenes, std::int64_t variable,
                                 std::int64_t value) {
  return scenes.state().setVariable(variable, value);
}

} // namespace

// Routines 30, 31 and 56: the shop's tally, which the shops' menus charge (script_interp.c:1781,
// 1784 and 1787: SHOP_VARIABLE). 30 clears it, 31 adds the amount in two variables (high byte,
// low byte) and 56 writes it back into them.
core::Result<void> registerShopTallyRoutines(StoryScenesContext& scenes) {
  auto cleared = scenes.routines.registerRoutine(
      ClearTally,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        scenes.shopTally = 0;
        core::logDebug("[port:story] call_routine 30 (script_interp.c:1781): tally 0");
        return game::story::RoutineGoesOn{};
      });
  if (!cleared) {
    return cleared;
  }
  auto added = scenes.routines.registerRoutine(
      AddToTally,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        const std::int64_t amount = (scenes.variable(scenes.rules.tallyHighVariable) << ByteBits) +
                                    scenes.variable(scenes.rules.tallyLowVariable);
        // The original adds it through a 16-bit signed number: an amount past 32767 subtracts.
        scenes.shopTally += static_cast<std::int16_t>(static_cast<std::uint16_t>(amount));
        core::logDebug("[port:story] call_routine 31 (script_interp.c:1784): +{} = {}", amount,
                       scenes.shopTally);
        return game::story::RoutineGoesOn{};
      });
  if (!added) {
    return added;
  }
  return scenes.routines.registerRoutine(
      StoreTally,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        auto high = writeVariable(scenes, scenes.rules.tallyHighVariable,
                                  (scenes.shopTally >> ByteBits) & ByteMask);
        if (!high) {
          return std::unexpected{high.error()};
        }
        auto low =
            writeVariable(scenes, scenes.rules.tallyLowVariable, scenes.shopTally & ByteMask);
        if (!low) {
          return std::unexpected{low.error()};
        }
        core::logDebug("[port:story] call_routine 56 (script_interp.c:1787): tally {}",
                       scenes.shopTally);
        return game::story::RoutineGoesOn{};
      });
}

} // namespace dwe::session
