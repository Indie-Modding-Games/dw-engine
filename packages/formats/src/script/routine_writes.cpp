#include <dwe/formats/script/routine_writes.hpp>

#include <algorithm>
#include <numeric>

namespace dwe::formats::script {

namespace {

// Triggers by number, as the game names them.
constexpr std::uint16_t DiscardOrSellTrigger = 3;
constexpr std::uint16_t NewGameNamedTrigger = 0x31;
constexpr std::uint16_t FirstTournamentTrigger = 0x25;
constexpr std::uint16_t LastTournamentTrigger = 0x28;
constexpr std::uint16_t FirstMedalTrigger = 0x16c;
constexpr std::uint16_t MedalCount =
    0x13; // script_common.c: unlockMedal ignores the medals from this one

std::vector<std::uint16_t> range(std::uint16_t first, std::size_t count) {
  std::vector<std::uint16_t> triggers(count);
  std::ranges::iota(triggers, first);
  return triggers;
}

// Read from the game code (the decomp): the functions each routine runs when it starts
// (script_interp.c: scriptInstruction64to7E, partner.c: callDigimonRoutine) and while the script
// waits on it (script_interp.c: tickScript), with every function they call or hand on as a
// callback, and the partner and tamer states they move to (partner.c: partnerTick, tamer.c:
// tamerTick). That search stops at the states that hand control back to play (tamer 0, 1 and 6,
// partner 1, 11 and 12): what play writes after the script is not the routine's. The triggers of
// the frame (guarded_walk.cpp: TriggersOfTheFrame) are listed where a routine writes them, though
// no path knows them anyway.
std::vector<RoutineWrites> table() {
  return {
      {.op = CallRoutineOp,
       .routine = 0x00,
       .source = "script_common.c: openDiscardItem, tickItemMenu",
       .set = WriteSet::Listed,
       .triggers = {DiscardOrSellTrigger}},
      {.op = CallRoutineOp,
       .routine = 0x01,
       .source = "script_common.c: tickItemMenu, tickSingleCardShop (openMeritShop)",
       .set = WriteSet::Listed,
       .triggers = {DiscardOrSellTrigger}},
      {.op = CallRoutineOp,
       .routine = 0x02,
       .source =
           "shop/script_menu.c: setMojyaItemTradedTrigger (openMojyamonShop) sets the trigger of the item under "
           "the menu cursor",
       .set = WriteSet::Any,
       .triggers = {}},
      {.op = CallRoutineOp,
       .routine = 0x06,
       .source =
           "utils2.c: startTournament sets 15 plus the cup in a variable, and its battles reach the death, "
           "evolution and map change code, which runs other scripts (script_interp.c: tickScript)",
       .set = WriteSet::Any,
       .triggers = {}},
      {.op = CallRoutineOp,
       .routine = 0x08,
       .source = "script_common.c: tickItemMenu, tickSingleCardShop (openShop, openRecycleShop)",
       .set = WriteSet::Listed,
       .triggers = {DiscardOrSellTrigger}},
      {.op = CallRoutineOp,
       .routine = 0x0e,
       .source =
           "script_common.c: tickSingleCardShop (rollCardPack, openBuyCardMenu, openSellCardMenu)",
       .set = WriteSet::Listed,
       .triggers = {DiscardOrSellTrigger}},
      {.op = CallRoutineOp,
       .routine = 0x12,
       .source = "dget/dget.c: initTournamentSchedule",
       .set = WriteSet::Listed,
       .triggers =
           range(FirstTournamentTrigger, LastTournamentTrigger - FirstTournamentTrigger + 1)},
      {.op = CallRoutineOp,
       .routine = 0x16,
       .source = "script_common.c: unlockMedal (scriptCheckTournamentMedal)",
       .set = WriteSet::Listed,
       .triggers = range(FirstMedalTrigger, MedalCount)},
      {.op = CallRoutineOp,
       .routine = 0x20,
       .source = "shop/script_menu.c: newGameStateMachine",
       .set = WriteSet::Listed,
       .triggers = {NewGameNamedTrigger}},
      {.op = CallRoutineOp,
       .routine = 0x21,
       .source = "script_interp.c: scriptInstruction64to7E",
       .set = WriteSet::Listed,
       .triggers = range(FirstTournamentTrigger, LastTournamentTrigger - FirstTournamentTrigger)},
      {.op = CallRoutineOp,
       .routine = 0x30,
       .source =
           "main_menu.c: loadSavegame (tickSaveMachine, tickMainMenu) copies a saved script state",
       .set = WriteSet::Any,
       .triggers = {}},
      {.op = CallRoutineOp,
       .routine = 0x36,
       .source =
           "main_menu.c: loadSavegame (scriptTickMainMenu, tickMainMenu) copies a saved script state",
       .set = WriteSet::Any,
       .triggers = {}},
      {.op = CallDigimonRoutineOp,
       .routine = 2,
       .source =
           "partner.c: partnerTickEvolving sets the raised trigger (script_common.c: setDigimonRaised) and "
           "hands the partner back to partner.c: partnerTick, whose states reach the main menu and other "
           "scripts",
       .set = WriteSet::Any,
       .triggers = {}},
      {.op = CallDigimonRoutineOp,
       .routine = 3,
       .source =
           "partner.c: partnerTickDying reaches the death map change (dooa/dooa.c: DOOA_tickDissolve, "
           "map.c: changeToDeathMap), which runs "
           "other scripts",
       .set = WriteSet::Any,
       .triggers = {}},
      {.op = CallDigimonRoutineOp,
       .routine = 4,
       .source =
           "partner.c: partnerTickDying2 reincarnates the partner (script_common.c: setDigimonRaised) and "
           "hands it back to partner.c: partnerTick, whose states reach the main menu and other scripts",
       .set = WriteSet::Any,
       .triggers = {}},
      {.op = CallDigimonRoutineOp,
       .routine = 7,
       .source =
           "tamer.c: tamerTickSicknessLostLife reaches the death map change (murd/murd.c: MURD_tickScene, map.c: "
           "changeToDeathMap), which "
           "runs other scripts",
       .set = WriteSet::Any,
       .triggers = {}},
      {.op = CallDigimonRoutineOp,
       .routine = 9,
       .source =
           "tamer.c: tamerTickBattleLostLife reaches the death map change (murd/murd.c: MURD_tickScene, "
           "map.c: changeToDeathMap), which runs "
           "other scripts",
       .set = WriteSet::Any,
       .triggers = {}},
  };
}

} // namespace

const std::vector<RoutineWrites>& routineWrites() {
  static const std::vector<RoutineWrites> writes = table();
  return writes;
}

const RoutineWrites* routineWrites(std::string_view op, std::uint8_t routine) {
  const auto& writes = routineWrites();
  const auto found = std::ranges::find_if(writes, [&](const RoutineWrites& entry) {
    return entry.op == op && entry.routine == routine;
  });
  return found == writes.end() ? nullptr : &*found;
}

} // namespace dwe::formats::script
