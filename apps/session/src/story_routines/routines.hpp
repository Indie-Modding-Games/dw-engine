#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/story_routines.hpp>
#include <dwe/session/story_scenes.hpp>

#include <cstdint>

// The call_routine handlers, one file each (script_interp.c: scriptInstruction64to7E and
// tickScript, SCRIPT_OP_CALL_ROUTINE). A lane that adds routines adds its file here and one line
// to registerOpeningRoutines or its own group (_work/logs/p4-hist-s6/routine-api.md).
namespace dwe::session {

struct StoryScenesContext;

// A routine that opens a screen and, when `waits`, holds the section until the app closes it.
core::Result<void> registerScreenRoutine(StoryScenesContext& scenes,
                                         game::story::RoutineNumber number, StoryScreen screen,
                                         bool waits);

core::Result<void> registerDiscardItemRoutine(StoryScenesContext& scenes);
core::Result<void> registerMeritShopRoutine(StoryScenesContext& scenes);
core::Result<void> registerTradeShopRoutine(StoryScenesContext& scenes);
core::Result<void> registerTournamentRoutine(StoryScenesContext& scenes);
core::Result<void> registerShopRoutine(StoryScenesContext& scenes);
core::Result<void> registerJukeboxRoutine(StoryScenesContext& scenes);
core::Result<void> registerItemKeeperRoutine(StoryScenesContext& scenes);
core::Result<void> registerRemoveItemRoutines(StoryScenesContext& scenes);
core::Result<void> registerCardShopRoutine(StoryScenesContext& scenes);
core::Result<void> registerMoneyBoxRoutine(StoryScenesContext& scenes);
core::Result<void> registerCloseTextBoxRoutine(StoryScenesContext& scenes);
core::Result<void> registerTournamentScheduleRoutine(StoryScenesContext& scenes);
core::Result<void> registerObjectAnimationRoutine(StoryScenesContext& scenes);
core::Result<void> registerTournamentMedalRoutine(StoryScenesContext& scenes);
core::Result<void> registerVariableAtStartRoutine(StoryScenesContext& scenes);
core::Result<void> registerEggBoxRoutine(StoryScenesContext& scenes);
core::Result<void> registerShopTallyRoutines(StoryScenesContext& scenes);
core::Result<void> registerShopOverlayRoutine(StoryScenesContext& scenes);
core::Result<void> registerSmokeEffectRoutine(StoryScenesContext& scenes);
core::Result<void> registerAnimationLoopRoutine(StoryScenesContext& scenes);
core::Result<void> registerNewGameMenuRoutine(StoryScenesContext& scenes);
core::Result<void> registerForgetMovesRoutine(StoryScenesContext& scenes);
core::Result<void> registerSaveMenuRoutine(StoryScenesContext& scenes);
core::Result<void> registerGymRoutines(StoryScenesContext& scenes);
core::Result<void> registerDirtCartRoutines(StoryScenesContext& scenes);
core::Result<void> registerScreenShakeRoutine(StoryScenesContext& scenes);
core::Result<void> registerAngemonPedestalRoutines(StoryScenesContext& scenes);
core::Result<void> registerToyTownRoutines(StoryScenesContext& scenes);
core::Result<void> registerBeetleCupEntryRoutine(StoryScenesContext& scenes);

// What routine 33 writes for an entry on `day` (from 0) with the partner of species number
// `partnerSpecies`: variables 2, 3 and 4.
struct BeetleCupEntry {
  std::int64_t entryDay = 0;
  std::int64_t cup = 0;
  std::int64_t entrant = 0;
};
BeetleCupEntry beetleCupEntry(std::int32_t day, std::int64_t partnerSpecies);

} // namespace dwe::session
