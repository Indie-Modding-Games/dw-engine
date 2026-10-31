#pragma once

#include <dwe/game/field/boss_arrival.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/session/story_props.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace dwe::game::story {
class SoundPort;
} // namespace dwe::game::story

namespace dwe::app {

class FieldScene;

// The app's side of the final boss's charge (story routine 40: eab.c: EAB_startBuildup) over
// game::field::BossArrival: it keeps the boss off the drawn map, draws the rings of ETCHI\BOSS_EFE
// round where it stands and plays the hum, stepping once an original frame. Entering a map takes
// it all away.
class BossArrivalScene {
public:
  // The scene must outlive this object.
  explicit BossArrivalScene(FieldScene& scene);

  // Where the hum and the other sounds play; none plays without it.
  void setSound(game::story::SoundPort* sound) { sound_ = sound; }

  // Routine 40's request.
  void startBuildup(const session::BossBuildupStarted& request);
  // Steps the charge by the frame's time, unless paused.
  void advance(double seconds);
  // Hands the scene this frame's rings and hidden boss.
  void show();

private:
  void play(const game::field::BossArrivalCues& cues);
  void stopSounds();
  [[nodiscard]] std::optional<scene::Entity> boss() const;
  void leaveMapIfChanged();

  FieldScene* scene_;
  game::story::SoundPort* sound_ = nullptr;
  game::field::BossArrival arrival_;
  // The boss's script id and the map it charges on.
  std::optional<std::int64_t> character_;
  std::string mapId_;
  // The held sound playing (playSound2): the hum, then the rise.
  std::optional<std::int64_t> held_;
  double seconds_ = 0.0;
};

} // namespace dwe::app
