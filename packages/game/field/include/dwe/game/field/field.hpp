#pragma once

#include <dwe/assets/clip_motion.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/game/field/partner_follow.hpp>
#include <dwe/game/field/player_trail.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/scene/components.hpp>
#include <dwe/scene/world.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::field {

// What the player wants this tick, as plain values: the app turns keys, sticks and clicks into
// this.
struct FieldInput {
  // A direction relative to the map's camera: x to the right of the screen, y away from the camera
  // (up on the stick or the W key). Length 0 to 1; shorter than the dead zone is no movement. It
  // cancels a move target.
  glm::vec2 stick{};
  // Run instead of walk.
  bool run = false;
  // Click-to-move: a ground point (from picking on the map) to walk to around walls. It replaces
  // the previous one; it stays until reached, until the stick is used or until cancelTarget.
  std::optional<glm::vec3> moveTarget = std::nullopt;
  bool cancelTarget = false;
  // The confirm button was pressed this tick: on a cell of value 80-109 it runs the map's script
  // (tamer.c: checkMapInteraction).
  bool confirm = false;
};

// The player stepped on a warp: the app loads `map` and calls Field::enterMap with `exit`.
struct MapChange {
  std::string map;
  std::int32_t exit = 0;
  // The collision value that fired it (110 + exit of the old map, or the cell of a script's map
  // change).
  std::int32_t trigger = 0;
};

// Whether an exit of map `from` may take the player to map `to` now (the story lock of
// packages/session). A closed one keeps the player on the map.
using ExitFilter = std::function<bool(std::string_view from, std::string_view to)>;

// The player stepped onto a trigger cell that is not a warp (scripts, the toilet): for later
// systems.
struct TriggerStep {
  std::int32_t trigger = 0;
  assets::CollisionCell cell;
};

// How the field takes the map changes of the scripts (field/script_warps).
struct ScriptWarpSetup {
  // NOLINTNEXTLINE(readability-redundant-member-init): the default lets designated initializers leave it out.
  std::vector<ScriptWarp> warps = {};
  // What their guards read, live.
  // NOLINTNEXTLINE(readability-redundant-member-init): the default lets designated initializers leave it out.
  GuardFacts facts = {};
  // Where the triggers a taken warp sets go; without it they go nowhere.
  TriggerWriter writeTrigger = nullptr;
  // What the field does with a recursion or a trigger it is told it cannot know.
  UnknownPolicy unknownPolicy = VanillaUnknownPolicy;
  // Free exploration: the scripts' map changes go wherever they can go, whatever their guards (off:
  // the original's conditions, untouched).
  bool explore = false;
  // The maps the field can enter, for free exploration's places; empty: every map.
  MapFilter hasMap = nullptr;
};

// Free exploration's choice on a cell whose scripts go to more than one place: the app shows it,
// and the player picks one (Field::chooseDestination) or stays.
struct DestinationChoice {
  std::string map; // where the player stands
  std::int32_t cell = 0;
  std::vector<MapChange> places;
  // The place the original's conditions take now, if any: what the choice starts on.
  std::optional<std::size_t> vanilla;
};

// A character as the minimap and the debug views see it.
struct CharacterState {
  glm::vec3 position{};
  float yaw = 0.0F;
  scene::Motion motion = scene::Motion::Idle;
};

// How a script's glide goes with the root motion of the clip the character plays (moveByClip): a
// scripted walk's glide stands for it, a scripted move's adds to it.
enum class ScriptGlide : std::uint8_t { ReplacesClipMotion, AddsToClipMotion };

// A character a script holds (script_interp.c: scriptSetEntityWalking, scriptStartAnimation): it
// glides at `velocity` (world units a second) with no collision, and plays `clip` (the original's
// animation id) instead of the clip of its motion.
struct ScriptedActor {
  glm::vec3 velocity{};
  ScriptGlide glide = ScriptGlide::ReplacesClipMotion;
  std::optional<std::uint32_t> clip;
  // How many times a script started a clip: anim.c: startAnimation restarts even the clip that
  // plays, as each push of the gym's log does.
  std::uint32_t starts = 0;
};

// The root motion of every clip of a character's model (anim.c: tickMomentum): the app gives it
// to the player and the NPCs once their models are in, setPartnerClips to the partner.
struct CharacterClipMotions {
  std::shared_ptr<const assets::ClipMotions> clips;
};

// A clip that carries its character by its root motion (Field::moveByClip): the clip's frame and
// the time toward the next frame.
struct ClipRootMotion {
  std::uint32_t clip = 0;
  std::size_t frame = 0;
  float seconds = 0.0F;
  // anim.c: animFlag 2: the clip plays on, but its root motion does not carry the character.
  bool held = false;
};

// One original frame of a clip's root motion (anim.c: tickAnimation, applyRootMomentum): the step
// of the clip's frame `frame` along the facing `yaw`, in world units. Nothing once a clip that
// does not repeat has ended.
std::optional<glm::vec3> clipRootStep(const assets::ClipMotion& clip, std::size_t frame, float yaw);

// A 3D object of the map, a door model (map_object.c: loadDoors, Map3DObject), in world space.
struct MapObject3D {
  // The door model, DOOR\DOORnn.TMD.
  std::int32_t model = 0;
  glm::vec3 position{};
  float yaw = 0.0F;
};

// Whether the game time runs while a care scene holds the player (partner.c: the care states keep
// it running; partnerTickSleep stops it).
enum class CareClock : std::uint8_t { Runs, Stops };

// The field's logic for one map at a time (docs/reference/campo.md): the player moves by intents
// with the original's grid collision, steps on triggers and warps, and the partner follows. Player
// and partner are entities of the scene world, with the shared components the app draws (Transform,
// ModelRef, AnimationIntent, PlayerTag, PartnerTag). No rendering, input devices or file access:
// the app feeds input and map documents, and reads state and changes.
class Field {
public:
  // Creates the player and the partner in the world. The world must outlive the field. The cells of
  // the map's script ask the story for their sections (cellSections), which change the map
  // themselves (startScriptedMapChange). Until the story runs the map's cells (setStoryCells), and
  // under free exploration, the scripts' map changes of field/script_warps fire instead on stepping
  // on (or confirming on) their cells when their facts decide them taken, under the setup's unknown
  // policy for a recursion or a trigger it is told it cannot know (logged once per warp); the ones
  // it cannot decide are logged once and not taken. A warp taken writes the triggers it sets on its
  // way.
  Field(scene::World& world, FieldRules rules, ScriptWarpSetup scriptWarps = {});

  // Enters a map through one of its exits: the player appears at the exit's spawn and the partner
  // next to it (tamer.c: loadMapEntities), and the screen fades in (tamer.c: tamerTickChangeMap)
  // before the player can move. Fails if the map has no such exit.
  core::Result<void> enterMap(const assets::MapDocument& map, std::int32_t exit);

  // Enters a map at a given place, without a fade in, for a loaded game, tests and debug tools. The
  // partner stands at `partner` when it is given and free (main.c: initializeLoadedMap puts it back
  // where the save left it), else next to the player.
  void enterMapAt(const assets::MapDocument& map, assets::SpawnPoint spawn,
                  std::optional<assets::SpawnPoint> partner = std::nullopt);

  // Play starts on the map just entered (main.c: main: fadeFromBlack(0x28) after initializeMap):
  // the fade in lasts `transition.startFadeInFrames` and holds no one, so the map's cells run from
  // its first frame, and a map change they start fades out from the shade the fade in drew.
  void startPlay();

  // Sets the intent for the next ticks.
  void setInput(const FieldInput& input);

  // Advances the logic by `seconds` (the app's fixed tick). On a warp the player walks on for the
  // fade out, without collision and without input; then nothing moves until the app enters the new
  // map and the fade in ends. The player and the partner walk on frames of the original
  // (`transition.frameSeconds`, 20 a second): a tick that starts a frame steps them a whole frame,
  // the ticks between leave them where they are (drawnPose draws them gliding). A press from
  // standing starts a frame on its own tick.
  void tick(float seconds);

  // Where to draw a character between the last two ticks (alpha from core::FixedStep). While the
  // last thing that moved the player or the partner was a frame's step, it glides over the frame's
  // ticks from where it was drawn when the frame began to where the step left it, so the steps
  // look smooth; anything else is drawn as scene::interpolate draws it. With smooth motion off
  // every character is drawn where its last tick left it, as the original draws its frames.
  [[nodiscard]] scene::Pose drawnPose(scene::Entity entity, float alpha) const;
  // Smooth motion (the engine's, on by default): drawnPose glides and interpolates. The logic is
  // the same either way; only the drawing changes.
  void setSmoothMotion(bool on) { smoothMotion_ = on; }
  [[nodiscard]] bool smoothMotion() const { return smoothMotion_; }

  // The warp the player stepped on, once the fade out has ended: after taking it the app loads the
  // map and calls enterMap.
  std::optional<MapChange> takeMapChange();

  // A script's map change (map.c: scriptTickChangeMap): the same fades as a warp, with the player
  // standing, and the new map's name only if `showName`. The app takes it as any other.
  void startScriptedMapChange(MapChange change, bool showName);

  // The sections of the map's script the player's cell asks for in the last tick (tamer.c:
  // checkMapInteraction): a step cell (51-79) every tick the player stands on it, a confirm cell
  // (80-109) when confirm is pressed on it, both only while the player moves freely. The story
  // drops them while a script runs. A cell whose map change field/script_warps takes
  // (setStoryCells) asks for none.
  [[nodiscard]] std::span<const std::int32_t> cellSections() const { return cellSections_; }

  // Whether a map change is fading out, waiting to be taken or waiting for its map.
  [[nodiscard]] bool mapChangePending() const {
    return leaving_.has_value() || pendingChange_.has_value() || waitingForMap_;
  }

  // How much the map change's fade takes away from the screen this tick, from 0 (nothing) to 1
  // (everything: black). The original subtracts a grey from every pixel (fade.c: renderFade), and
  // the app draws it that way.
  [[nodiscard]] float fadeLevel() const;
  // Whether a map change's fade runs (fade.c: FADE_PROTECTION): from the fade out's first frame
  // (fadeToBlack sets it) until the fade in's end (renderFadeIn clears it on its last frame), one
  // frame after the player walks again. It keeps the game menu and the pause shut (tamer.c:520,
  // utils2.c: handlePause).
  [[nodiscard]] bool fadeProtected() const {
    return mapChangePending() || fadeInSeconds_.has_value();
  }
  // The shade the screen holds now (fade.c: FADE_PROGRESS), nothing when no fade has run.
  [[nodiscard]] std::optional<std::int32_t> shadeNow() const;
  // A fade the field does not run (the story's fade to white, WhiteFade) drew `shade`:
  // FADE_PROGRESS is shared, so the next map change fades out from it.
  void setShade(std::int32_t shade) { fadeProgress_ = shade; }
  // A fade out another screen runs (a fled battle's fadeToBlack, battle_tick.c: handleCommands)
  // drew `shade`: the screen keeps it after that screen ends, until a map change fades on from it
  // or releaseShade fades it back in. A fade in under way stops.
  void holdShade(std::int32_t shade);
  // The held shade fades back in (fadeFromBlack(transition.fadeInFrames)), for when no map change
  // follows; nothing when none is held.
  void releaseShade();
  [[nodiscard]] bool shadeHeld() const { return shadeHeld_; }
  // Runs only the map change's fade in, for the ticks something else holds the field (a battle).
  // fade.c: renderFadeIn is a drawn object (addObject 4005) that steps on every frame the game
  // draws, whatever runs the logic, so a battle a script starts before the fade in ends (a story
  // battle right after a map change) still sees the screen come back.
  void tickFadeIn(float seconds);

  // The map whose name the map change shows now: from the fade out's name frame until the new map
  // is entered (tamer.c: tamerTickChangeMap, map.c: addMapNameObject). Nothing otherwise.
  [[nodiscard]] std::optional<std::string_view> mapNameShown() const;

  // Whether game time ran during the last tick. The field has no clock of its own: whoever owns the
  // game clock (ruleset::GameClock) advances it by the tick only then. The original stops game time
  // for a map change, from the warp until the fade in gives the player back control (tamer.c:
  // stopGameTime, startGameTime).
  [[nodiscard]] bool gameTimeRan() const { return gameTimeRan_; }

  // The trigger cells the player stepped onto during the last tick.
  [[nodiscard]] std::span<const TriggerStep> triggerSteps() const { return triggerSteps_; }

  // Which of the player's map changes may happen: a walked warp, a script warp of a cell and free
  // exploration's choice ask it before they fade out or write a trigger. An empty filter lets
  // every one through. A script's own map change (startScriptedMapChange) is its caller's to ask.
  void setExitFilter(ExitFilter filter) { exitFilter_ = std::move(filter); }
  // The last map change the filter kept closed, once; the player stayed.
  [[nodiscard]] std::optional<MapChange> takeClosedExit() {
    return std::exchange(closedExit_, std::nullopt);
  }

  // The id of the current map; empty before the first enterMap.
  [[nodiscard]] const std::string& mapId() const { return mapId_; }

  // Whether stepping on a cell with this trigger changes the map now: an exit of the map's warp
  // table with a destination (cells 110-119), or a step cell (51-79) whose script map change is
  // decided taken. Click-to-move and the autotest's walks keep off these cells unless one is where
  // they go.
  [[nodiscard]] bool warpsOn(std::int32_t trigger) const;

  // Whether a walk must keep off the cells with this trigger: those that warp (warpsOn), except the
  // ones of the trigger the player stands on, which fire nothing until stepped onto again (after
  // staying in free exploration's chooser, or on a spawn on a warp).
  [[nodiscard]] bool keepsOff(std::int32_t trigger) const {
    return trigger != lastTrigger_ && warpsOn(trigger);
  }

  // Decides the map's step cells again after what their guards read changed: the field does it
  // itself when a warp it takes writes triggers; whoever else writes them (a load, the console)
  // calls it.
  void refreshScriptWarps();

  // A map editor's reload (dwe-game's reload_map): the field's rules and the scripts' map changes
  // read again from the packs replace the ones the field was built with. Map changes, fades and
  // the step cells use them from the next map entered; what the field built from the rules once
  // (the NPCs' movers, the partner's follow distance) keeps the old ones until a new game. A
  // choice of free exploration waiting is dropped, since it points at the old map changes.
  void reloadData(FieldRules rules, std::vector<ScriptWarp> scriptWarps);

  // Free exploration (ScriptWarpSetup::explore), which the player can turn on and off while
  // playing; the map's cells are decided again at once.
  void setExplore(bool explore);
  [[nodiscard]] bool explores() const { return explore_; }

  // Whether the story runs the sections of this map's cells (it set up the map's script): then
  // field/script_warps only serves free exploration. Off, the static graph stands in for the
  // story.
  void setStoryCells(bool storyCells);
  [[nodiscard]] bool storyCells() const { return storyCells_; }

  // Free exploration's choice waiting for the player; nothing moves while it waits.
  [[nodiscard]] const std::optional<DestinationChoice>& destinationChoice() const {
    return choice_;
  }

  // Answers the choice: a place of it is taken (with the triggers its warp sets), nothing stays on
  // the cell. Without a choice, or with a place it does not have, nothing happens.
  void chooseDestination(std::optional<std::size_t> place);

  // The current map's collision grid, for the minimap.
  [[nodiscard]] const assets::MapCollision& collision() const { return collision_; }
  // Makes a rectangle of the current map's cells stop movement until a map is entered again, as a
  // script closes an area (map_collision.c: setImpassableRect); cells outside the grid are left
  // alone.
  void blockCells(assets::CollisionCell from, std::int32_t width, std::int32_t height);

  // The current map's region lists, which say how the partner feels about the map.
  [[nodiscard]] const assets::MapRegions& regions() const { return regions_; }

  [[nodiscard]] CharacterState player() const;
  [[nodiscard]] CharacterState partner() const;
  // The player's way back on this map (map.c: TAMER_WAYPOINT_*, TAMER_START_TILE).
  [[nodiscard]] const PlayerTrail& playerTrail() const { return playerTrail_; }
  [[nodiscard]] PlayerTrail& playerTrail() { return playerTrail_; }
  // The partner's clips by the original's animation ids, with the root motion it walks by
  // (anim.c: applyRootMomentum). Without them it walks and runs at its rules' speeds.
  void setPartnerClips(assets::ClipMotions clips);
  // What the partner's state says about how it stands, walks and gestures (partner.c).
  void setPartnerMood(const PartnerMood& mood) { partnerMood_ = mood; }
  // The original's randomLimit for the partner's wait before its gesture; without one it waits
  // one play of its idle clip.
  void setPartnerRandom(std::function<std::int32_t(std::int32_t)> randomBelow) {
    partnerRandom_ = std::move(randomBelow);
  }
  // Whether a ground point is out of the camera's view, with the original's margin around the
  // screen (map.c: isTileOffScreen). With it a partner left too far and out of view comes back
  // from the screen's edge, and one still in view walks on; without it (no camera) it is placed
  // behind the player.
  void setOffScreenTest(std::function<bool(glm::vec3)> offScreen) {
    offScreen_ = std::move(offScreen);
  }
  // The clip the partner plays while it follows the player, or while a script holds it (its idle
  // clip 0 from the hold's start, partner.c: partnerTickIdle), by the original's id; nothing while
  // something else moves it (a care scene, making room).
  [[nodiscard]] std::optional<PartnerClip> partnerFollowClip() const;
  // Starts the partner's `clip` on its frame `frame` where it stands, with `fraction` of a unit
  // carried (the original's axes, 1/32768ths): where a trace of the original catches it in the
  // middle of a clip (the console's place command).
  void playPartnerFrom(std::int32_t clip, std::size_t frame, OriginalPoint fraction);

  // Whether the partner stands beside the player, closer than its walking distance (partner.c:
  // getPartnerTamerCloseness returns 2), as it comes to the player to sleep or to be praised.
  [[nodiscard]] bool partnerBeside() const;
  // The player turns to the partner at once (map.c: entityLookAtLocation); the partner already
  // looks at the player while it waits.
  void playerFacePartner();
  [[nodiscard]] scene::Entity playerEntity() const { return player_; }
  [[nodiscard]] scene::Entity partnerEntity() const { return partner_; }

  // A script holds the player and the partner (script_interp.c: scriptPauseGame, overworld.c:
  // setMovementEnabled): no input, no following, no triggers and no game time, and the camera stays
  // where it is. The script moves them and picks their clips (ScriptedActor). Letting go gives
  // them, the camera and the clock back (script_interp.c: tickScript).
  void holdForScript(bool held);
  [[nodiscard]] bool heldByScript() const { return scriptHeld_; }
  // A care scene holds the player while the game time runs (partner.c: partnerTickFeedItem and
  // the other partner states that set tamer state 6): no input, no triggers and no following; the
  // camera stays on the player unless panCameraByScript moves it, and comes back on release. The
  // partner stands unless walkPartnerToRange or walkPartnerTo moves it. A sleep holds them with the
  // game time stopped (partner.c: partnerTickSleep, stopGameTime), which also slides the HUD off.
  void holdForCare(bool held, CareClock clock = CareClock::Runs);
  [[nodiscard]] bool heldForCare() const { return careHeld_; }
  // While held for care: the partner walks until its ground distance to the player crosses
  // `range`, toward the player from farther and away from it from closer, while the player looks
  // at it (partner.c: partnerTickFeedItem sub states 0 to 2, checkEatDistance).
  void walkPartnerToRange(float range);
  // While held for care: the partner goes to `point` with `motion` (Walk or Run), the player
  // looking at it (partner.c: tickPartnerToilet, tickEntityWalkTo).
  void walkPartnerTo(glm::vec3 point, scene::Motion motion);
  // While held for care: the partner runs up to the player until it is within its running
  // distance (partner.c: tickWildPoop sub state 1, getPartnerTamerCloseness > 0); it stays where it
  // is if it already is.
  void runPartnerUp();
  // Whether either walk goes on; once a walk to a range is over, the two face each other.
  [[nodiscard]] bool partnerWalking() const { return careWalk_.has_value(); }
  // The partner and the player turn to each other at once (partner.c: entityLookAtLocation on
  // both).
  void faceEachOther();

  // A held character's scripted move, place, facing and clip. A place farther than half a cell
  // and a turn of more than an eighth are a jump, drawn at once; nearer ones are drawn as a move
  // over the tick.
  void driveByScript(scene::Entity entity, glm::vec3 velocity,
                     ScriptGlide glide = ScriptGlide::ReplacesClipMotion);
  void placeByScript(scene::Entity entity, glm::vec3 position);
  void turnByScript(scene::Entity entity, float yaw);
  // The clip also carries the character by its root motion (moveByClip), as anim.c's
  // tickAnimation runs on the player, the partner and every NPC each frame.
  void animateByScript(scene::Entity entity, std::uint32_t clip);
  // The held character's motion picks its clip again, as before any animateByScript.
  void releaseClipByScript(scene::Entity entity);
  // `clip` carries the character by its root motion from its first frame, one step each frame of
  // the original, along where it faces and with no collision test (anim.c: startAnimation sets
  // animFlag 1, tickAnimation, applyRootMomentum), until it ends or a script glides the character
  // (driveByScript); only holdClipInPlace stops its steps. The character needs
  // CharacterClipMotions.
  void moveByClip(scene::Entity entity, std::optional<std::uint32_t> clip);
  // The clip the character plays keeps its frames but stops (or starts again) carrying it (anim.c:
  // applyRootMomentum skips the step while animFlag 2 is set). A new clip carries it again, as
  // startAnimation resets animFlag.
  void holdClipInPlace(scene::Entity entity, bool held);
  // Where the camera looks while a script holds it; nothing: at the player.
  [[nodiscard]] std::optional<glm::vec3> scriptCamera() const { return scriptCamera_; }
  // Pans the held camera to `focus` in `seconds` (map.c: tickCameraMovement).
  void panCameraByScript(glm::vec3 focus, float seconds);
  [[nodiscard]] bool cameraPanning() const { return cameraPan_.has_value(); }
  // Moves the held camera by `by` (map.c: moveCameraByDiff).
  void shiftCameraByScript(glm::vec3 by);

  // Places the map's 3D objects from its door set, or none (map.c: loadMap calls loadDoors). The
  // slots keep the set's order, empty where it has no model, as scripts name them by slot.
  void placeMapObjects(const assets::MapDoorSet* doors);
  [[nodiscard]] std::span<const std::optional<MapObject3D>> mapObjects() const {
    return mapObjects_;
  }
  // Ticks one frame of a script's turn of the map object in `slot` toward `direction`; true when
  // it is done (map_object.c: tickRotateDoor).
  [[nodiscard]] bool turnMapObjectByScript(std::int64_t slot, std::int64_t direction);

  // The ground waypoints left of the click-to-move path, for debug drawing.
  [[nodiscard]] std::span<const glm::vec3> path() const { return path_; }

  // Whether a click-to-move target is being followed.
  [[nodiscard]] bool hasMoveTarget() const { return moveTarget_.has_value(); }
  // Drops the click-to-move target and its path: something took the player's control (a talk, a
  // menu, a gym course, a battle, a scene, a map change), and the player must not walk on to it
  // once it lets go. The field drops it by itself on every tick it holds the player.
  void dropMoveTarget();
  // Whether the last tick let the player move freely (no script, care scene, map change, choice
  // or fade holding him), when a click on the ground may become a walk order.
  [[nodiscard]] bool playerFree() const { return freeTick_; }

  // The ground direction away from the camera, from the map's camera: where "up" on the stick goes.
  [[nodiscard]] glm::vec3 cameraForward() const { return cameraForward_; }

  // How many times the partner was placed next to the player because it was stuck or too far.
  [[nodiscard]] std::int32_t partnerTeleports() const { return partnerTeleports_; }

  // The rules in use.
  [[nodiscard]] const FieldRules& rules() const { return rules_; }

  // Something that held the characters let them go: a script, a care scene, a battle, a gym
  // course, a map entry or load (docs/modulos/game/field.md, "Posiciones", the engine's). The
  // partner's trail and stuck time and the player's last cell start over, and on the next tick the
  // field runs unheld, separateActors puts a partner standing in someone or in a wall, or a player
  // in a wall, on the nearest free spot; NPCs stay where they are. After a script, it waits for the
  // partner to make room on foot first (makeRoomForPlayer). An NPC put on the map or let go asks
  // for the same check by itself.
  void releaseHandoff(std::string_view why);
  // A battle's frames (battle_main.c: BTL_tickPartnerWaypointTrail): the fighting partner's cells
  // out of the player's sight go on its trail, which the release after the battle keeps.
  void startBattleTrail(glm::vec3 partner);
  void tickBattleTrail(glm::vec3 partner);
  // Puts the player, the partner or an NPC at a place and facing at once, drawn there with no move
  // in between (the console's place command, which starts a run where a trace of the original
  // starts). It lets go as above, but nothing moves them off it: no release check and no making
  // room, as the original's characters stand where its trace starts.
  void placeAt(scene::Entity entity, glm::vec3 position, float yaw);
  // The release rule now, the engine's (the original has none): the partner in a wall cell or deep
  // in another's collision square (overlaps), or the player in a wall cell, goes to the nearest
  // spot where it fits on the grid, touches nobody, reaches from where it stood and warps nothing.
  // NPCs never move: the original keeps them where the map, their path or a script put them, even
  // on a wall cell (a bed, behind a counter). How many it moved.
  std::int32_t separateActors(std::string_view why);
  // Whether two characters' squares overlap deep enough to stand in each other: by more than half
  // their summed half sides on both axes. The original's squares are wide, and a partner stopped
  // by the player on a diagonal overlaps it by a corner (partner.c:971 measures a circle).
  [[nodiscard]] static bool overlaps(const GroundSquare& a, const GroundSquare& b);
  // Starts a tick of every character's transform without moving it. The game calls it at the
  // start of every tick; when the field's own tick follows, a frame's glide goes on. On the ticks
  // the field does not run (a battle, the evolution, a sleep, the fishing, a menu, an item's
  // notice) what they draw is where they stand, not a move from an older tick nor the rest of a
  // frame's glide, and the glides end.
  void settle();
  // The partner's collision size, its species' radius (the original's species table): it sizes
  // the grid collision, the NPCs' and the follow distances (partner.c:971), and asks for the
  // release check when it changes, as a bigger partner after an evolution may stand in someone.
  // Zero or less keeps the size.
  void setPartnerRadius(float radius);
  // How many times two characters (or one and a wall) stood in each other longer than a second
  // while the player moved freely; each such episode is logged once ("[positions] overlap").
  [[nodiscard]] std::int32_t overlapEpisodes() const { return overlapEpisodes_; }

  // The map's NPCs, which the story puts on the map and takes off (npcs.hpp).
  [[nodiscard]] FieldNpcs& npcs() { return npcs_; }
  [[nodiscard]] const FieldNpcs& npcs() const { return npcs_; }
  // The NPC touches and talks of the last tick, whose sections the story runs if no script runs.
  [[nodiscard]] std::span<const NpcContact> npcContacts() const { return npcs_.contacts(); }

private:
  struct Step {
    glm::vec2 direction{};
    bool run = false;
    // Follow the click-to-move path instead of a direction.
    bool followPath = false;
  };

  Step playerStep();
  [[nodiscard]] bool playerWantsToMove() const;
  void planPath(glm::vec3 target);
  // How many frames of the original start on this tick of `seconds` (frameClock_).
  std::int32_t takeFrames(float seconds);
  void movePlayer(std::int32_t frames);
  // One frame of tamer.c: tamerTickWalkingState and the animation's step.
  void stepPlayer(const Step& step);
  // The player's step along its facing (tamer.c: checkMapInteraction's entityCheckCollision,
  // map.c: collisionGrace, anim.c: applyRootMomentum).
  void walkForward(float distance);
  // Whether the player, a step ahead, would stand in an NPC or the partner (map.c:
  // entityCheckCollision as collisionGrace calls it).
  [[nodiscard]] bool playerTouchesSomeone(glm::vec3 from, glm::vec2 step) const;
  glm::vec2 followPath(glm::vec3& position, float distance);
  void checkTriggers();
  void checkConfirm();
  // tamer.c: checkMapInteraction's NPC part; whether an NPC's section was asked for.
  bool touchNpcs();
  void tickNpcs(float seconds, bool playerFree);
  void leave(MapChange change);
  // The fade in's last frame is drawn: FADE_PROGRESS keeps its shade.
  void endFadeIn();
  // Whether the exit filter lets the player take this map change; a closed one is kept for
  // closedExit and logged.
  bool exitOpen(const MapChange& change);
  // The map change the map's scripts make when `via` runs the section `value` and its guards are
  // decided taken; the warp's triggers are written.
  std::optional<MapChange> takeScriptWarp(WarpVia via, std::int32_t value);
  void writeTriggersOf(const ScriptWarp& warp);
  // takeScriptWarp under free exploration, given what the original's conditions take (`cell`).
  [[nodiscard]] bool staticWarps() const { return explore_ || !storyCells_; }
  std::optional<MapChange> exploreScriptWarp(WarpVia via, std::int32_t value, const CellWarp& cell);
  // The warp taken, as the map change it makes; logs what free exploration opened if the original
  // would not take it.
  std::optional<MapChange> takeExplored(const ScriptWarp& warp, const ScriptWarp* vanilla,
                                        std::int32_t value);
  void walkOut(float seconds);
  // The frame of the original a fade is on after `seconds`, counting the first as 1
  // (FADE_OUT_CURRENT, FADE_IN_CURRENT).
  [[nodiscard]] std::int32_t fadeFrame(float seconds) const;
  void movePartnerFrames(std::int32_t frames);
  void movePartner(float seconds);
  // The follow takes the partner where its transform is when something else moved it, and the
  // transform takes the follow's position and rotation after its frame.
  void syncPartnerFollow();
  void showPartnerFollow();
  // After a script lets the player go: a partner left against the player, in an NPC or hiding the
  // player from the camera steps off first; whether it took this tick.
  bool makeRoomForPlayer(float seconds);
  void placePartnerNearPlayer();
  // A partner too far and out of view: on the first cell out of the camera's view along the line
  // from the player to it (map.c: getClosestTileOffScreen); whether there was one to stand on,
  // nearer than it is.
  bool placePartnerOffScreen();
  // Puts the partner at `spot` if it fits there clear of the others; whether it did.
  bool placePartnerAt(const assets::SpawnPoint& spot);
  // A character's body for the release rule and the overlap watch.
  struct Body {
    scene::Entity entity{};
    float half = 0.0F;
    // 0 the player, 1 the partner, an NPC's script id + 2 otherwise.
    std::int64_t who = 0;
  };
  [[nodiscard]] std::vector<Body> bodies() const;
  [[nodiscard]] std::string nameOf(const Body& body) const;
  [[nodiscard]] GroundSquare squareOf(const Body& body) const;
  [[nodiscard]] bool inWall(glm::vec3 position) const;
  // Whether a character may stand at `at`: it fits on the grid, warps nothing and touches none of
  // `others` but `self`.
  [[nodiscard]] bool freeFor(const Body& self, glm::vec3 at, std::span<const Body> others) const;
  // The nearest free spot around `from`, rings outward; the ones toward `away` first.
  [[nodiscard]] std::optional<glm::vec3> freeSpotNear(const Body& self, glm::vec3 from,
                                                      glm::vec2 away, bool walled,
                                                      std::span<const Body> others) const;
  void requestSeparation(std::string_view why);
  void watchOverlaps(float seconds);
  void tickScripted(float seconds);
  // The characters a clip carries (moveByClip) take their frames' root steps (anim.c:
  // tickAnimation from tamerTickOverworld, partnerTickOverworld and NPCEntityTickOverworld).
  void applyClipRootMotion(float seconds);
  void tickCare(float seconds);
  void tickCareWalk(float seconds);
  void tickCameraPan(float seconds);
  // One step of the partner's care walk along `heading`. A walk to a point does not collide: in
  // partner state 6 and 9 (tickPartnerToilet, partnerTickEatShit) the clip's root motion carries
  // the partner, with no partnerTickCollision, so it runs into the toilet's hut.
  void stepCareWalk(glm::vec2 heading, float distance, scene::Motion motion, float seconds,
                    bool collides);

  scene::World* world_;
  FieldRules rules_;
  std::vector<ScriptWarp> scriptWarps_;
  GuardFacts facts_;
  TriggerWriter writeTrigger_;
  UnknownPolicy unknownPolicy_;
  bool explore_;
  bool storyCells_ = false;
  MapFilter hasMap_;
  std::optional<DestinationChoice> choice_;
  // The warp of each place of the choice, and the one the original's conditions take.
  std::vector<const ScriptWarp*> choiceWarps_;
  const ScriptWarp* choiceVanilla_ = nullptr;
  // The step cells of the current map whose map change is taken.
  std::vector<std::int32_t> warpingStepCells_;
  // The map and cell of each section whose map change could not be decided, logged once.
  std::vector<std::pair<std::string, std::int32_t>> undecidedReported_;
  // The script warps taken under the unknown policy, by index, logged once.
  std::vector<std::size_t> allowedReported_;
  // The map and cell of each section whose warps the field could not tell apart, logged once.
  std::vector<std::pair<std::string, std::int32_t>> ambiguousReported_;
  scene::Entity player_;
  scene::Entity partner_;
  FieldNpcs npcs_;

  // A character's move over the frame that last stepped it, for drawnPose: from where it was drawn
  // when the frame began to where the step left it, `seconds` after the frame's tick.
  struct FrameGlide {
    scene::Pose from;
    scene::Pose to;
    float seconds = 0.0F;
    bool on = false;
  };
  void beginTicks(float seconds);
  void beginTransforms();
  void startGlide(FrameGlide& glide, scene::Entity entity) const;
  void endGlide(FrameGlide& glide, scene::Entity entity) const;
  [[nodiscard]] std::optional<scene::Pose> glidePose(const FrameGlide& glide, scene::Entity entity,
                                                     float alpha) const;
  FrameGlide playerGlide_;
  FrameGlide partnerGlide_;
  // Whether the field's tick ran since the last settle: the glides go on only then.
  bool fieldRan_ = false;
  bool smoothMotion_ = true;
  // Seconds since the current frame of the original began, not counting this tick.
  float frameClock_ = 0.0F;
  float tickSeconds_ = 0.0F;
  // Whether the last tick ran the free field, and whether the player asked to move then.
  bool freeTick_ = false;
  bool playerPressed_ = false;
  // The player's clip as the last frame started it (anim.c: startAnimation).
  scene::Motion playerMotion_ = scene::Motion::Idle;
  // The partner's clip and speed as its last frame left them, for the ticks between frames.
  scene::AnimationIntent partnerIntent_;

  std::string mapId_;
  // A battle started its partner's trail: the next release keeps it.
  bool battleTrail_ = false;
  assets::MapCollision collision_;
  assets::MapRegions regions_;
  std::vector<assets::MapWarp> warps_;
  glm::vec3 cameraForward_{0.0F, 0.0F, -1.0F};
  // The ground direction the map's camera looks, unrounded.
  glm::vec2 cameraView_{0.0F, -1.0F};

  FieldInput input_;
  std::optional<glm::vec3> moveTarget_;
  std::vector<glm::vec3> path_;
  float pathStallSeconds_ = 0.0F;

  std::int32_t lastTrigger_ = 0;
  // The warp being faded out to, and for how long; then the change waiting to be taken.
  std::optional<MapChange> leaving_;
  float leavingSeconds_ = 0.0F;
  // A walked warp walks on and always shows the name; a script's map change does neither, unless
  // it asks for the name.
  bool leavingWalks_ = true;
  bool leavingShowsName_ = true;
  // Whether the fade in holds the player: after a warp it does (tamer.c: tamerTickChangeMap); after
  // a script's map change only the script's hold does (map.c: scriptTickChangeMap).
  bool fadeInHolds_ = true;
  // A script's map change asked for while the app loaded a map: it starts once that map is in.
  std::optional<std::pair<MapChange, bool>> queuedChange_;
  // The cell the player stood on at the last check, for the port log.
  std::int32_t checkedCell_ = 0;
  std::optional<MapChange> pendingChange_;
  bool waitingForMap_ = false;
  // The map being changed to, from the warp until it is entered.
  std::string changeTarget_;
  // How long the fade in after entering a map has run; nothing once it ends.
  std::optional<float> fadeInSeconds_;
  // How many frames that fade in lasts (fadeFromBlack's length).
  std::int32_t fadeInFrames_ = 20;
  // The shade the last fade in ended on (fade.c: FADE_PROGRESS), where a fade out starts when none
  // runs; nothing before any, for what a whole warp's fade in leaves.
  std::optional<std::int32_t> fadeProgress_;
  // The shade the running fade out started from.
  std::optional<std::int32_t> leavingFrom_;
  // Whether the screen keeps fadeProgress_ while no fade of the field runs (holdShade).
  bool shadeHeld_ = false;

  bool gameTimeRan_ = false;
  std::vector<TriggerStep> triggerSteps_;
  ExitFilter exitFilter_;
  std::optional<MapChange> closedExit_;
  // Whether the cell being checked now asked for a closed map change: then it asks for nothing
  // else.
  bool exitClosedNow_ = false;
  std::vector<std::int32_t> cellSections_;

  assets::CollisionCell lastPlayerCell_;
  PlayerTrail playerTrail_;
  // The partner following the player in the original's integers, and the pose it last gave the
  // partner's transform.
  PartnerFollow follow_;
  std::optional<scene::Pose> followPose_;
  // Whether the follow moved the partner on its last frame.
  bool partnerFollowing_ = false;
  PartnerMood partnerMood_;
  std::function<std::int32_t(std::int32_t)> partnerRandom_;
  std::function<bool(glm::vec3)> offScreen_;
  float partnerStuckSeconds_ = 0.0F;
  std::int32_t partnerTeleports_ = 0;
  // Set when a script lets the player go, until the partner has made room (makeRoomForPlayer).
  bool partnerMakesRoom_ = false;

  struct CameraPan {
    glm::vec3 from{};
    glm::vec3 to{};
    float seconds = 0.0F;
    float elapsed = 0.0F;
  };
  bool scriptHeld_ = false;
  // A care scene's walk of the partner: to `point`, or to `range` of the player, `away` from it
  // when closer.
  struct CareWalk {
    float range = 0.0F;
    bool away = false;
    std::optional<glm::vec3> point;
    scene::Motion motion = scene::Motion::Walk;
  };
  bool careHeld_ = false;
  CareClock careClock_ = CareClock::Runs;
  std::optional<CareWalk> careWalk_;
  // Seconds the partner's clip of its care walks has played, kept across walks on with it.
  float careClipSeconds_ = 0.0F;
  std::optional<glm::vec3> scriptCamera_;
  std::optional<CameraPan> cameraPan_;
  std::vector<std::optional<MapObject3D>> mapObjects_;

  // The release check waiting for the next unheld tick, and why.
  std::optional<std::string> separationDue_;
  // The pairs standing in each other now (a wall is entt::null), for how long.
  struct OverlapWatch {
    scene::Entity a{};
    scene::Entity b{};
    std::string names;
    std::int32_t ticks = 0;
    float seconds = 0.0F;
    bool reported = false;
  };
  std::vector<OverlapWatch> overlapWatch_;
  std::int32_t overlapEpisodes_ = 0;
};

} // namespace dwe::game::field
