#pragma once

#include <dwe/game/field/rules.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/life_rules.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

struct DeathSceneDesc {
  session::GameSession* session = nullptr;
  FieldScene* scene = nullptr;
  // The story's triggers and variables pick the wake map; nullptr wakes on the early one.
  const session::StoryAdapter* story = nullptr;
  // The field's fades: the length of a frame and the shades of fade.c.
  game::field::TransitionRules transition;
};

// The scenes of a lost life (murd.c: MURD_tickScene), a death (dooa.c: DOOA_tickDissolve) and the
// rebirth that follows it (DOOA_tickRebirth): the field and the game time wait while the partner
// stands alone on black, the tamer wakes up on the wake map and, after a death, the new baby is
// drawn on the original's frame. The session already took the life, or killed the partner and gave
// it its new form; the scenes only show it.
class DeathScene {
public:
  // The desc's pointees must outlive the scene. The scenes are the session's life rules'
  // (life.scenes); without them the partner changes at once.
  explicit DeathScene(const DeathSceneDesc& desc);

  // Whether the field and the game time wait for the scene.
  [[nodiscard]] bool active() const { return phase_ != Phase::Idle || !pending_.empty(); }

  // How much the scene's fade takes away from the screen, from 0 (nothing) to 1 (black).
  [[nodiscard]] float fadeLevel() const { return fade_; }

  // Takes the partner's lost lives, deaths and rebirths for the scenes and returns the events left
  // for showPartnerEvents.
  [[nodiscard]] std::vector<game::partner::PartnerEvent>
  take(std::span<const game::partner::PartnerEvent> events);

  // Runs the scene; a scene taken during a battle starts once the field is back.
  void tick(double seconds);

private:
  enum class Phase : std::uint8_t {
    Idle,
    // MURD_tickScene.
    LostLifeAlone,
    LostLifeFadeOut,
    LostLifeFadeIn,
    // DOOA_tickDissolve.
    Dissolve,
    // DOOA_tickRebirth, and the wait for its last clip.
    Rebirth,
    RebirthFinish
  };

  enum class SceneKind : std::uint8_t { LostLife, Death };

  struct Pending {
    SceneKind kind = SceneKind::LostLife;
    std::int32_t lives = 0;
  };

  void begin(const Pending& pending);
  void step();
  void stepLostLife();
  void stepDissolve();
  void stepRebirth();
  void startDissolve(bool livesLeft);
  void enterWakeMap();
  void finish();
  void setPhase(Phase phase);
  void playSound(std::int32_t sound);
  void showStage();
  void setShade(std::int32_t shade);
  // Whether a moment that runs once has come: the frame reached it and it has not run yet.
  [[nodiscard]] bool reached(std::int32_t frame) const;

  session::GameSession* session_;
  FieldScene* scene_;
  const session::StoryAdapter* story_;
  // The scenes' rules as they were when the scene started: a tuned ruleset changes the next one.
  // Only read while a scene runs, which begin() starts after copying them.
  game::partner::LifeScenes rules_;
  game::field::TransitionRules transition_;

  std::deque<Pending> pending_;
  // A LostLife with no lives left: the death that follows starts from the lost life's scene.
  bool lastLifeLost_ = false;
  // The form the scene ends on (the reborn baby).
  std::string baby_;
  Phase phase_ = Phase::Idle;
  std::int32_t frame_ = 0;
  std::int32_t lives_ = 0;
  double pendingSeconds_ = 0.0;
  float fade_ = 0.0F;
  FieldStage stage_;
  std::int32_t waitFrames_ = 0;
};

} // namespace dwe::app
