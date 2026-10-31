#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/story/partner_routines.hpp>
#include <dwe/session/game_session.hpp>

#include <cstdint>
#include <vector>

#include "ending_effects.hpp"

namespace dwe::app {

class FieldScene;

struct EndingSceneDesc {
  session::GameSession* session = nullptr;
  FieldScene* scene = nullptr;
  // The player's bone count is the species table's first row (the player's model); nullptr takes
  // the original's.
  const assets::DigimonTable* species = nullptr;
  // The field's frame (the scene runs a frame of the original at a time).
  double frameSeconds = 0.05;
};

// The game's last scene, call_partner_routine 6 (partner.c: callDigimonRoutine), which routine 53
// starts and waits for: the player's ending tick plays endi.c (EndingEffects) on the field, the
// player turning into light that rises; the story goes on once it is over.
class EndingScene {
public:
  // The desc's pointees must outlive the scene.
  explicit EndingScene(const EndingSceneDesc& desc);

  // Starts the scene for call_partner_routine 6; false for another scene.
  bool play(game::story::PartnerScene scene);
  [[nodiscard]] bool active() const { return running_; }
  // Runs the scene's frames; the player is held by the story's script meanwhile.
  void tick(double seconds);
  // Whether the scene ended since the last call (SOME_SCRIPT_SYNC_BIT).
  [[nodiscard]] bool takeOver() { return std::exchange(over_, false); }

private:
  void step();
  void show();
  void playSounds();
  [[nodiscard]] std::optional<OriginalPoint> place(std::int32_t bone, OriginalPoint offset,
                                                   std::int32_t scale) const;

  session::GameSession* session_;
  FieldScene* scene_;
  double frameSeconds_;
  EndingEffects effects_;
  bool running_ = false;
  bool over_ = false;
  double pendingSeconds_ = 0.0;
  std::vector<std::int64_t> heldSounds_;
};

} // namespace dwe::app
