#pragma once

#include <dwe/core/math.hpp>
#include <dwe/game/story/frame_systems.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <variant>

// The story's moves of the characters and the camera (script_interp.c: scriptInstruction46to58,
// scriptInstruction64to7E, tickScriptedMovement): an order fills the move slot of a character, or
// the camera's, and the slot is ticked once a frame of the original until its move is done;
// wait_for_entity waits for slots to empty. Positions and turns are in world space; the orders take
// the original's operands and turn them into it.
namespace dwe::game::story {

// The script ids of the player and the partner (script_value.c: scriptIdToEntityId); an NPC has
// the id its map gives it.
inline constexpr std::int64_t PlayerScriptId = 0xFD;
inline constexpr std::int64_t PartnerScriptId = 0xFC;

// How a scripted glide goes with the clip the character plays: a walk stands for the walk clip's
// root motion (tamer.c: tickEntityWalkTo only turns the character, the clip carries it); a move
// adds to it (tickEntityMoveTo and tickEntityMoveToAxis step the position while anim.c:
// tickAnimation still applies the clip's root motion).
enum class Glide : std::uint8_t { Walk, Move };

// The characters and the camera the story moves. The session implements it on the field; the
// characters are named by script id.
class ScriptActorsPort {
public:
  ScriptActorsPort() = default;
  ScriptActorsPort(const ScriptActorsPort&) = delete;
  ScriptActorsPort& operator=(const ScriptActorsPort&) = delete;
  ScriptActorsPort(ScriptActorsPort&&) = delete;
  ScriptActorsPort& operator=(ScriptActorsPort&&) = delete;
  virtual ~ScriptActorsPort() = default;

  // The move slot of a character on the map: 0 the player, 1 the partner, from 2 the NPCs;
  // nothing when it is not on the map (script_value.c: scriptIdToEntityId).
  [[nodiscard]] virtual std::optional<std::int64_t> slotOf(std::int64_t character) const = 0;
  [[nodiscard]] virtual glm::vec3 position(std::int64_t character) const = 0;
  [[nodiscard]] virtual float yaw(std::int64_t character) const = 0;
  virtual void turn(std::int64_t character, float yaw) = 0;
  virtual void place(std::int64_t character, glm::vec3 position) = 0;
  // Moves the character at `velocity` (world units a second) with no collision until told
  // otherwise; zero stops it.
  virtual void drive(std::int64_t character, glm::vec3 velocity, Glide glide) = 0;
  // Plays the clip with the original's animation id; the script holds the character until it lets
  // go (tamer.c: tamerSetState, partner.c: partnerSetState).
  virtual void animate(std::int64_t character, std::int64_t animation) = 0;
  // How fast the clip's root carries the character over the ground, world units a second: the
  // original walks a scripted character by its animation (anim.c: applyRootMomentum).
  [[nodiscard]] virtual float groundSpeed(std::int64_t character, std::int64_t animation) const = 0;
  // Whether the character, `step` further ahead, would touch another character (map.c:
  // entityCheckCollision).
  [[nodiscard]] virtual bool wouldTouchCharacter(std::int64_t character, float step) const = 0;
  // Pans the camera's focus to `focus` over `seconds` (map.c: tickCameraMovement).
  virtual void panCamera(glm::vec3 focus, float seconds) = 0;
  [[nodiscard]] virtual bool cameraPanning() const = 0;
  // Moves the camera by `by` (map.c: moveCameraByDiff, which a move with the camera calls each
  // frame with the mover's step).
  virtual void shiftCamera(glm::vec3 by) = 0;
  // Makes the NPC's place on the map where it stands now, and shifts its route the same
  // (overworld.c: resetEntityOrigin).
  virtual void resetOrigin(std::int64_t character) = 0;
  // Ticks one frame of the turn or slide of a 3D object of the map, toward `direction` (-1 or 1);
  // true when it is done (map_object.c: tickRotateDoor).
  [[nodiscard]] virtual bool turnMapObject(std::int64_t object, std::int64_t direction) = 0;
  // The place of a sprite object of the map in background pixels, nothing when the map has no
  // such object; and moving it (overworld.c: MAP_TILE_DATA.objects, tickMoveObjectTo).
  [[nodiscard]] virtual std::optional<glm::ivec2> mapSpritePosition(std::int64_t object) const = 0;
  virtual void placeMapSprite(std::int64_t object, glm::ivec2 position) = 0;
};

// The rules of the scripted moves, from the field rules.
struct MotionRules {
  // One frame of the original.
  float frameSeconds = 0.0F;
  // How far entity_look_at_entity turns a character each frame, in radians.
  float lookTurnPerFrame = 0.0F;
};

class ScriptMotion final : public ScriptFrameSystem {
public:
  // The original's slot of the camera (SCRIPT_MOVEMENT[10]).
  static constexpr std::int64_t CameraSlot = 10;

  // The port must outlive the moves.
  ScriptMotion(ScriptActorsPort& actors, MotionRules rules);

  // entity_look_at_entity: turns the character to where `target` stands now.
  void lookAt(std::int64_t character, std::int64_t target);
  // entity_set_rotation: the original's rotation, 4096 a turn.
  void setRotation(std::int64_t character, std::int64_t rotation);
  // entity_walk_to: walks with the walk animation `animation` to the cell of the point (x, z).
  void walkTo(std::int64_t character, std::int64_t x, std::int64_t z, std::int64_t animation);
  // entity_walk_to_entity: walks to the cell of `target`, or until it touches a character.
  void walkToCharacter(std::int64_t character, std::int64_t animation, std::int64_t target);
  // entity_walk_to_with_camera and entity_walk_to_entity_with_camera: the same, and the camera
  // follows the walker (tamer.c: tickEntityWalkTo with useCamera).
  void walkToWithCamera(std::int64_t character, std::int64_t x, std::int64_t z,
                        std::int64_t animation);
  void walkToCharacterWithCamera(std::int64_t character, std::int64_t animation,
                                 std::int64_t target);
  // entity_move_to: slides to (x, z) in `frames` frames, without animation.
  void moveTo(std::int64_t character, std::int64_t x, std::int64_t z, std::int64_t frames);
  // entity_move_to_entity: slides to where `target` stands when the move starts.
  void moveToCharacter(std::int64_t character, std::int64_t target, std::int64_t frames);
  // entity_move_to_with_camera and entity_move_to_entity_with_camera: the camera follows.
  void moveToWithCamera(std::int64_t character, std::int64_t x, std::int64_t z,
                        std::int64_t frames);
  void moveToCharacterWithCamera(std::int64_t character, std::int64_t target, std::int64_t frames);
  // entity_move_to_axis: slides along one axis (0 x, 1 y, 2 z) to `position` in `frames` frames.
  void moveToAxis(std::int64_t character, std::int64_t position, std::int64_t axis,
                  std::int64_t frames);
  // entity_move_to_axis_with_camera: the camera follows.
  void moveToAxisWithCamera(std::int64_t character, std::int64_t position, std::int64_t axis,
                            std::int64_t frames);
  // reset_entity_origin: at once, no slot.
  void resetOrigin(std::int64_t character);
  // rotate_3d_object: turns or slides a 3D object of the map in the first object slot
  // (SCRIPT_MOVEMENT[11]); `direction` is the original's signed byte.
  void turnMapObject(std::int64_t object, std::int64_t direction);
  // move_object_to: moves the map's sprite object to (x, y) in background pixels in `steps`
  // frames, in the object slot `slot` (SCRIPT_MOVEMENT[12 + slot]).
  void moveMapSprite(std::int64_t slot, std::int64_t object, std::int64_t steps, std::int64_t x,
                     std::int64_t y);
  // move_camera_to and move_camera_to_entity: pans the camera in `frames` frames.
  void cameraTo(std::int64_t frames, std::int64_t x, std::int64_t z);
  void cameraToCharacter(std::int64_t character, std::int64_t frames);
  // play_animation: at once, no slot.
  void playAnimation(std::int64_t character, std::int64_t animation);
  // wait_for_entity: what the section waits on, by the original's wait id.
  [[nodiscard]] std::function<bool()> waitFor(std::int64_t what);

  // The partner's sync for the scripts (partner.c: getScriptSyncBit): the partner's routines
  // clear and raise it. Until its owner connects, a sync a wait cleared counts as raised.
  void connectPartnerSync() { syncConnected_ = true; }
  void setPartnerSync(bool raised) { partnerSync_ = raised; }

  void scriptBegins() override;
  void scriptFrame() override;
  void talkFrozen(std::int64_t talker) override;

private:
  struct LookAt {
    std::int64_t target = 0;
    std::optional<glm::vec3> point;
  };
  struct SetRotation {
    float yaw = 0.0F;
  };
  struct Walk {
    std::optional<std::int64_t> target;
    glm::vec3 point{};
    std::int64_t clip = 0;
    bool withCamera = false;
  };
  struct MoveTo {
    std::optional<std::int64_t> target;
    glm::vec3 point{};
    std::int64_t frames = 1;
    std::optional<glm::vec3> perFrame;
    bool withCamera = false;
    // The frames it has moved; one that runs long past its count ends on the point.
    std::int64_t moved = 0;
  };
  struct MoveToAxis {
    std::int64_t axis = 0;
    float position = 0.0F;
    std::int64_t frames = 1;
    std::optional<float> perFrame;
    bool withCamera = false;
  };
  struct MapObjectTurn {
    std::int64_t direction = 0;
  };
  struct SpriteMove {
    glm::ivec2 target{};
    std::int64_t steps = 0;
    std::optional<glm::ivec2> perFrame;
  };
  struct CameraPan {
    std::optional<std::int64_t> target;
    glm::vec3 point{};
    std::int64_t frames = 1;
    bool started = false;
  };
  using Move = std::variant<LookAt, SetRotation, Walk, MoveTo, MoveToAxis, CameraPan, MapObjectTurn,
                            SpriteMove>;
  struct Slot {
    // The character, or the map object of a MapObjectTurn or a SpriteMove.
    std::int64_t character = 0;
    Move move;
    // Where the mover stood on the last frame the camera followed it (PREVIOUS_CAMERA_POS).
    std::optional<glm::vec3> cameraFrom;
  };

  // The slot of a character on the map, logged when it is not there.
  [[nodiscard]] std::optional<std::int64_t> slotFor(std::string_view order,
                                                    std::int64_t character) const;
  void fill(std::int64_t slot, std::int64_t character, Move move);
  void walk(std::string_view order, std::int64_t character, std::optional<std::int64_t> target,
            glm::vec3 point, std::int64_t animation, bool withCamera);
  void slide(std::string_view order, std::int64_t character, MoveTo move);
  void slideAlong(std::string_view order, std::int64_t character, std::int64_t position,
                  std::int64_t axis, std::int64_t frames, bool withCamera);
  // Moves the camera by the mover's step since the frame before (tamer.c: moveCameraByDiff).
  void followWithCamera(Slot& slot);
  // Ticks one slot; true when its move is done.
  bool tick(Slot& slot);
  bool tick(std::int64_t character, LookAt& move);
  bool tick(std::int64_t character, const Walk& move);
  bool tick(std::int64_t character, MoveTo& move);
  bool tick(std::int64_t character, MoveToAxis& move);
  bool tick(CameraPan& move);
  bool tick(std::int64_t object, SpriteMove& move);
  [[nodiscard]] static float frames(std::int64_t count);
  [[nodiscard]] bool partnerSynced() const;

  ScriptActorsPort* actors_;
  MotionRules rules_;
  std::map<std::int64_t, Slot> slots_;
  bool partnerSync_ = true;
  bool syncConnected_ = false;
};

} // namespace dwe::game::story
