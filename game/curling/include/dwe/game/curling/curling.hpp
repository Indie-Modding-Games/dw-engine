#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// The stones minigame of the frozen lake (curling; kar/kar.c, the KAR overlay): two throwers send
// five stones each down an ice rink at a target, past three obstacle stones; the stones slide,
// slow down, bounce off the walls and knock each other; the rings they rest in score. It draws
// nothing: the app shows the rink, the stones, the prompts and the hint pages from the state and
// the events. Positions are the original's map units (x across, z down the rink; the target is at
// negative z), angles 4096 a turn.
namespace dwe::game::curling {

inline constexpr std::string_view CurlingSectionName = "curling";

// The stones of a team (KarStoneRow) and the teams: the player, the opponent and the obstacles.
inline constexpr std::size_t StonesPerTeam = 5;
inline constexpr std::size_t Teams = 3;
inline constexpr std::size_t PlayerTeam = 0;
inline constexpr std::size_t OpponentTeam = 1;
inline constexpr std::size_t ObstacleTeam = 2;

// A ring of the target (KarZones, KarTallyValues): its centre, its radius and its points.
struct Ring {
  std::int32_t x = 0;
  std::int32_t z = 0;
  std::int32_t radius = 0;
  std::int32_t points = 0;
  // The map objects that light the ring while a stone slides over it or scores in it
  // (KAR_D_8005B450: KAR_updateRingMarkers shows them, else hides them).
  std::vector<std::int32_t> objects;
};

struct CurlingRules {
  // The story values: the opponent (pstat 0x7A: 0 the first, 1 the second) and the result
  // (pstat 0x79: KAR_finishMatch).
  std::int32_t opponentVariable = 0;
  std::int32_t resultVariable = 0;
  // KAR_setupMatch: a stone's type from rand() % 10 into this table, and the weight by type.
  std::vector<std::int32_t> stoneTypes;
  std::array<std::int32_t, 4> weights{};
  // KAR_tickStones: the speed kept each frame (98 / 100) and the least move that keeps a stone
  // going.
  std::int32_t frictionKeep = 0;
  std::int32_t frictionOf = 1;
  std::int32_t minMove = 0;
  // KAR_tickMatchState: the power gauge's step and limit, and what the throw adds to it.
  std::int32_t powerStep = 0;
  std::int32_t powerLimit = 0;
  std::int32_t throwBonus = 0;
  // KAR_computeThrowPower: what each opponent takes off its computed power.
  std::array<std::int32_t, 2> opponentPowerOffsets{};
  // KAR_classifyStoneRings, KAR_tickScoreTally: the rings in their order of precedence.
  std::vector<Ring> rings;
  // KAR_finishMatch: the score of a perfect win.
  std::int32_t perfectScore = 0;
};

core::Result<CurlingRules> parseCurlingRules(const ruleset::Section& section);

struct Point {
  std::int32_t x = 0;
  std::int32_t z = 0;
};

// A stone (KarStone). `state`: -1 waiting in its team's rack, -101 out (thrown off or never
// placed), 0 in the thrower's hand, 1 sliding or resting, 2 pegged to the ice (a type-3 stone that
// stopped), 3 a pegged stone shaking after a hit; 100 more while it blinks in the score tally.
struct Stone {
  std::int32_t state = 0;
  std::int32_t type = 0;
  std::int32_t weight = 0;
  std::int32_t speed = 0;
  std::int32_t angle = 0;
  // The ring it rests in (1 to 4, 0 none; + 0x10 for the opponent's) and before the last throw.
  std::int32_t ring = 0;
  std::int32_t previousRing = 0;
  Point position;
  // Where it was on the last frame (the shaking centre of a pegged stone).
  Point target;
  // The rack's height of a waiting stone (pos.vy).
  std::int32_t height = 0;
  // Its spin about the vertical (rotation.vy): the selected stone turns in the rack.
  std::int32_t spin = 0;

  [[nodiscard]] bool pegged() const { return state == 2 || state == 3; }
  // On the ice: thrown and not out (blinking or not).
  [[nodiscard]] bool inPlay() const { return state > 0; }
  // Hidden by the tally's blink, or out.
  [[nodiscard]] bool hidden() const { return state >= 100 || state < -100; }
};

struct Team {
  std::int32_t score = 0;
  std::int32_t thrown = 0;
  // The opponent's planned power and stone (KarStoneRow unk2, unk4).
  std::int32_t plannedPower = 0;
  std::int32_t plannedStone = 0;
  std::array<Stone, StonesPerTeam> stones{};
};

// KAR_tickMatchState's states by number.
enum class Phase : std::int32_t {
  Placing = 0x00,      // the throwers to their marks
  Prompt = 0x01,       // play or hear the rules
  Demo = 0x02,         // the rules: the racks fill
  Ready = 0x03,        // "Ready" blinks and the racks fill
  DemoThrow = 0x04,    // the rules: the demonstration throw is planned
  Select = 0x05,       // pick a stone from the rack
  Aim = 0x06,          // turn the throw's direction
  Camera = 0x07,       // the camera goes to the stone
  Power = 0x08,        // the power gauge
  Release = 0x09,      // the stone leaves the hand
  Throwing = 0x0A,     // the throw's first frames
  Rolling = 0x0B,      // the stones slide
  Rings = 0x0C,        // the rings are read
  Tally = 0x0D,        // the score is counted stone by stone
  DemoCamera = 0x0E,   // the rules: the camera to the target
  NextThrower = 0x0F,  // who throws next
  Turn = 0x10,         // the camera to the thrower; the end when all are thrown
  OpponentPlan = 0x11, // the opponent picks its shot
  WaitBox = 0x12,      // unused
  Comment = 0x13,      // the opponent's comment, or the end's
  Finished = 0x14,     // the match is over
};

// The pad as the match reads it on a frame: held and newly pressed (POLLED_INPUT and its edge).
struct CurlingButtons {
  bool up = false;
  bool down = false;
  bool left = false;
  bool right = false;
  bool upPressed = false;
  bool downPressed = false;
  bool leftPressed = false;
  bool rightPressed = false;
  bool confirmPressed = false;
};

// The opponent (pstat 0x7A); its entity is that number + 2.
enum class Opponent : std::uint8_t { Penguinmon, MetalMamemon };

struct CurlingEvent {
  enum class Kind : std::uint8_t {
    // a: the bank (0 menu, 8 the minigame's), b: the sound (playSound, playSound2).
    Sound,
    // The rolling sound at the release (playSound2 kept in MAIN_D_80135228): a the bank, b the
    // sound, played so that SoundStop can stop it.
    RollingSound,
    // The rolling sound stops (KAR_tickMatchState state 0xC: stopSoundMask).
    SoundStop,
    // The play / rules prompt opened (a: the option shown, 0 play, 1 rules).
    Prompt,
    // A hint page: a the page group (KAR_tickHintBox's n), b the page from 1; b 0 closes it.
    Hint,
    // A stone scored in the tally: a the team, b the ring (1 to 4).
    Tally,
    // An animation: a who (0 the player, 1 the opponent, 2 the partner), b the animation.
    Animation,
    // The camera goes to a: x, b: z (tickCameraMoveTo; done at once here).
    Camera,
    // The player scrolls the view while aiming, by a, b background pixels (KAR_handleAimScroll,
    // moveCameraByOffset).
    Scroll,
    // While a thrown stone slides the camera follows it: it moves by the stone's step since the
    // frame before, a: x, b: z (KAR_tickMatchState state 0xB, moveCameraByDiff).
    Follow,
    // The match is over: a the result (KAR_finishMatch).
    Finished,
  };
  Kind kind = Kind::Sound;
  std::int32_t a = 0;
  std::int32_t b = 0;
};

// The hint pages of each group (KAR_D_8005B04C, KAR_D_8005B40C: 4 lines a page). Groups: 0 select,
// 1 aim, 2 power, 3 after the demonstration, 4 to 6 the opponent's comment (player behind, ahead,
// level), 7 lost, 8 won.
inline constexpr std::array<std::int32_t, 9> HintPages{3, 2, 1, 4, 1, 1, 1, 1, 1};

// KARRING.TMD (the stones' model, read by KAR_start): its object for a stone of `team`.
// KAR_setupMatch links type * 3 + the team's model (KAR_D_8005AB80: 2 the player's, 1 the
// opponent's, 0 the obstacles'); KAR_beginAiming links a spiked stone (type 3) taken in hand to its
// team's type-0 look, and KAR_tickStones links the spiked one back (MAIN_D_80134A4C) when it pegs
// itself to the ice.
inline constexpr std::size_t StoneModelObjects = 12;

// KAR_renderAimArrow: the dotted line ahead of the stone in a hand while its thrower aims, 200
// apart along the thrower's facing and mirrored off the side walls; the last one is the head.
inline constexpr std::size_t AimDots = 15;
struct AimArrow {
  std::array<Point, AimDots> dots{};
  // Each one's grey (setRGB0) over 0x80 for the texture as is: 0x40 to 0xFE, a wave down the line.
  std::array<std::int32_t, AimDots> shades{};
  // The one dot left out this frame, moving down the line.
  std::size_t hidden = 0;
  // The head's turn on the screen (KAR_rotatePoint's angle), from the dot before it.
  std::int32_t headTurn = 0;
};
[[nodiscard]] std::size_t stoneModelObject(std::size_t team, const Stone& stone);

// The throwers' marks (KAR_tickMatchState state 0).
inline constexpr Point PlayerMark{.x = -0x190, .z = 0x578};
inline constexpr Point OpponentMark{.x = 0x190, .z = 0x578};

class Match {
public:
  // KAR_start: the match from the start (KAR_setupMatch(0)) against `opponent`.
  Match(const CurlingRules& rules, Opponent opponent, ruleset::Random& random);

  // One frame of the original (20 per second): KAR_tick's logic, then the stones' object tick
  // (KAR_tickStones), then where KAR_renderScene puts the stone in a hand.
  std::span<const CurlingEvent> tick(const CurlingButtons& buttons, ruleset::Random& random);

  // Whether KAR_renderScene draws stone `index` of `team` this frame: the opponent's rack shows
  // from the select phase on and only without a hint box (always while "Ready" blinks and from the
  // turn's end on), the racks fill one stone each 60 of the intro counter before the demonstration
  // throw, and a stone out (below -100) or blinking off in the tally (above 100) is not drawn.
  [[nodiscard]] bool stoneDrawn(std::size_t team, std::size_t index) const;

  // Where the stone in a hand rides (KAR_renderScene: the player's bone 9, the second opponent's
  // bone 4, the first opponent's position less 80 in x). Without a model the marks stand in.
  void setHands(Point player, Point opponent);

  [[nodiscard]] Phase phase() const { return static_cast<Phase>(state_); }
  [[nodiscard]] std::int32_t state() const { return state_; }
  [[nodiscard]] const std::array<Team, Teams>& teams() const { return teams_; }
  [[nodiscard]] std::size_t selectedStone() const { return static_cast<std::size_t>(selected_); }
  [[nodiscard]] std::int32_t throwingTeam() const { return team_; }
  [[nodiscard]] std::int32_t aimAngle() const { return aim_; }
  [[nodiscard]] std::int32_t power() const { return power_; }
  [[nodiscard]] Opponent opponent() const { return opponent_; }
  // The facing (rotation.vy) the match gave a character (0 the player, 1 the opponent, 2 the
  // partner): both throwers 0 at the start and when they aim, turned against the aim while aiming
  // (KAR_turnAimLeft / KAR_turnAimRight), all three 0x800 at the end (KAR_finishMatch); nothing
  // until the match turns it.
  [[nodiscard]] std::optional<std::int32_t> facing(std::size_t who) const;
  // The aim's dotted line this frame, while the thrower aims (states 6 and 7).
  [[nodiscard]] const std::optional<AimArrow>& aimArrow() const { return arrow_; }
  // The play / rules prompt's highlighted option (0 play, 1 rules), while it is open.
  [[nodiscard]] std::optional<std::int32_t> promptOption() const;
  // The hint page shown: its group and page (from 1), while a box is open.
  [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> hint() const;
  // The intro counter (MAIN_D_80135252 in states 2 and 3): stone i of a rack shows from i*60+60.
  [[nodiscard]] std::int32_t counter() const { return counter_; }
  // Whether "Ready" shows this frame (KAR_renderReadyPrompt).
  [[nodiscard]] bool readyShown() const { return state_ == 3 && (blink_ & 8) != 0; }
  // Bit i set: ring i's map objects show this frame (KAR_updateRingMarkers).
  [[nodiscard]] std::uint32_t litRings() const { return litRings_; }
  [[nodiscard]] bool finished() const {
    return state_ == static_cast<std::int32_t>(Phase::Finished);
  }
  // KAR_finishMatch: 2 a perfect win, 1 a win, 0 a loss.
  [[nodiscard]] std::int32_t result() const { return result_; }

  // Sets a scene, for tests and debug tools: these stones, the match in `phase` and `team` throwing
  // its stone `stone`. The stones out of their racks count as thrown before it, the player's first.
  void setScene(const std::array<Team, Teams>& teams, Phase phase, std::int32_t team,
                std::size_t stone);

private:
  struct ThrownStone {
    std::size_t team = 0;
    std::size_t index = 0;
  };

  [[nodiscard]] Stone& stoneAt(ThrownStone at) { return teams_.at(at.team).stones.at(at.index); }
  [[nodiscard]] Stone& current() {
    return teams_.at(static_cast<std::size_t>(team_))
        .stones.at(static_cast<std::size_t>(selected_));
  }

  void emit(CurlingEvent::Kind kind, std::int32_t a = 0, std::int32_t b = 0);
  void sound(std::int32_t bank, std::int32_t id) { emit(CurlingEvent::Kind::Sound, bank, id); }
  void camera(Point at) { emit(CurlingEvent::Kind::Camera, at.x, at.z); }
  [[nodiscard]] std::int32_t throwerWho() const { return team_ != 0 ? 1 : 0; }
  [[nodiscard]] Team& row(std::size_t team) { return teams_.at(team); }
  void animate(std::int32_t who, std::int32_t animation) {
    emit(CurlingEvent::Kind::Animation, who, animation);
  }
  void face(std::int32_t who, std::int32_t rotation) {
    facing_.at(static_cast<std::size_t>(who)) = rotation;
  }

  // kar.c functions.
  void setupMatch(bool demonstration, ruleset::Random& random);
  void tickMatchState(const CurlingButtons& buttons, ruleset::Random& random);
  void tickStones();
  void placeHeldStone();
  void renderAimArrow();
  [[nodiscard]] bool shownThisFrame(std::size_t team, std::size_t index) const;
  void handleAimScroll(const CurlingButtons& buttons);
  void updateRingMarkers();
  void checkStonesStopped();
  void updateCollisions();
  void bounceOffWall();
  void resolveStoneCollision(Stone& a, Point atA, Stone& b, Point atB);
  void collidePeggedStone(Stone& pegged, Point a, Stone& mover, Point b);
  void collideRestingStone(Stone& resting, Point a, Stone& mover, Point b);
  void collideMovingStones(Stone& stoneA, Point a, Stone& stoneB, Point b);
  void classifyStoneRings();
  void registerThrownStone();
  bool tickScoreTally(const CurlingButtons& buttons);
  std::int8_t tickYesNoPrompt(const CurlingButtons& buttons);
  bool tickHintBox(std::int32_t group, const CurlingButtons& buttons);
  void beginAiming();
  void selectPreviousStone();
  void selectNextStone();
  void beginThrow();
  void turnAim(std::int32_t delta);
  void finishMatch();
  void chooseOpponentShot(ruleset::Random& random);
  std::int32_t findClearShotAngle(std::int32_t x, std::int32_t z) const;
  std::int32_t aimAtStoneInRing(std::size_t team, std::int32_t ring, Point& out) const;
  std::int32_t aimAtRandomStone(ruleset::Random& random) const;
  std::int32_t aimBankShot(const Stone& stone, std::int32_t x, std::int32_t z) const;
  void setOpponentShot(std::size_t plan, std::int32_t angle, std::int32_t x, std::int32_t z);
  [[nodiscard]] std::int32_t findUnusedStoneOfType(std::int32_t type) const;
  [[nodiscard]] std::int32_t computeThrowPower(std::int32_t weight, std::int32_t x,
                                               std::int32_t z) const;

  const CurlingRules* rules_;
  Opponent opponent_;
  std::array<Team, Teams> teams_{};
  std::vector<CurlingEvent> events_;
  std::array<std::optional<std::int32_t>, 3> facing_{};
  std::int32_t arrowGap_ = 0; // MAIN_D_80135238: the dot left out, three frames each
  std::optional<AimArrow> arrow_;
  Point playerHand_ = PlayerMark;
  Point opponentHand_;

  std::int32_t state_ = 0;    // MAIN_D_80135244
  std::int32_t selected_ = 0; // MAIN_D_8013523A
  std::int32_t team_ = 0;     // MAIN_D_8013523C
  std::int32_t power_ = 0;    // MAIN_D_8013523E
  std::int32_t aim_ = 0;      // MAIN_D_8013524C
  std::int32_t counter_ = 0;  // MAIN_D_80135252
  std::int32_t turnStep_ = 0; // MAIN_D_80135254
  std::int32_t box_ = 0;      // MAIN_D_80135250: the prompt's or the hint box's page
  // UI_BOX_DATA[0].state: the box's open and close animations take a frame here.
  enum class Box : std::uint8_t { Closed, Opening, Open, Closing };
  Box boxState_ = Box::Closed;
  std::int32_t hintGroup_ = -1;
  std::int32_t blink_ = 0; // MAIN_D_80135220
  // The score tally (MAIN_D_80135256, MAIN_D_80134A4F, MAIN_D_8013522D, MAIN_D_8013522E,
  // MAIN_D_80135230, MAIN_D_80135234).
  std::int32_t tallyStone_ = 0;
  std::int32_t tallyShown_ = -1;
  bool tallySkip_ = false;
  std::int32_t tallyStep_ = 0;
  std::int32_t tallyBlinks_ = 0;
  std::int32_t tallyFrames_ = 0;
  std::array<ThrownStone, (StonesPerTeam * 2) + 1> thrownOrder_{}; // KAR_D_800639C0
  // The player's last thrown stone as the second opponent reads it (KAR_D_80063914).
  Stone lastPlayerStone_;
  // Where the thrown stone was on the frame before, for the camera that follows it
  // (KAR_D_800638F4).
  Point followFrom_;
  std::int32_t result_ = 0;
  std::uint32_t litRings_ = 0;
};

} // namespace dwe::game::curling
