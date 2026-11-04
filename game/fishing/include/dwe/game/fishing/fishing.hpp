#pragma once

#include <dwe/core/math.hpp>
#include <dwe/game/fishing/rules.hpp>
#include <dwe/game/fishing/view.hpp>
#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The fishing of a map (the FISH overlay, fish.c): the fish that swim there, the rod, the bobber,
// the hooked fish and the line's tension. One tick is one frame of the original (20 a second). The
// water and the fish spots are in the original's screen space, so a FishingView maps them onto the
// map's ground (FISH_projectPoint, mapToWorldPosition). Positions are in world space on the water's
// plane, y = 0; the bobber and the bait fly above it. The fishing holds the camera while the rod is
// out (FISH_enableCameraControl) and says where the view centres (cameraCentre); the player's
// model, the messages and the items are the caller's: the fishing asks for them through its events.
namespace dwe::game::fishing {

// The screen of the map the fishing happens on: the original's screen pixels from the
// background's optical centre, y down.
class FishingView {
public:
  FishingView() = default;
  FishingView(const FishingView&) = default;
  FishingView(FishingView&&) = default;
  FishingView& operator=(const FishingView&) = default;
  FishingView& operator=(FishingView&&) = default;
  virtual ~FishingView() = default;

  // Where a world point shows (FISH_projectPoint); nothing behind the camera or past its far end.
  [[nodiscard]] virtual std::optional<glm::vec2> toScreen(glm::vec3 world) const = 0;
  // The point of the water's plane under a screen point (mapToWorldPosition).
  [[nodiscard]] virtual std::optional<glm::vec3> toGround(glm::vec2 screen) const = 0;
  // The screen points the background covers, corner to corner (FISH_isScreenPointVisible).
  [[nodiscard]] virtual glm::vec2 screenMin() const = 0;
  [[nodiscard]] virtual glm::vec2 screenMax() const = 0;
  // The centres the view may take, so that it stays on the background, and where it centres now
  // (FISH_viewX(0xa0), FISH_viewY(0x78)), in the same screen pixels.
  [[nodiscard]] virtual ViewRange viewRange() const = 0;
  [[nodiscard]] virtual glm::ivec2 viewCentre() const = 0;
};

// getBestFishingRod: the old rod, or the amazing rod.
enum class Rod : std::uint8_t { Old, Amazing };

// What the player does this frame, and where the player's model puts the rod.
struct FishingInput {
  bool confirmPressed = false;
  bool confirmHeld = false;
  bool confirmReleased = false;
  bool cancelPressed = false;
  bool cameraPressed = false;
  bool cameraHeld = false;
  // The d-pad: left and right turn the player, and with the camera button held all four move
  // the view (FISH_panCameraWithDPad).
  bool turnLeft = false;
  bool turnRight = false;
  bool lookUp = false;
  bool lookDown = false;
  // Whether the player's current fishing animation still plays (animFlag & 1).
  bool animationPlaying = false;
  // The rod's tip (FISH_updateRodPose), where the line starts.
  glm::vec3 lineStart{0.0F};
  // Where the bobber hangs from the player's hand (FISH_getRodTipPos), and where the bait is held
  // (FISH_moveBaitToRodTip).
  glm::vec3 bobberRest{0.0F};
  glm::vec3 baitHold{0.0F};
  glm::vec3 player{0.0F};
  // The player's facing, on the ground.
  glm::vec3 playerFacing{0.0F, 0.0F, -1.0F};
  std::int32_t hour = 0;
  // Whether the player still has any bait (hasFoodItem).
  bool hasBait = false;
};

enum class FishingMessage : std::uint8_t {
  BaitTaken,
  TooLate,
  TooSoon,
  Hooked,
  LineCut,
  HookLost,
  Snagged,
};

enum class FishingEventKind : std::uint8_t {
  // value: the sound of the fishing's slot.
  Sound,
  // value: the player's animation.
  Animation,
  // value: a FishingMessage.
  Message,
  // The bait menu opens; answer with chooseBait.
  OpenBaitMenu,
  // value: the bait the fish ate; it leaves the bag.
  BaitEaten,
  // A fish was landed. value: its item; kind and size.
  FishCaught,
  // Something that got stuck on the hook. value: its item.
  JunkCaught,
  // The sea serpent reached the shore at position: its scene starts.
  SeaSerpentLanded,
  // The fishing ended; the player walks again.
  Ended,
  // A ripple for the eye: value is its kind (FISH_spawnRipple), size its scale.
  Ripple,
  // value: a command of the bobber's animation (FISH_bobberCommand).
  Bobber,
  // A mark over a fish that rolls a bite: value 1 when it may bite.
  Marker,
};

// A Message's position is the point the view turns to while it is read (FISH_showTextBox's
// target): the bobber. A catch's is the catch's.

struct FishingEvent {
  FishingEventKind kind = FishingEventKind::Sound;
  std::int32_t value = 0;
  std::int32_t fishKind = 0;
  std::int32_t size = 0;
  glm::vec3 position{0.0F};
};

struct FishingOutput {
  std::vector<FishingEvent> events;
  // The player's turn this frame, in the original's angle units.
  std::int32_t playerTurn = 0;
  // The rotation the player takes, in the original's angle units.
  std::optional<std::int32_t> playerRotation;
};

enum class FishState : std::uint8_t {
  Appearing,
  Idle,
  Swimming,
  Leaving,
  // On the hook (FISH_tickHookedFish): nibbling, pulling the bobber down, struggling, running,
  // escaping and, for the sea serpent, swimming to the shore.
  Nibbling,
  Pulling,
  Struggling,
  Running,
  Escaping,
  Landing,
};

struct Fish {
  // 0 to 5, or SeaSerpentKind.
  std::int32_t kind = 0;
  std::int32_t size = 0;
  glm::vec3 position{0.0F};
  // Radians about +Y; the fish swims along (sin, 0, cos).
  float yaw = 0.0F;
  // 0 (hidden) to OpaqueFish.
  std::int32_t opacity = 0;
  FishState state = FishState::Appearing;
  std::int32_t framesOnState = 0;
  std::int32_t lifetime = 0;
  std::int32_t timeNextMove = 0;
  std::int32_t speed = 0;
  std::int32_t animationPhase = 0;
  // The sea serpent's next point of its path.
  std::size_t pathStep = 0;
  // The sea serpent's turns over its last frames, newest first: its body's pieces
  // (FISH_pushSeadramonSegment, seaSerpentBody).
  std::array<float, SeaSerpentPieces> turns{};
};

inline constexpr std::int32_t SeaSerpentKind = 6;
inline constexpr std::int32_t OpaqueFish = 128;

// The rod's states (FISH_tickRod's fishingState).
enum class RodState : std::uint8_t {
  Off,
  ToPlayer,
  ChoosingBait,
  Readying,
  ToBobber,
  Aiming,
  WindingUp,
  Flying,
  Waiting,
  Hooked,
  Reeling,
  Landing,
  Ending,
  LineCut,
  HookLost,
  Snagged,
  SeaSerpent,
};

struct Tension {
  bool shown = false;
  std::int32_t level = 0;
  std::int32_t limit = 0;
  std::int32_t framesAtLimit = 0;
  // The line's colour (tensionColor), white until the fight pulls it.
  glm::u8vec3 color{0xFF};
};

class Fishing {
public:
  Fishing(FishingRules rules, FishingTables tables, FishingPlace place);

  // Fishing stops on the map until a scene allows it again (setFishingDisabled).
  void setDisabled(bool disabled) { disabled_ = disabled; }
  [[nodiscard]] bool disabled() const { return disabled_; }

  // FISH_loadFishing: the player takes the rod out.
  void start(Rod rod);
  [[nodiscard]] bool fishing() const { return state_ != RodState::Off; }

  // The answer to OpenBaitMenu: the bait chosen, or nothing.
  void chooseBait(std::optional<std::int32_t> bait);
  [[nodiscard]] bool choosingBait() const { return baitMenuOpen_; }

  // One frame: the rod (FISH_tamerTick), then the water (FISH_tickFishingWorld). Call it only while
  // no message of the fishing is open.
  FishingOutput tick(const FishingInput& input, const FishingView& view, ruleset::Random& random);
  // A frame while a message of the fishing is open: the view moves to the message's point
  // (FISH_scrollViewToTarget), 16 pixels a frame.
  void scrollToNotice(glm::vec3 target, const FishingView& view);

  [[nodiscard]] std::span<const Fish> fish() const { return pool_; }
  [[nodiscard]] const std::optional<Fish>& hooked() const { return hooked_; }
  [[nodiscard]] RodState state() const { return state_; }
  // Where the view centres while the fishing holds the camera, in the view's screen pixels;
  // nothing while the field's camera follows the player.
  [[nodiscard]] const std::optional<glm::ivec2>& cameraCentre() const { return camera_; }
  [[nodiscard]] const Tension& tension() const { return tension_; }
  [[nodiscard]] glm::vec3 bobber() const { return bobber_.position; }
  [[nodiscard]] bool bobberShown() const { return bobberShown_; }
  [[nodiscard]] bool lineShown() const { return lineShown_; }
  // Where the line starts: the rod's tip of the last frame (rod->lineStart).
  [[nodiscard]] glm::vec3 lineStart() const { return lineStart_; }
  // The bait or the catch on its way to the hand; nothing while hidden.
  [[nodiscard]] std::optional<glm::vec3> bait() const;
  [[nodiscard]] std::int32_t baitItem() const { return bait_; }

private:
  struct Bobber {
    glm::vec3 position{0.0F};
    glm::vec3 velocity{0.0F};
    // 0 rests, 1 hangs from the hand, 2 to 4 flies.
    std::int32_t mode = 0;
    float gravity = 0.0F;
    // The line's length while it swings from the rod; 0 when free.
    float lineLength = 0.0F;
  };

  struct Scare {
    glm::vec3 position{0.0F};
    float radius = 0.0F;
    std::int32_t framesLeft = 0;
  };

  // The water (pond.cpp).
  void tickWater();
  void tickPopulation();
  void tickFish(Fish& fish);
  void tickSeaSerpent(Fish& fish);
  void spawnFish(std::int32_t kind);
  void spawnSeaSerpent();
  [[nodiscard]] std::optional<glm::vec3> randomPointInSpot(std::int32_t kind);
  [[nodiscard]] std::optional<std::size_t> findFishToBite();
  void hookFish(std::size_t index);
  void tickHooked();
  void hookedEscapes();
  void hookedGrabbed();
  void steerHookedFromShore();
  bool tryStrike();
  void spawnRipple(std::int32_t kind, glm::vec3 position, float scareRadius, std::int32_t spread,
                   std::int32_t scale);
  [[nodiscard]] std::int32_t roll(std::int32_t min, std::int32_t max);

  // The rod (rod.cpp).
  void tickRod();
  void toState(RodState state);
  void giveUp();
  void cast();
  void release();
  void splash();
  void waiting();
  void checkBobber();
  void reeling();
  void landed();
  void showCatch();
  void finish();
  void endFishing();
  void reelBobber(float scale, float minus, float share);
  [[nodiscard]] std::int32_t updateTension();
  void tickBobber();
  void tickBait(glm::vec3 previous);
  bool moveBaitToHand(float maxStep);
  void bobberCommand(std::int32_t command);
  // The camera (FISH_panCameraTo, FISH_panCameraWithDPad): true while the view moves.
  bool panTo(glm::vec3 world, std::int32_t radius, glm::ivec2 deadZone);
  bool panToBobber();
  bool panToPlayer();
  void panWithDPad();

  // Screen tests on the water's plane.
  [[nodiscard]] std::optional<glm::vec2> visibleScreen(glm::vec3 world) const;
  [[nodiscard]] bool onWater(glm::vec3 world) const;
  [[nodiscard]] bool isWaterOnScreen(glm::vec2 screen) const;
  [[nodiscard]] bool withinCastRange(glm::vec3 position, std::int32_t extra) const;

  void emit(FishingEventKind kind, std::int32_t value = 0);
  void emitAt(FishingEventKind kind, std::int32_t value, glm::vec3 position);
  void sound(std::int32_t sound);
  void animate(std::int32_t animation);
  void message(FishingMessage message);

  // The frame being ticked: set by tick() for its length.
  const FishingInput* input_ = nullptr;
  const FishingView* view_ = nullptr;
  ruleset::Random* random_ = nullptr;
  FishingOutput out_;

  FishingRules rules_;
  FishingTables tables_;
  FishingPlace place_;
  bool disabled_ = false;
  std::uint32_t frame_ = 0;

  std::vector<Fish> pool_;
  std::int32_t scaredPause_ = 0;
  std::optional<Scare> scare_;
  std::optional<Fish> hooked_;
  bool hookedEscaped_ = false;
  std::int32_t nibbleTime_ = 0;
  std::int32_t pullTime_ = 0;
  std::int32_t power_ = 0;
  std::int32_t powerMax_ = 0;

  RodState state_ = RodState::Off;
  std::int32_t framesOnState_ = 0;
  Rod rod_ = Rod::Old;
  std::optional<glm::ivec2> camera_;
  bool baitMenuOpen_ = false;
  std::optional<std::optional<std::int32_t>> baitChosen_;
  std::int32_t bait_ = 0;
  bool baitEaten_ = false;
  std::int32_t pressedFrames_ = 0;
  std::int32_t caughtKind_ = 0;
  std::int32_t caughtSize_ = 0;
  Tension tension_;
  Bobber bobber_;
  bool bobberShown_ = false;
  bool lineShown_ = false;
  glm::vec3 lineStart_{0.0F};
  glm::vec3 previousLineStart_{0.0F};
  bool baitShown_ = false;
  glm::vec3 baitPosition_{0.0F};
  std::optional<glm::vec3> seaSerpentFrom_;
};

} // namespace dwe::game::fishing
