#pragma once

#include <dwe/game/gym/gym_rules.hpp>
#include <dwe/game/gym/slot_machine.hpp>
#include <dwe/game/gym/stat_countup.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dwe::game::gym {

// What a session gave: the stats before it and its gains (trn_reward.c: TRN_award*TrainingGains).
struct Award {
  GymStats before{};
  GymStats gains{};
};

// Who a course moves: the partner, the tamer (the brains course) or the map's first character
// (NPC_ENTITIES[0], the teacher of the second brains course).
enum class Actor : std::uint8_t { Partner, Tamer, Teacher };

// What a course asks of the field, the actors, the sound and the screen. The game implements it;
// a course knows none of them.
class GymPorts {
public:
  GymPorts() = default;
  GymPorts(const GymPorts&) = delete;
  GymPorts& operator=(const GymPorts&) = delete;
  GymPorts(GymPorts&&) = delete;
  GymPorts& operator=(GymPorts&&) = delete;
  virtual ~GymPorts() = default;

  // tamerSetState and partnerSetState: the actors are the course's while training, the player's
  // after.
  virtual void setTraining(bool training) = 0;
  virtual void animate(Actor actor, std::int32_t animation) = 0;
  // Whether the actor's animation reached its last frame (anim.animFrame >= frameCount).
  [[nodiscard]] virtual bool animationEnded(Actor actor) = 0;
  // 4096 per turn.
  virtual void face(Actor actor, std::int32_t angle) = 0;
  // entityLookAtLocation, both ways.
  virtual void faceEachOther() = 0;
  // entityLookAtLocation: `actor` turns to `target`.
  virtual void lookAt(Actor actor, Actor target) = 0;
  // tickEntityWalkTo: one frame of a walk to a spot; with no spot, the partner walks back to the
  // tamer. True once there.
  virtual bool walk(Actor actor, std::optional<Point> spot) = 0;
  // createCameraMovement: to a spot, or back to the tamer with no spot.
  virtual void moveCamera(std::optional<Point> spot, std::int32_t speed) = 0;
  virtual void playSound(std::int32_t slot, std::int32_t sound) = 0;
  // playSound2 and thunkStopSoundMask: a sound that plays until stopped.
  virtual std::int32_t loopSound(std::int32_t slot, std::int32_t sound) = 0;
  virtual void stopSound(std::int32_t handle) = 0;
  // storeMapObjectPosition and loadMapObjectPosition: the objects go back where they were stored.
  virtual void storeObjects(const ObjectGroup& group) = 0;
  virtual void restoreObjects(const ObjectGroup& group) = 0;
  virtual void moveObjects(const ObjectGroup& group, ObjectMove move) = 0;
  // setMapObjectsFlag: hidden or shown.
  virtual void showObjects(const ObjectGroup& group, bool shown) = 0;
  // resetMapObjectAnimation: their sprites play again from the first frame.
  virtual void restartObjects(const ObjectGroup& group) = 0;
  // MAP_TILE_DATA.objects[object].y: where an object is now, in background pixels.
  [[nodiscard]] virtual std::optional<std::int32_t> objectRow(std::int32_t object) = 0;
  // getDrawPosition of the top of the partner's head: its background pixel row, if known.
  [[nodiscard]] virtual std::optional<std::int32_t> partnerHeadRow() = 0;
  [[nodiscard]] virtual std::optional<Point> partnerPosition() = 0;
  virtual void movePartner(Point delta) = 0;
  // anim.c: applyRootMomentum: a running clip carries its actor along its facing, one frame's
  // worth.
  virtual void runOn(Actor actor) = 0;
  // Whether the partner is drawn on screen (entity.isOnScreen).
  [[nodiscard]] virtual bool partnerOnScreen() = 0;
  // Puts the partner at another x or z, where it runs on from.
  virtual void placePartner(Axis axis, std::int32_t value) = 0;
  [[nodiscard]] virtual std::string partnerSpecies() = 0;
  // createCloudFX, createParticleFX and removeAllCloudFX.
  virtual void addCloud(Point at) = 0;
  virtual void addParticle(Point3 at) = 0;
  virtual void removeClouds() = 0;
  // closeBox(0): closes the story's textbox (the roulette's "Slot Game" help).
  virtual void closeTextbox() = 0;
  // randomLimit: 0 to limit - 1.
  virtual std::int32_t randomBelow(std::int32_t limit) = 0;
  // Trains the partner with the roulette's result in tenths (0 for none).
  virtual Award award(const CourseRules& course, std::int32_t slotTenths) = 0;
};

// What the player pressed this frame.
struct GymInput {
  // Confirm newly pressed. With cancel, it ends the stats count at once.
  bool confirm = false;
  // Cancel newly pressed: it ends the session early and, with confirm, the stats count.
  bool cancel = false;
  // Cancel held (POLLED_INPUT & CANCEL_BUTTON): it also ends the session early.
  bool cancelHeld = false;
};

// One run of a course, as trn_*.c: TRN_tick*Training: the actors walk to the machine, the partner
// works until the session ends or the player cancels, the stats box counts the gains, and the
// partner goes back. It steps whole frames of the original; each course's steps are numbered as
// the original's TRAINING_STATE.
class CourseRun {
public:
  // The rules must outlive the run. With the roulette, it decides when the session ends and the
  // rate of the gains.
  CourseRun(const GymRules& gym, const CourseRules& course, bool withSlots);

  // Real seconds of a logic tick; `frameSeconds` is how long a frame of the original lasts.
  void tick(GymPorts& ports, double seconds, double frameSeconds, const GymInput& input);

  // TRAINING_COMPLETE.
  [[nodiscard]] bool finished() const { return finished_; }
  // The stats box while it is on screen.
  [[nodiscard]] const std::optional<StatCountup>& countup() const { return countup_; }
  [[nodiscard]] const CourseRules& course() const { return *course_; }
  // The roulette while it is on screen.
  [[nodiscard]] const std::optional<SlotMachine>& slots() const { return slots_; }

private:
  void frame(GymPorts& ports, const GymInput& input);
  void hpFrame(GymPorts& ports, const HpCourse& hp, const GymInput& input);
  void mpFrame(GymPorts& ports, const MpCourse& mp, const GymInput& input);
  void offenseFrame(GymPorts& ports, const OffenseCourse& offense, const GymInput& input);
  void defenseFrame(GymPorts& ports, const DefenseCourse& defense, const GymInput& input);
  void speedFrame(GymPorts& ports, const SpeedCourse& speed, const GymInput& input);
  void brainFrame(GymPorts& ports, const BrainCourse& brain, const GymInput& input);
  void defensePulseFrame(GymPorts& ports, const DefensePulseCourse& defense, const GymInput& input);
  void defenseDropFrame(GymPorts& ports, const DefenseDropCourse& defense, const GymInput& input);
  void treadmillFrame(GymPorts& ports, const TreadmillCourse& treadmill, const GymInput& input);
  // trn_off.c states 7 and 8, trn2_off.c states 2 and 3: the hit swaps objects instead.
  void offenseSwapFrame(GymPorts& ports, const OffenseCourse& offense, const OffenseSwap& swap,
                        const GymInput& input);
  // createParticleFX at each point.
  void particles(GymPorts& ports, const std::vector<Point3>& points) const;

  // The steps every course shares.
  void begin(GymPorts& ports);
  // TRN_startSlotSessionIfEnabled.
  void startSlots(GymPorts& ports);
  // TRN_startSlotSpin: the partner is at the machine.
  void startSpin();
  void award(GymPorts& ports);
  // The session ran its frames, or the player cancelled.
  [[nodiscard]] bool sessionOver(const GymInput& input) const;
  // The same, with the counter past its frames (trn2_def_*.c set it to 1200 on cancel).
  [[nodiscard]] bool sessionOverCounted() const;
  [[nodiscard]] bool counted() const;
  [[nodiscard]] bool rested();
  void finish(GymPorts& ports);
  void setState(std::int32_t state);
  void cloudLine(GymPorts& ports, const CloudLine& line) const;
  // moveMapObjectsWithLimit: moves the objects, true once the first reaches `limit` from where
  // they were stored, in the direction of the move.
  bool moveObjectsTo(GymPorts& ports, const ObjectGroup& group, std::int32_t dx,
                     std::int32_t limit);

  const GymRules* gym_;
  const CourseRules* course_;
  std::int32_t state_ = 0;
  bool finished_ = false;
  // tamer.c: tamerSetState(8): the player watches the partner train.
  bool watching_ = false;
  double pendingSeconds_ = 0.0;
  // MAIN_D_8013537A: frames into the session; the HP course then keeps the beats to undo.
  std::int32_t counter_ = 0;
  // MAIN_D_8013537E: frames into a swing or punch.
  std::int32_t step_ = 0;
  // MAIN_D_8013537C.
  std::int32_t rest_ = 0;
  // How far the objects moved in x since they were stored.
  std::int32_t shift_ = 0;
  std::optional<std::int32_t> loop_;
  std::size_t swing_ = 0;
  std::optional<StatCountup> countup_;
  bool withSlots_ = false;
  std::optional<SlotMachine> slots_;
  // An input seen during a tick that steps no frame still counts on the next frame.
  GymInput pending_;
};

} // namespace dwe::game::gym
