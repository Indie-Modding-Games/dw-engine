#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/scene/components.hpp>
#include <dwe/scene/world.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The map's NPCs on the field (overworld.c: loadMapDigimon, scriptSetDigimon,
// NPCEntityTickOverworld; map.c: entityCheckCollision, collisionGrace): the story puts them on the
// map from the map's NPC records, they walk their waypoint paths, block and are blocked by the
// other characters, and report the touches and talks that run their script section.
namespace dwe::game::field {

// Marks an NPC entity: the script id the story names it by (NPCEntity.scriptId).
struct NpcTag {
  std::int32_t scriptId = 0;
};

// What the story asks for to put an NPC on the map (scriptSetDigimon).
struct NpcSpawn {
  // The NPC record of the map (MapDocument::npcs).
  std::size_t slot = 0;
  // The species the script names: the record's must be the same.
  std::int32_t species = 0;
  // Whether touching it runs its section without confirm.
  bool autotalk = false;
  // The .glb drawn for it, and its collision size (DIGIMON_DATA[type].radius).
  std::string model;
  float radius = 0.0F;
};

// How an NPC's section was asked for.
enum class NpcContactCause : std::uint8_t {
  // An NPC with autotalk and the player touched, from either side.
  Touch,
  // The player pressed confirm at an NPC without autotalk.
  Confirm,
};

// The player touched or talked to an NPC: its section of the map's script should run, if no script
// runs (tamer.c: checkMapInteraction, overworld.c:1305-1340).
struct NpcContact {
  std::int32_t scriptId = 0;
  scene::Entity entity{};
  NpcContactCause cause = NpcContactCause::Touch;
};

// A character's collision square on the ground: centre and half its side (DIGIMON_DATA radius).
struct GroundSquare {
  glm::vec2 centre{};
  float half = 0.0F;
};

// The other characters an NPC can bump into.
struct NpcSurroundings {
  GroundSquare player;
  GroundSquare partner;
  // Whether the player moves freely (TAMER_STATE 0): only then does an NPC's touch count.
  bool playerFree = false;
};

// The first entity instance an NPC takes: 0 is the player and 1 the partner (overworld.c:
// ENTITY_TABLE, the scripts' move slots).
inline constexpr std::size_t FirstNpcInstance = 2;

class FieldNpcs {
public:
  // The world must outlive the NPCs.
  FieldNpcs(scene::World& world, NpcRules rules, float frameSeconds);

  // A new map: the NPCs of the last one go (overworld.c: removeMapEntities) and the map's records
  // are the ones the story can put on the map.
  void enterMap(std::span<const assets::MapNpc> records);

  // The map's NPC record in a slot, or nothing.
  [[nodiscard]] const assets::MapNpc* record(std::size_t slot) const;

  // Puts the record's NPC on the map at its place, replacing the one in that slot. False, and
  // nothing changes, when the record is of another species or there is no such record.
  bool spawn(const NpcSpawn& spawn);
  // Takes an NPC off the map (scriptUnloadEntity). False when no NPC has that script id.
  bool unload(std::int32_t scriptId);
  // set_autotalk. False when no NPC has that script id.
  bool setAutotalk(std::int32_t scriptId, bool autotalk);
  // The entity of an NPC on the map, by script id (script_value.c: scriptIdToEntityId).
  [[nodiscard]] std::optional<scene::Entity> entityOf(std::int32_t scriptId) const;
  // Where an NPC on the map stands now, in world axes, by script id.
  [[nodiscard]] std::optional<glm::vec3> positionOf(std::int32_t scriptId) const;
  // Its entity instance, FirstNpcInstance + its record's slot (the scripts' move slot).
  [[nodiscard]] std::optional<std::size_t> instanceOf(std::int32_t scriptId) const;

  // Holds an NPC still for a script, or lets it go (overworld.c: setMovementEnabled, stopAnim):
  // held, it stands idle; holding it starts its path over (clearMapAITable). False when no NPC has
  // that script id.
  bool hold(std::int32_t scriptId, bool held);
  // The same for every NPC on the map (setMovementEnabled(-1, ...)).
  void holdAll(bool held);
  // A script plays the NPC's animation `animation` (overworld.c: setActiveAnim, NPC_ACTIVE_ANIM):
  // while it is not 0 the NPC's own tick leaves it to the script. False when no NPC has that
  // script id.
  bool setScriptedAnimation(std::int32_t scriptId, std::int32_t animation);

  // Whether a character moving from `from` to `to` walks into an NPC (map.c:
  // entityCheckEntityCollision): its square at `to` overlaps an NPC's it was not getting away from.
  [[nodiscard]] bool blocks(glm::vec3 from, glm::vec3 to, float half) const;

  // The player's side (tamer.c: checkMapInteraction): the NPC under the player's square, reaching
  // talkReach further the way it faces when confirm is pressed while standing. A touch when it has
  // autotalk, a talk when confirm is pressed and it has not.
  void touchFromPlayer(GroundSquare player, glm::vec2 facing, bool standing, bool confirm);

  // One tick of every NPC: its waypoint path, its move with the collision of the map and the other
  // characters, and its touch of the player.
  void tick(float seconds, const assets::MapCollision& collision,
            const NpcSurroundings& surroundings);

  // Starts a tick of every NPC's transform, so it is drawn between this tick's start and end.
  void beginTick();

  // The touches and talks since clearContacts.
  [[nodiscard]] std::span<const NpcContact> contacts() const { return contacts_; }
  void clearContacts() { contacts_.clear(); }

private:
  // What stopped an NPC's last move (NPC_COLLISION_STATE: -1, 0, 1, 2-9, 10).
  enum class Blocker : std::uint8_t { None, Player, Partner, Npc, Wall };

  struct Npc {
    std::size_t slot = 0;
    std::int32_t scriptId = 0;
    scene::Entity entity{};
    float half = 0.0F;
    bool autotalk = false;
    // map.c: entityCheckCollision skips NPCs with no HP left.
    bool solid = true;
    // The waypoint path (MapDigimonEntity.path, hasWaypointTarget, waypointWaitTimer).
    std::size_t activeSection = 0;
    bool hasTarget = false;
    float waitSeconds = 0.0F;
    glm::vec3 target{};
    scene::Motion motion = scene::Motion::Idle;
    // A path that points past its waypoints: logged once, and the NPC stands.
    bool pathBroken = false;
    Blocker blocker = Blocker::None;
    // Held by a script (MapDigimonEntity.stopAnim) and the script's animation (NPC_ACTIVE_ANIM).
    bool held = false;
    std::int32_t scriptedAnimation = 0;
    // How far it is in its reaction to the player (MapDigimonEntity.lookAtTamerState): 0 while it
    // has not seen the player, and its path runs only then.
    std::uint8_t trackState = 0;
  };

  [[nodiscard]] Npc* find(std::int32_t scriptId);
  [[nodiscard]] const Npc* find(std::int32_t scriptId) const;
  void remove(std::size_t index);
  static void setHeld(Npc& npc, bool held);
  void tickPath(Npc& npc, float seconds);
  // overworld.c:1247-1303: the reaction of the NPC's follow mode, if the rules have one.
  [[nodiscard]] static const NpcTrackingMode* trackingOf(const NpcTrackingRules& tracking,
                                                         const assets::MapNpc& record);
  [[nodiscard]] bool sees(const Npc& npc, const assets::MapNpc& record,
                          const NpcTrackingRules& tracking, NpcSpot spot, glm::vec2 player) const;
  void track(Npc& npc, const assets::MapNpc& record, const NpcTrackingRules& tracking,
             const NpcTrackingMode& mode, glm::vec2 player, float seconds);
  void look(Npc& npc, const assets::MapNpc& record, const NpcTrackingRules& tracking,
            glm::vec2 player, float seconds);
  void chase(Npc& npc, const assets::MapNpc& record, const NpcTrackingRules& tracking,
             glm::vec2 player, float seconds);
  void follow(Npc& npc, const assets::MapNpc& record, const NpcTrackingRules& tracking,
              NpcGait gait, glm::vec2 player, float seconds);
  // getRotationDifference and rotateEntity at the NPC's turn speed, unless it is blocked.
  void turnTo(Npc& npc, glm::vec3 target, float seconds);
  // Whether the NPC stands on its target's collision cell (getModelTile).
  [[nodiscard]] bool reachedTarget(const Npc& npc) const;
  // Whether it waited this many of the original's frames, counting this tick.
  [[nodiscard]] bool waited(Npc& npc, std::int32_t frames, float seconds) const;
  static void setTrackState(Npc& npc, std::uint8_t state);
  void waitOnPath(Npc& npc, const assets::MapNpc& record, float seconds) const;
  void walkOnPath(Npc& npc, const assets::MapNpc& record, scene::Motion motion, float seconds);
  void move(Npc& npc, float seconds, const assets::MapCollision& collision,
            const NpcSurroundings& surroundings);
  [[nodiscard]] Blocker probe(const Npc& npc, glm::vec2 step, const assets::MapCollision& collision,
                              const NpcSurroundings& surroundings) const;
  void turnFromBlock(Npc& npc, float seconds, const assets::MapCollision& collision,
                     const NpcSurroundings& surroundings);
  [[nodiscard]] GroundSquare squareOf(const Npc& npc) const;
  [[nodiscard]] float speedOf(scene::Motion motion) const;

  scene::World* world_;
  NpcRules rules_;
  float frameSeconds_;
  std::vector<assets::MapNpc> records_;
  std::vector<Npc> npcs_;
  std::vector<NpcContact> contacts_;
};

} // namespace dwe::game::field
