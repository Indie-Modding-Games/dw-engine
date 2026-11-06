#include <array>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

core::Result<void> registerOpeningRoutines(StoryScenesContext& scenes) {
  using Register = core::Result<void> (*)(StoryScenesContext&);
  // One line per routine file.
  constexpr std::array<Register, 37> Routines{
      &registerDiscardItemRoutine,
      &registerMeritShopRoutine,
      &registerTradeShopRoutine,
      &registerTournamentRoutine,
      &registerShopRoutine,
      &registerJukeboxRoutine,
      &registerItemKeeperRoutine,
      &registerRemoveItemRoutines,
      &registerCardShopRoutine,
      &registerMoneyBoxRoutine,
      &registerCloseTextBoxRoutine,
      &registerTournamentScheduleRoutine,
      &registerObjectAnimationRoutine,
      &registerTournamentMedalRoutine,
      &registerVariableAtStartRoutine,
      &registerEggBoxRoutine,
      &registerShopTallyRoutines,
      &registerShopOverlayRoutine,
      &registerSmokeEffectRoutine,
      &registerAnimationLoopRoutine,
      &registerNewGameMenuRoutine,
      &registerForgetMovesRoutine,
      &registerSaveMenuRoutine,
      &registerClearSaveRoutine,
      &registerGymRoutines,
      &registerDirtCartRoutines,
      &registerScreenShakeRoutine,
      &registerAngemonPedestalRoutines,
      &registerToyTownRoutines,
      &registerBeetleCupEntryRoutine,
      &registerRemoveMistRoutine,
      &registerBossAndEndingLoadRoutines,
      &registerStartEndingRoutine,
      &registerWhiteFadeRoutines,
      &registerMoodBubbleRoutine,
      &registerBirdTransportRoutine,
      &registerBossBuildupRoutine,
  };
  for (const Register add : Routines) {
    if (auto added = add(scenes); !added) {
      return added;
    }
  }
  return {};
}

} // namespace dwe::session
