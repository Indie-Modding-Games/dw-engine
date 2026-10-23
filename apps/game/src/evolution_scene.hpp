#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/evolution/rules.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/session/game_session.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

struct EvolutionSceneDesc {
  session::GameSession* session = nullptr;
  FieldScene* scene = nullptr;
  // The sizes of the forms (DIGIMON_DATA radius and height); nullptr keeps the partner's size.
  const assets::DigimonTable* species = nullptr;
  // The ruleset's scene; nullptr draws a new form at once, with no scene.
  const game::evolution::EvolutionSceneRules* rules = nullptr;
  // The length of one frame of the original (field rules, transition.frame_seconds).
  float frameSeconds = 0.0F;
};

// The evolution scene on the field (partner.c: partnerTickEvolving; main/evl.c:
// tickEvoSequenceLoading; evl.c: EVL_tickEvoSequence): the field and the game time wait while the
// scenery goes, the camera closes in on the partner and circles it, the partner bursts, grows to
// its new form's size and is drawn as that form on the original's frame. The session evolved the
// partner already; the scene only shows it.
class EvolutionScene {
public:
  // The desc's pointees must outlive the scene.
  explicit EvolutionScene(const EvolutionSceneDesc& desc);

  // Whether the field and the game time wait for the scene.
  [[nodiscard]] bool active() const { return phase_ != Phase::Idle; }

  // Takes the partner's evolutions (Evolved) for the scene and returns the events left for
  // showPartnerEvents; a rebirth (Reborn) tells the scene the form the field now draws.
  [[nodiscard]] std::vector<game::partner::PartnerEvent>
  take(std::span<const game::partner::PartnerEvent> events);

  void tick(double seconds);

private:
  // The original's states: the two waits of tickEvoSequenceLoading, then EVL_tickEvoSequence's
  // 0 (dimming), 1 (camera), 2 (burst), 3 (camera back) and 4 (the last clip).
  enum class Phase : std::uint8_t {
    Idle,
    Glowing,
    Gathering,
    Dimming,
    Closing,
    Bursting,
    Restoring,
    Finishing
  };

  // A form's sizes in the original's units.
  struct Size {
    float radius = 0.0F;
    float height = 0.0F;
  };

  // Where the camera circling the partner stands and looks.
  struct Orbit {
    glm::vec3 eye{0.0F};
    glm::vec3 target{0.0F};
  };

  void start(const std::string& target);
  void step();
  void stepClosing();
  void stepBursting();
  void restore();
  void finish();
  void setPhase(Phase phase);
  void playSound(std::int32_t sound);
  void drawNewForm();
  [[nodiscard]] Size sizeOf(std::string_view species) const;
  // The partner's size on the scene's frame: the old form's, then growing to the new one's.
  [[nodiscard]] Size grownSize() const;
  // EVL_calculateCameraVectors: the camera circling the partner at the current angle.
  [[nodiscard]] Orbit orbitAt(Size size) const;
  void showCamera();

  session::GameSession* session_;
  FieldScene* scene_;
  const assets::DigimonTable* species_;
  const game::evolution::EvolutionSceneRules* rules_;
  double frameSeconds_;

  Phase phase_ = Phase::Idle;
  std::int32_t frame_ = 0;
  double pendingSeconds_ = 0.0;
  // The form the field draws, and the one the scene ends on.
  std::string drawn_;
  std::string target_;
  FieldStage stage_;
  std::int32_t orbitAngle_ = 0;
  // The map camera when the scenery went (EVL_D_800688E8, DRAWING_OFFSET, VIEWPORT_DISTANCE).
  glm::vec3 mapEye_{0.0F};
  glm::vec3 mapTarget_{0.0F};
  glm::mat4 mapProjection_{1.0F};
  float mapFocalLength_ = 1.0F;
  // The scene's camera (GS_VIEWPOINT): where it stands and looks, its projection distance, how far
  // the picture drops and how far it has moved from the map's off-centre projection to a centred
  // one (DRAWING_OFFSET to 160, 120), from 0 to 1.
  Orbit camera_;
  float focalLength_ = 1.0F;
  float drop_ = 0.0F;
  float recentre_ = 0.0F;
  // The burst clip ends in the ready one (EVL_tickEvoSequence's first check).
  bool burstClipPlaying_ = false;
  std::int32_t waitFrames_ = 0;
};

} // namespace dwe::app
