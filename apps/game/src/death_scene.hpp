#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/life_rules.hpp>
#include <dwe/game/story/partner_routines.hpp>
#include <dwe/screens/lives_box.hpp>
#include <dwe/session/fishing_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "death_effects.hpp"
#include "field_scene.hpp"
#include "scene_sounds.hpp"

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
  // Where the lost life's lives box shows; nullptr shows none.
  ui::Ui* ui = nullptr;
  // The species' heights, for the partner's middle the lives box grows from; nullptr grows it from
  // the partner's feet.
  const assets::DigimonTable* species = nullptr;
  // The map's fishing, which stops while a death's scene runs and comes back once it, and the
  // rebirth, end (partnerTickDying, partnerTickDying2); nullptr for none.
  session::FishingAdapter* fishing = nullptr;
};

// The lost life's lives box on its document (ui/lives_box/document).
inline constexpr std::string_view LivesBoxDocument = "lives_box";

// The scenes of a lost life (murd.c: MURD_tickScene), a death (dooa.c: DOOA_tickDissolve) and the
// rebirth that follows it (DOOA_tickRebirth): an iris closes on the partner, which stands alone on
// black; at a death the camera comes round to it, its body breaks into wireframe and shards, a
// light comes down and the shards flicker; the player wakes up on the wake map; at the rebirth the
// shards close into an egg, which flashes and breaks around the new baby while the scenery comes
// back. The session already took the life, or killed the partner and gave it its new form; the
// scenes only show it.
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
  // The wake map's first character (ENTITY_TABLE[2]), if it has one.
  [[nodiscard]] std::optional<scene::Entity> wakeHost() const;
  // A projection centred on the screen, moved right and down by the original's pixels (the
  // drawing offset away from 160, 120), at the projection distance `focalLength`.
  [[nodiscard]] glm::mat4 centredProjection(float focalLength, std::int32_t offsetX,
                                            float offsetY) const;
  void enterWakeMap();
  void finish();
  void setPhase(Phase phase);
  // The scene's sounds of slot 8, kept until stopSounds (sound.c: stopSound stops them all).
  void playSound(std::int32_t sound);
  void stopSounds();
  // Draws the scene's primitives (the iris, the effects) into the stage and shows it.
  void showEffects();
  // The iris on this frame of its close to `end` (MURD_renderIris, DOOA_renderIrisWindow).
  void showIris(std::int32_t end);
  // DOOA_updateCutsceneCamera: from the start view to round the partner standing at `feet` facing
  // `yaw`, over the frames from `start` to `end`.
  void moveCamera(glm::vec3 feet, float yaw, std::int32_t start, std::int32_t end,
                  std::int32_t frame);
  // The squash and stretch of the dissolve's phase 0x64 (DOOA_DISSOLVE_SCALE_CURVE) and the leap.
  void squash();
  [[nodiscard]] float partnerHeight() const;
  [[nodiscard]] float partnerRadius() const;
  void showStage();
  void setShade(std::int32_t shade);
  // Whether a moment that runs once has come: the frame reached it and it has not run yet.
  [[nodiscard]] bool reached(std::int32_t frame) const;
  // The partner's middle on the screen (murd.c: MURD_createLivesBox projects it), in percent of
  // the window; the window's middle when it is behind the camera.
  [[nodiscard]] screens::WindowFraction partnerPoint() const;
  // Plays the lives box's sound and shows it as it is this frame.
  void showLivesBox();

  session::GameSession* session_;
  session::FishingAdapter* fishing_;
  FieldScene* scene_;
  ui::Ui* ui_;
  const assets::DigimonTable* species_;
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
  screens::LivesBox livesBox_;
  // The death's and the rebirth's effects, with their own rand() state for each scene.
  DeathEffects effects_;
  std::uint32_t effectSeed_ = 0;
  HeldSceneSounds sounds_;
  // The iris's primitives this frame.
  std::vector<ScenePrim> iris_;
  // The camera the scene's camera moves from (DOOA_CAMERA_START_VIEW and its offsets and
  // distance), and the one it shows: its eye and target, distance, how far the picture moved from
  // the map's off-centre projection to a centred one and down by DRAWING_OFFSET_Y.
  struct SceneView {
    glm::vec3 eye{0.0F};
    glm::vec3 target{0.0F, 0.0F, -1.0F};
    glm::mat4 projection{1.0F};
    float focalLength = 1000.0F;
  };
  SceneView startView_;
  // The projection distance of the camera shown (VIEWPORT_DISTANCE), which sizes the flashes.
  float cameraFocal_ = 1000.0F;
  // Where the partner stood when the death started (DOOA_SAVED_LOCATION and its rotation).
  glm::vec3 savedFeet_{0.0F};
  float savedYaw_ = 0.0F;
  // Where it stands from frame 0x56 on, facing the player.
  float sceneYaw_ = 0.0F;
  // The egg of TAMA.TMD the rebirth shows (the baby's place among the rebirth's babies).
  std::int32_t eggSlot_ = 0;
};

} // namespace dwe::app
