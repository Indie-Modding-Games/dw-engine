#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <cstdint>
#include <optional>

// call_partner_routine: the scenes a script starts on the partner and the player (partner.c:
// callDigimonRoutine). The order freezes the world, starts the scene and goes on; the script waits
// for the scene with wait_for_entity(255), which reads the scene sync (SOME_SCRIPT_SYNC_BIT).
namespace dwe::game::story {

// The scenes, by the original's routine number.
enum class PartnerScene : std::uint8_t {
  Toilet = 0,           // partner state 6
  Curling = 1,          // the curling game (KAR_start), tamer 12, partner 11
  ReturnFromVirus = 2,  // evolves into the form kept before the virus (pstat 5), partner 13
  Dying = 3,            // partner state 8
  LostAllLives = 4,     // partner state 14
  Opening = 5,          // tamer 15 (tamerTickOpening), partner 11
  Ending = 6,           // tamer 16 (tamerTickEnding), partner 11
  SicknessLostLife = 7, // tamer 17, partner 12
  BossAppears = 8,      // tamer 18 (EAB_tick), partner 11
  BattleLostLife = 9,   // tamer 19, partner 12
  EvolveTo = 10,        // evolves into the species in a variable (pstat 254), partner 13
};

// Whether the scene the script waits for is over: the scene's owner finishes it.
class SceneSync {
public:
  void begin() { done_ = false; }
  void finish() { done_ = true; }
  [[nodiscard]] bool done() const { return done_; }

private:
  // The original sets the bit when a section starts (script_interp.c:2266).
  bool done_ = true;
};

class PartnerScenePort {
public:
  PartnerScenePort() = default;
  PartnerScenePort(const PartnerScenePort&) = delete;
  PartnerScenePort& operator=(const PartnerScenePort&) = delete;
  PartnerScenePort(PartnerScenePort&&) = delete;
  PartnerScenePort& operator=(PartnerScenePort&&) = delete;
  virtual ~PartnerScenePort() = default;

  // Starts the scene; `species` is the target form's number for the evolutions. The owner calls
  // SceneSync::finish once it is over.
  virtual void startScene(PartnerScene scene, std::optional<std::int64_t> species) = 0;
};

// The variables the evolutions read their target from.
struct PartnerRoutineRules {
  std::int64_t virusBackupVariable = 0;
  std::int64_t targetVariable = 0;
};

class PartnerRoutines {
public:
  // The ports and the sync must outlive the routines.
  PartnerRoutines(StoryStatePort& state, PartnerScenePort& scenes, SceneSync& sync,
                  PartnerRoutineRules rules);

  // call_partner_routine. The caller froze the world. A number with no scene does nothing, as the
  // original's switch does (routine 214 before a gym's training).
  core::Result<void> call(std::int64_t routine);

private:
  StoryStatePort* state_;
  PartnerScenePort* scenes_;
  SceneSync* sync_;
  PartnerRoutineRules rules_;
};

} // namespace dwe::game::story
