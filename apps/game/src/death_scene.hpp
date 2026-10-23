#pragma once

#include <dwe/game/field/rules.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/life_rules.hpp>
#include <dwe/game/story/partner_routines.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_scenes.hpp>

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

struct DeathSceneDesc {
  session::GameSession* session = nullptr;
  FieldScene* scene = nullptr;
  // The story's triggers and variables pick the wake map; nullptr wakes on the early one.
  const session::StoryAdapter* story = nullptr;
  // When the story plays the deaths (StoryScenes::playsDeath), the scenes start when it asks;
  // nullptr, or a story that does not, starts them from the partner's events.
  const session::StoryScenes* scenes = nullptr;
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

  // Whether a scene runs or waits to.
  [[nodiscard]] bool active() const { return phase_ != Phase::Idle || !pending_.empty(); }
  // Whether the field and the game time wait for the scene: a scene the story asked for leaves
  // them to the story's script, which holds the player itself.
  [[nodiscard]] bool holdsField() const { return active() && !storyScene_; }

  // How much the scene's fade takes away from the screen, from 0 (nothing) to 1 (black).
  [[nodiscard]] float fadeLevel() const { return fade_; }

  // Takes the partner's lost lives, deaths and rebirths for the scenes and returns the events left
  // for showPartnerEvents.
  [[nodiscard]] std::vector<game::partner::PartnerEvent>
  take(std::span<const game::partner::PartnerEvent> events);

  // Runs the scene; a scene taken during a battle starts once the field is back.
  void tick(double seconds);

  // Plays a scene of call_partner_routine the story asked for: 9 and 7, a lost life (murd.c), 3, a
  // death (dooa.c: DOOA_tick), and 4, the rebirth (partnerTickDying2). False for another scene, or
  // with no scenes in the rules.
  bool play(game::story::PartnerScene scene);
  // Whether the scene the story waits on is over, once. A death's is over when the partner is gone,
  // and the scene waits on black for the rebirth.
  [[nodiscard]] bool takeSceneOver() { return std::exchange(sceneOver_, false); }

private:
  enum class Phase : std::uint8_t {
    Idle,
    // MURD_tickScene.
    LostLifeAlone,
    LostLifeFadeOut,
    LostLifeFadeIn,
    // DOOA_tickDissolve.
    Dissolve,
    // The story's death script runs between the death and the rebirth (the egg box).
    AwaitRebirth,
    // DOOA_tickRebirth, and the wait for its last clip.
    Rebirth,
    RebirthFinish
  };

  enum class SceneKind : std::uint8_t { LostLife, Death, Rebirth };

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
  void setDeathMap();
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
  const session::StoryScenes* scenes_;
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
  // The map the tamer wakes up on, picked when the scene starts.
  std::int32_t deathMap_ = 0;
  double pendingSeconds_ = 0.0;
  float fade_ = 0.0F;
  FieldStage stage_;
  std::int32_t waitFrames_ = 0;
  // Whether the scene is one the story asked for, and whether the story's wait on it is over.
  bool storyScene_ = false;
  bool sceneOver_ = false;
};

} // namespace dwe::app
