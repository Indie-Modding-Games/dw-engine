#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/scene/components.hpp>
#include <dwe/scene/world.hpp>

#include <cstdint>
#include <deque>
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

  // Enters a map at a given place, without a fade in, for tests and debug tools.
  void enterMapAt(const assets::MapDocument& map, assets::SpawnPoint spawn);

  // Sets the intent for the next ticks.
  void setInput(const FieldInput& input);

  // Advances the logic by `seconds` (the app's fixed tick). On a warp the player walks on for the
  // fade out, without collision and without input; then nothing moves until the app enters the new
  // map and the fade in ends.
  void tick(float seconds);

  // The warp the player stepped on, once the fade out has ended: after taking it the app loads the
  // map and calls enterMap.
  std::optional<MapChange> takeMapChange();

  // A script's map change (map.c: scriptTickChangeMap): the same fades as a warp, with the player
  // standing, and the new map's name only if `showName`. The app takes it as any other.
  void startScriptedMapChange(MapChange change, bool showName);

  // The script's hold on the player (script_interp.c: scriptPauseGame; tickScript gives it back):
  // while held, the player and the partner stand, game time stops and no cell asks for its section;
  // map changes still fade and enter, and the NPCs go on.
  void holdForScript(bool hold);
  [[nodiscard]] bool heldForScript() const { return held_; }

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

  // The current map's region lists, which say how the partner feels about the map.
  [[nodiscard]] const assets::MapRegions& regions() const { return regions_; }

  [[nodiscard]] CharacterState player() const;
  [[nodiscard]] CharacterState partner() const;
  [[nodiscard]] scene::Entity playerEntity() const { return player_; }
  [[nodiscard]] scene::Entity partnerEntity() const { return partner_; }

  // The ground waypoints left of the click-to-move path, for debug drawing.
  [[nodiscard]] std::span<const glm::vec3> path() const { return path_; }

  // Whether a click-to-move target is being followed.
  [[nodiscard]] bool hasMoveTarget() const { return moveTarget_.has_value(); }

  // The ground direction away from the camera, from the map's camera: where "up" on the stick goes.
  [[nodiscard]] glm::vec3 cameraForward() const { return cameraForward_; }

  // How many times the partner was placed next to the player because it was stuck or too far.
  [[nodiscard]] std::int32_t partnerTeleports() const { return partnerTeleports_; }

  // The rules in use.
  [[nodiscard]] const FieldRules& rules() const { return rules_; }

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
  void planPath(glm::vec3 target);
  void movePlayer(float seconds);
  glm::vec2 followPath(glm::vec3& position, float distance);
  void checkTriggers();
  void checkConfirm();
  // tamer.c: checkMapInteraction's NPC part; whether an NPC's section was asked for.
  bool touchNpcs();
  void tickNpcs(float seconds, bool playerFree);
  void leave(MapChange change);
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
  void updateTrail();
  void movePartner(float seconds);
  void placePartnerNearPlayer();

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

  std::string mapId_;
  assets::MapCollision collision_;
  assets::MapRegions regions_;
  std::vector<assets::MapWarp> warps_;
  glm::vec3 cameraForward_{0.0F, 0.0F, -1.0F};

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
  bool held_ = false;
  // The cell the player stood on at the last check, for the port log.
  std::int32_t checkedCell_ = 0;
  std::optional<MapChange> pendingChange_;
  bool waitingForMap_ = false;
  // The map being changed to, from the warp until it is entered.
  std::string changeTarget_;
  // How long the fade in after entering a map has run; nothing once it ends.
  std::optional<float> fadeInSeconds_;

  bool gameTimeRan_ = false;
  std::vector<TriggerStep> triggerSteps_;
  std::vector<std::int32_t> cellSections_;

  assets::CollisionCell lastPlayerCell_;
  std::deque<assets::CollisionCell> trail_;
  scene::Motion partnerMotion_ = scene::Motion::Idle;
  float partnerStuckSeconds_ = 0.0F;
  std::int32_t partnerTeleports_ = 0;
};

} // namespace dwe::game::field
