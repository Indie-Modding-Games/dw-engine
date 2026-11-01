#pragma once

#include <dwe/game/field/boss_arrival.hpp>
#include <dwe/render/camera.hpp>
#include <dwe/ruleset/random.hpp>
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

// The app's side of the final boss's charge (story routine 40: eab.c: EAB_startBuildup) and arrival
// (the partner's routine 8: EAB_tick, EAB_tickSpawn) over game::field::BossArrival, stepping once
// an original frame. The charge keeps the boss off the drawn map, draws the rings of ETCHI\BOSS_EFE
// round where it stands and plays the hum. The arrival blacks the screen out, shows the boss alone
// on black (FieldStage) with the camera going round it, the rings, the light squares and its
// animations and sounds, and then gives the field back. Entering a map takes it all away.
class BossArrivalScene {
public:
  // The scene must outlive this object.
  explicit BossArrivalScene(FieldScene& scene);

  // Where the hum and the other sounds play; none plays without it.
  void setSound(game::story::SoundPort* sound) { sound_ = sound; }

  // Routine 40's request.
  void startBuildup(const session::BossBuildupStarted& request);
  // The partner's routine 8 (tamer.c: tamerTickMachinedramonSpawn): the boss is the charge's, or
  // the map's first NPC (ENTITY_TABLE[2]).
  void startArrival();
  // Whether the arrival ended since the last call: the script's wait is over.
  [[nodiscard]] bool takeOver();
  // Steps the charge by the frame's time, unless paused.
  void advance(double seconds);
  // Hands the scene this frame's rings, hidden boss and stage.
  void show();

private:
  void play(const game::field::BossArrivalCues& cues);
  void stopSounds();
  [[nodiscard]] std::optional<scene::Entity> boss() const;
  // The boss's scripted animation when its clip played through (anim.c: animFrame == frameCount).
  [[nodiscard]] std::optional<std::int32_t> finishedAnimation() const;
  void leaveMapIfChanged();
  void showStage(std::optional<scene::Entity> entity);
  [[nodiscard]] std::optional<render::Camera> orbitCamera(scene::Entity entity) const;

  FieldScene* scene_;
  game::story::SoundPort* sound_ = nullptr;
  game::field::BossArrival arrival_;
  // The boss's script id and the map it charges on.
  std::optional<std::int64_t> character_;
  std::string mapId_;
  // The held sound playing (playSound2): the hum, then the rise.
  std::optional<std::int64_t> held_;
  double seconds_ = 0.0;
  // Whether the field shows this scene's stage, to give it back once.
  bool staged_ = false;
  // Whether the story waits for the arrival.
  bool waiting_ = false;
  // rand() for renderWireframed's grey.
  ruleset::Random wireRandom_{0};
};

} // namespace dwe::app
