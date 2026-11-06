#include <dwe/core/log.hpp>
#include <dwe/session/game_session.hpp>

#include <array>
#include <cstdint>
#include <utility>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber LoadBoss = 51;
constexpr game::story::RoutineNumber LoadEnding = 52;

// script_interp.c:1846 and 1853: the sound banks the routines load into slot 8
// (loadMapSounds2).
constexpr std::int32_t BossSounds = 0x15;
constexpr std::int32_t EndingSounds = 0x14;
// script_interp.c:1845: routine 51 also loads the final boss's own bank, the vball entry of species
// 0x73, into slot 5, where its clips' sounds play (eab.c: EAB_tickSpawn gives it vabId 5).
constexpr std::int64_t BossSpeciesSlot = 5;
constexpr std::int32_t BossSpecies = 0x73;

} // namespace

std::optional<std::int32_t> loadRoutineSounds(game::story::RoutineNumber routine) {
  if (routine == LoadBoss) {
    return BossSounds;
  }
  if (routine == LoadEnding) {
    return EndingSounds;
  }
  return std::nullopt;
}

std::optional<RoutineSpeciesBank> loadRoutineSpeciesBank(game::story::RoutineNumber routine) {
  if (routine == LoadBoss) {
    return RoutineSpeciesBank{.slot = BossSpeciesSlot, .type = BossSpecies};
  }
  return std::nullopt;
}

void dropRoutineSoundsOnNewMap(StoryScenesContext& scenes, GameSession& session) {
  if (!scenes.routineSoundsMap) {
    return;
  }
  const std::optional<FieldPlace> place = session.currentPlace();
  if (place && place->map == *scenes.routineSoundsMap) {
    return;
  }
  // map.c: loadMap puts the new map's bank in slot 8 again.
  scenes.routineSoundsMap.reset();
  session.useSceneSounds(std::nullopt);
}

// Routines 51 and 52 (script_interp.c: call_routine 0x33 and 0x34): the final boss's files
// (BOSS_EFE.TMD, the EAB overlay, its sounds) on mgen99 and the ending's (its image, the ENDI
// overlay, its sounds) on the ending's map. The engine reads files when it needs them; what is
// left are the sound banks: slot 8's, which stays until a map loads or a battle is won, and for
// the boss its own in slot 5. Both go on in the same frame.
core::Result<void> registerBossAndEndingLoadRoutines(StoryScenesContext& scenes) {
  auto load =
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
    const std::optional<std::int32_t> bank = loadRoutineSounds(routine);
    core::logDebug("[port:story] call_routine {} (script_interp.c:1846): sound bank {:#x} in "
                   "slot 8",
                   routine, bank.value_or(-1));
    if (scenes.session != nullptr) {
      const std::optional<FieldPlace> place = scenes.session->currentPlace();
      scenes.routineSoundsMap = place ? place->map : std::string{};
      scenes.session->useSceneSounds(bank);
      if (const std::optional<RoutineSpeciesBank> species = loadRoutineSpeciesBank(routine)) {
        scenes.session->useSpeciesSounds(species->slot, species->type);
      }
    }
    return game::story::RoutineGoesOn{};
  };
  for (const game::story::RoutineNumber number : {LoadBoss, LoadEnding}) {
    if (auto added = scenes.routines.registerRoutine(number, load); !added) {
      return added;
    }
  }
  return {};
}

} // namespace dwe::session
