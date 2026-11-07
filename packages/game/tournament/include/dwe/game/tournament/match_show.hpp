#pragma once

#include <dwe/assets/clip_motion.hpp>
#include <dwe/game/tournament/tournament.hpp>
#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// What a cup's match shows around the fight (std/std_main.c, std_effect.c, std_hud.c): the intro
// that runs each fighter in under a chasing camera with its stats (STD_runIntro), the arena's
// cameras while it fights (STD_tickVSPhase, STD_tickBattle), the camera that circles the partner as
// the match ends (STD_startCameraIntro), the time-up notice with the damage each side dealt
// (STD_tickBattleResultScreen, STD_addTimeoutWindow) and the 3D letters of a won, lost or drawn
// match (STD_addWinScene, STD_addLoseScene, STD_addVersusModelScene). Pure state, one tick per
// frame of the original (20 a second); the app draws it. Points are the original's axes and units
// (y down), angles 4096 a turn.
namespace dwe::game::tournament {

using ShowPoint = std::array<std::int32_t, 3>;

// GS_VIEWPOINT (the eye vp and the point it looks at vr) and the projection (VIEWPORT_DISTANCE).
struct ShowView {
  ShowPoint eye{};
  ShowPoint target{};
  std::int32_t projection = 0;

  friend bool operator==(const ShowView&, const ShowView&) = default;
};

// A fighter's size (its species' height and radius).
struct ShowBody {
  std::int32_t height = 0;
  std::int32_t radius = 0;
};

// Where a fighter's model nodes are on this frame (calculateBoneMatrix): its node 0 and the height
// of its node 1, which the cameras hang on; the app reads them from the drawn pose. Without them
// the cameras use the fighter's feet and half its height.
struct ShowBones {
  ShowPoint root{};
  std::int32_t node1Y = 0;
};

// The intro's clips (startAnimation): the stance it waits in and ends on, and the run.
inline constexpr std::uint32_t IntroStanceClip = 0x21;
inline constexpr std::uint32_t IntroRunClip = 0x23;

// A fighter of the intro. `walkIn` is how far it runs toward the arena's centre (STD_runIntro:
// dist, negative for the rival, which runs the other way).
struct IntroFighter {
  ArenaPoint start;
  std::int32_t yaw = 0;
  std::int32_t walkIn = 0;
  ShowBody body;
  // The name's length in characters (strlen / 2 of the original's two-byte name; 10 for two
  // species, STD_tickIntroName).
  std::int32_t nameLength = 0;
  // HP, MP, offense, defense, speed and brains as the match starts.
  std::array<std::int32_t, 6> stats{};
  // The clip of its first technique (stats.base.moves): its pose at the end of the run.
  std::optional<std::uint32_t> poseClip;
  // Its clips' root motion; without the run's, it runs at RunFallbackStep a frame.
  assets::ClipMotions clips;
};

// STD_runIntro: first the partner, then the rival. Each waits in its stance while its name comes
// in (a letter every 4 frames) and the camera looks at it from its side; then it runs in (its run
// clip's root motion) while the camera chases it, swinging round to its front, its stats bars
// count up, and it strikes its first technique's pose where it stops. The confirm button cuts a
// fighter's turn short and puts it where its run ends.
class MatchIntro {
public:
  MatchIntro(IntroFighter partner, IntroFighter rival);

  // One frame; `confirm` was pressed on it; `bones` of the partner and the rival, when known.
  void tick(bool confirm, const std::array<std::optional<ShowBones>, 2>& bones);
  [[nodiscard]] bool done() const { return turn_ >= 2; }

  // The fighter whose turn it is (0 the partner, 1 the rival).
  [[nodiscard]] std::size_t turn() const { return turn_; }
  struct Figure {
    ArenaPoint position;
    std::int32_t yaw = 0;
    std::uint32_t clip = IntroStanceClip;
    // Bumped each time the clip starts again.
    std::uint32_t starts = 0;
  };
  [[nodiscard]] const Figure& figure(std::size_t fighter) const { return figures_.at(fighter); }
  [[nodiscard]] ShowView view() const { return view_; }
  // The name of the fighter whose turn it is: how many letters show, the last one's step of its
  // grow-in (0 to 3; STD_INTRO_NAME_CHAR_SIZES and _OFFSETS) and the row's y (it slides up once
  // whole). The US version draws no letters (STD_renderIntroNameChar adds no primitive).
  [[nodiscard]] std::int32_t nameShown() const { return nameShown_; }
  [[nodiscard]] std::int32_t nameStep() const { return nameStep_; }
  [[nodiscard]] std::int32_t nameY() const { return nameY_; }
  // The stats box (STD_addIntroStats): the values as they count up, nothing before it opens.
  [[nodiscard]] const std::optional<std::array<std::int32_t, 6>>& stats() const { return stats_; }
  // Whether the counted values are all in (then the numbers show, STD_renderIntroStats).
  [[nodiscard]] bool statsCounted() const;
  // The music track the arena plays from this frame on (STD_MUSIC 0 for the partner, 1 for the
  // rival), once, and whether the stats ticked this frame (playSound(0, 0x16)).
  [[nodiscard]] std::optional<std::int32_t> takeMusic() { return std::exchange(music_, {}); }
  [[nodiscard]] bool counting() const { return counting_; }

  // The run's step a frame without the run clip's root motion (a port fallback).
  static constexpr std::int32_t RunFallbackStep = 40;

private:
  struct Fighter {
    IntroFighter desc;
    ArenaPoint target;
  };
  void startTurn();
  void endTurn();
  void tickName();
  void tickStats();
  void tickChase(const std::optional<ShowBones>& bones);
  void stepClip(std::size_t fighter);
  void play(std::size_t fighter, std::uint32_t clip);
  [[nodiscard]] bool clipEnded(std::size_t fighter) const;
  [[nodiscard]] ShowBones bonesOf(std::size_t fighter, const std::optional<ShowBones>& bones) const;

  std::array<Fighter, 2> fighters_;
  std::array<Figure, 2> figures_{};
  std::array<std::size_t, 2> clipFrames_{};
  std::size_t turn_ = 0;
  // STD_INTRO_CAMERA_CHASE: -1 once the run is over, 0 while the name comes in, 20 on.
  std::int32_t chaseTimer_ = 0;
  std::int32_t chasePhase_ = 0;
  ArenaPoint chaseFrom_;
  std::int32_t nameFrames_ = 0;
  std::int32_t nameShown_ = 0;
  std::int32_t nameStep_ = 3;
  std::int32_t nameY_ = 0;
  std::optional<std::array<std::int32_t, 6>> stats_;
  bool counting_ = false;
  std::optional<std::int32_t> music_;
  ShowView view_;
};

// A fixed camera of the original's arena (STDVS_CAMERA): its angles (RotMatrix, about x then y; it
// never turns about z) and the translation of view space; the eye stands `translation` back from
// the point it hangs on, looking along the turned z axis.
struct FixedCamera {
  std::int32_t pitch = 0;
  std::int32_t yaw = 0;
  ShowPoint translation{};
};

// STD_CAMERA_PRESETS: the technique cameras, the first five by STD_selectRandomCamera's mode (0 and
// 1 behind the attacker to either side, 2 and 3 lower and nearer, 4 in front of it), the last three
// for a move that hits all around (range 3), one drawn at random.
inline constexpr std::array<FixedCamera, 9> TechniqueCameras{{
    {.pitch = 0x0e0, .yaw = 0x140, .translation = {0, 0xc8, 0x5dc}},
    {.pitch = 0x0e0, .yaw = 0xec0, .translation = {0, 0xc8, 0x5dc}},
    {.pitch = 0xfa0, .yaw = 0x140, .translation = {0, 0x104, 0x4b0}},
    {.pitch = 0xfa0, .yaw = 0xec0, .translation = {0, 0x104, 0x4b0}},
    {.pitch = 0x0e0, .yaw = 0x800, .translation = {0, 0x104, 0x5dc}},
    {.pitch = 0x000, .yaw = 0x000, .translation = {0, 0xc8, 0x5dc}},
    {.pitch = 0x0e0, .yaw = 0x140, .translation = {0, 0x190, 0xbb8}},
    {.pitch = 0x0e0, .yaw = 0xec0, .translation = {0, 0x190, 0xbb8}},
    {.pitch = 0x0e0, .yaw = 0x800, .translation = {0, 0x208, 0xbb8}},
}};

// Where a fixed camera looks from when it hangs on a point and turns with an angle about y
// (STD_setCameraToEntity: the view turned by -yaw and moved so that `origin` ends up at the
// translation), or on the world's origin with yaw 0 (STD_applyRotatingCamera).
[[nodiscard]] ShowView fixedCameraView(const FixedCamera& camera, ArenaPoint origin,
                                       std::int32_t yaw, std::int32_t projection);

// What a fighter of the match is doing on this frame, for the technique camera: where it stands,
// its yaw in the original's angles (posData rotation.vy) and the range of the move it performs
// (MOVE_DATA range 1 to 4), if any.
struct FightFigure {
  ArenaPoint at;
  std::int32_t yaw = 0;
  std::optional<std::int32_t> moveRange;
};

// The arena camera while a match runs (STD_tickVSPhase with STD_CAMERA_STATE 1, 3 and 6,
// STD_tickBattle): the orbit across the fighters (STD_setCameraOrbit); one technique in three a
// camera hung on its attacker until nobody attacks or is knocked back (STD_setupMoveExecution,
// STD_selectRandomCamera, STD_setCameraToEntity); every 600 frames, one time in two, a fixed eye
// high in a corner looking at the partner (STD_setRandomViewpoint(entity, 4)) for 60 to 100 frames.
// The start's banner pulls the orbit's nearest distance out to 4200 and back to 1000
// (STD_combatSetup: STD_DEFAULT_CAM_MIN_DISTANCE).
class MatchCamera {
public:
  explicit MatchCamera(ArenaCamera orbit) : orbit_(orbit) {}

  // STD_combatSetup: the banner shows (true) or bursts (false); the nearest distance moves 400.
  void tickBanner(bool showing);
  // STD_setupMoveExecution's end: fighter `fighter` (0 the partner, 1 the rival) starts a move of
  // range `range`; `focusedRange` is the range of the move the fighter the camera hangs on performs
  // now, if any. Comes before the frame's tickFight.
  void moveStarted(std::size_t fighter, std::int32_t range,
                   std::optional<std::int32_t> focusedRange, ruleset::Random& random);
  // One frame of the fight (STD_tickBattle's camera part): `busy` when a fighter attacks or is
  // knocked back.
  void tickFight(bool busy, ruleset::Random& random);
  [[nodiscard]] ShowView view(ArenaPoint partner, ArenaPoint rival,
                              const std::optional<ShowBones>& partnerBones) const;
  // The same with the fighters' yaws, which the technique camera turns with.
  [[nodiscard]] ShowView view(const std::array<FightFigure, 2>& fighters,
                              const std::optional<ShowBones>& partnerBones) const;
  [[nodiscard]] std::int32_t minDistance() const { return minDistance_; }
  [[nodiscard]] bool corner() const { return mode_ == Mode::Corner; }
  // The technique camera hangs on this fighter, with this preset (an index of TechniqueCameras).
  [[nodiscard]] std::optional<std::size_t> focused() const;
  [[nodiscard]] std::size_t preset() const { return preset_; }

  // STD_combatSetup: 200 as the banner opens.
  static constexpr std::int32_t BannerMinDistance = 200;

private:
  enum class Mode : std::uint8_t { Orbit, Technique, Corner };

  ArenaCamera orbit_;
  std::int32_t minDistance_ = BannerMinDistance;
  std::int32_t frame_ = 0;
  std::int32_t timer_ = 0;
  Mode mode_ = Mode::Orbit;
  std::size_t focused_ = 0;
  std::size_t preset_ = 0;
};

// STD_initializeCamera and STD_applyRotatingCamera (STD_CAMERA_STATE 10): from the cup's start and
// between its matches the arena turns slowly behind the bracket, the eye 500 up and 3000 back from
// its middle, tilted down 100, the turn 2 a frame (a whole turn in 2048 frames). The second pad's
// keys that move it (a debug aid) are left out.
class ArenaTurnCamera {
public:
  void tick() { yaw_ = (yaw_ + TurnStep) & 0xfff; }
  [[nodiscard]] std::int32_t yaw() const { return yaw_; }
  [[nodiscard]] ShowView view() const;

  static constexpr std::int32_t Pitch = 100;
  static constexpr ShowPoint Translation{0, 500, 3000};
  static constexpr std::int32_t Projection = 500;
  static constexpr std::int32_t TurnStep = 2;

private:
  std::int32_t yaw_ = 0;
};

// STD_startCameraIntro and STD_tickCameraIntro: as a match ends the camera hangs on the rival at
// the partner's eye height and circles it, ever faster after 30 frames, its distance and height
// following key frames, some drawn at random.
class EndCamera {
public:
  EndCamera(ArenaPoint partner, ShowBody partnerBody, ArenaPoint rival, ShowBody rivalBody,
            ruleset::Random& random);
  void tick();
  [[nodiscard]] ShowView view(ArenaPoint rival, const std::optional<ShowBones>& rivalBones) const;

private:
  ShowBody target_;
  std::int32_t frame_ = 0;
  std::int32_t angle_ = 0;
  std::array<std::int32_t, 22> distances_{};
  std::array<std::int32_t, 22> heights_{};
};

// STD_tickBattleResultScreen after time ran out: TIME UP (stdtim 002) and, a winner, from frame 60
// the damage box (STD_addTimeoutWindow) until frame 120 or a confirm after 60; a draw keeps the
// text 60 frames or until a confirm, then the drawn match's letters play.
class TimeUpNotice {
public:
  explicit TimeUpNotice(bool draw) : draw_(draw) {}
  void tick(bool confirm);
  [[nodiscard]] bool done() const { return done_; }
  [[nodiscard]] bool boxShown() const { return boxShown_; }
  [[nodiscard]] std::int32_t frame() const { return frame_; }

  static constexpr std::int32_t BoxFrame = 0x3c;
  static constexpr std::int32_t SkipFrom = 0x3d;
  static constexpr std::int32_t WinnerFrames = 0x79;

private:
  bool draw_ = false;
  std::int32_t frame_ = 0;
  bool boxShown_ = false;
  bool done_ = false;
};

// The 3D letters over a match's end, each one object of its model (STDDAT\WIN_LOSE.TMD,
// DRAW.TMD): where it is in the scene's own space (STD_MODEL_SCENE_MATRIX: 1000 in front of the
// eye) and its angles; the projection the scene is drawn with.
struct LetterPose {
  std::size_t object = 0;
  ShowPoint location{};
  ShowPoint rotation{};
  // Its height's scale, 4096 for none (the champion's title is drawn twice as tall).
  std::int32_t scaleY = 0x1000;
  // How many texels to the left of where its face rests the face reads the title's gold strip,
  // 0 at rest (std_effect.c: STD_tickChampionScene: the gleam that sweeps over each letter).
  std::int32_t gleamShift = 0;
};
struct LetterScene {
  std::vector<LetterPose> letters;
  std::int32_t projection = 0;
  // STD_renderLoseScene: the 32 columns of the half-dark wipe, 10 pixels wide from the left edge,
  // their heights from the top (0 to 240).
  std::vector<std::int32_t> wipe;
};

enum class MatchEnding : std::uint8_t { Won, Lost, Drawn };

// STD_addWinScene/STD_tickWinScene, STD_addLoseScene/STD_tickLoseScene,
// STD_loadVersusSceneModel/STD_tickVersusModelScene: one frame at a time from the moment the match
// is decided.
class MatchLetters {
public:
  MatchLetters(MatchEnding ending, ruleset::Random& random);
  void tick();
  [[nodiscard]] LetterScene scene() const;
  [[nodiscard]] MatchEnding ending() const { return ending_; }
  // STD_isVersusModelSceneFinished: the drawn match's letters are in.
  [[nodiscard]] bool finished() const;

private:
  MatchEnding ending_;
  std::int32_t frame_ = 0;
  std::array<std::int32_t, 4> steps_{};
  std::array<std::int32_t, 4> z_{};
  std::array<std::int32_t, 32> wipe_{};
  std::array<std::int32_t, 32> order_{};
  std::array<std::int32_t, 4> drawX_{};
};

} // namespace dwe::game::tournament
