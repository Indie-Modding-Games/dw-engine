#pragma once

#include <dwe/game/field/field.hpp>
#include <dwe/game/gym/course_flow.hpp>
#include <dwe/game/gym/gym_rules.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace dwe::session {

struct GymFieldPortsDesc {
  GameSession* session = nullptr;
  game::field::Field* field = nullptr;
  const game::gym::GymRules* rules = nullptr;
  // The story's flags (a gym's upgrades) and its textbox, and the bag (the training manual):
  // without them, no upgrade, no manual and no textbox to close.
  StoryAdapter* story = nullptr;
  InventoryAdapter* inventory = nullptr;
};

// A course's ports on the field and the session. The story holds the field while a course runs (it
// waits for the gym), so the actors move, turn, animate and hold their clips as a script moves
// them, the camera pans as a script pans it, the sounds play and the session trains the partner.
// What only the game draws (the map's objects, the effects, the screen's rows) is a subclass's.
// Everything in the description must outlive the ports.
class GymFieldPorts : public game::gym::GymPorts {
public:
  explicit GymFieldPorts(const GymFieldPortsDesc& desc);

  void setTraining(bool training) override;
  void animate(game::gym::Actor actor, std::int32_t animation) override;
  void holdInPlace(game::gym::Actor actor, bool held) override;
  void face(game::gym::Actor actor, std::int32_t angle) override;
  void lookAt(game::gym::Actor actor, game::gym::Actor target) override;
  void faceEachOther() override;
  bool walk(game::gym::Actor actor, std::optional<game::gym::Point> spot) override;
  void moveCamera(std::optional<game::gym::Point> spot, std::int32_t speed) override;
  void playSound(std::int32_t slot, std::int32_t sound) override;
  std::optional<std::int64_t> loopSound(std::int32_t slot, std::int32_t sound) override;
  void stopSound(std::int64_t handle) override;
  std::optional<game::gym::Point> partnerPosition() override;
  void movePartner(game::gym::Point delta) override;
  bool partnerOnScreen() override;
  void placePartner(game::gym::Axis axis, std::int32_t value) override;
  std::string partnerSpecies() override;
  void closeTextbox() override;
  std::int32_t randomBelow(std::int32_t limit) override;
  game::gym::Award award(const game::gym::CourseRules& course, std::int32_t slotTenths) override;

  // A course that never reached its end (the screen goes with the map) leaves no voice behind.
  void stopHeldSounds();

protected:
  // The ground speed of a clip of the entity's model, 0 without one: the walks go at the speed of
  // the walk clip (anim.c: applyRootMomentum).
  [[nodiscard]] virtual float clipGroundSpeed(scene::Entity entity, std::uint32_t clip) const = 0;
  // What an award adds for the screen, such as the brains course's new orders and moves.
  virtual void awarded(const game::gym::CourseRules& /*course*/,
                       const game::gym::Award& /*award*/) {}

  [[nodiscard]] game::field::Field& field() const { return *desc_.field; }
  [[nodiscard]] GameSession& session() const { return *desc_.session; }
  [[nodiscard]] scene::Entity entityOf(game::gym::Actor actor) const;
  [[nodiscard]] glm::vec3 positionOf(game::gym::Actor actor) const;
  // The teacher too: NPC_ENTITIES[0], if the map has it.
  [[nodiscard]] std::optional<scene::Entity> anyEntityOf(game::gym::Actor actor) const;
  [[nodiscard]] std::optional<glm::vec3> anyPositionOf(game::gym::Actor actor) const;

private:
  // NPC_ENTITIES[0]: the script id of the map's first character record.
  [[nodiscard]] std::optional<std::int32_t> teacherId() const;
  // overworld.c: setActiveAnim, as the story animates an NPC: 0 hands it back to its own motion.
  void animateTeacher(std::int32_t animation);
  // The last clip's ground speed (anim.c: applyRootMomentum); a clip without one walks at the
  // field's walk speed.
  [[nodiscard]] float walkSpeed(game::gym::Actor actor) const;

  GymFieldPortsDesc desc_;
  std::vector<std::int64_t> held_;
  // Whether the course holds the field itself; each actor's walk and last clip (player, partner).
  bool ownHold_ = false;
  std::array<bool, 2> walks_{};
  std::array<std::optional<std::uint32_t>, 2> clips_{};
  // Only the clouds' scatter and the moves taught are random; seeded from the partner's numbers, a
  // replayed game does the same.
  std::minstd_rand random_;
};

// The gym's points are in the original's axes (gym.yaml), whose z runs the other way to the
// world's: a course spot taken as a world point sent the partner and the camera off the map.
[[nodiscard]] glm::vec3 gymWorldOf(game::gym::Point spot, float height);

} // namespace dwe::session
