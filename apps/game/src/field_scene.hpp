#pragma once

#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/character_model.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/partner/field_animation.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/render/camera.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/map_renderer.hpp>
#include <dwe/render/map_scene.hpp>
#include <dwe/render/model_renderer.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/scene/world.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fighter_figure.hpp"
#include "place_command.hpp"
#include "scene_effect.hpp"

namespace dwe::ui {
class Ui;
} // namespace dwe::ui

namespace dwe::app {

// An item lying on the field's ground (item.c: spawnItem, spawnDroppedItems): its world point and
// its icon, the cell of its number in the items' image with its palette (ItemEntry::index,
// iconPalette).
struct GroundItemSprite {
  glm::vec3 position{0.0F};
  std::int32_t icon = 0;
  std::uint8_t palette = 0;
};

struct FieldSceneDesc {
  // The map and exit to start at; field/warps' start when not given.
  std::optional<std::string> startMap;
  std::int32_t startExit = 0;
  // A place on startMap to start at instead of its exit, as a saved game keeps it.
  std::optional<assets::SpawnPoint> startAt;
  // Without startMap: the map, by its number in the original, and the exit a new game starts at
  // (map.c: initializeMap), instead of field/warps' start.
  std::optional<std::int32_t> startOriginalMap;
  std::int32_t startOriginalExit = 0;
  // The game clock, whose hour gives the time of day; it must outlive the scene.
  ruleset::GameClock* clock = nullptr;
  // Jump the clock to this time of day instead of keeping its hour.
  std::optional<render::TimeOfDay> time;
  // What the scripts' map changes read of the game, and where the triggers they set go
  // (session/field_facts.hpp). Without facts no script warp is taken.
  // NOLINTNEXTLINE(readability-redundant-member-init): the default lets designated initializers leave it out.
  game::field::GuardFacts facts = {};
  game::field::TriggerWriter writeTrigger = nullptr;
  // Start with free exploration on (game::field::ScriptWarpSetup::explore).
  bool explore = false;
};

// How long the last map change took, in milliseconds: reading the map through the VFS, entering it
// in the field and handing it to the map renderer (its uploads happen in the next frame).
struct MapChangeTiming {
  std::string from;
  std::string to;
  double loadMilliseconds = 0.0;
  double totalMilliseconds = 0.0;
};

// The field as the player sees it (R2.1-R2.9): the map drawn by MapRenderer with its camera, the
// player and the partner drawn by ModelRenderer with their animations, and game::field moving them.
// The app feeds it intents once per logic tick and draws its drawers in this order: background,
// models (opaque), map depth layers, models (translucent). It knows nothing of input devices or of
// the UI: the app reads its state for those.
// The key of a map's name among the texts (localization::mapNameKey of the map table, or
// addedMapNameKey for a map a pack adds), empty when the map has no name. The one place the app
// looks a map's name up.
[[nodiscard]] std::string mapNameKeyOf(const assets::MapTable& table, std::string_view mapId);

// What a scene of the original (the evolution, the death) does to the field's picture.
struct FieldStage {
  // setMapLayerEnabled(0) and *_setOtherEntitiesVisible(0) (evl.c, dooa.c, murd.c): the map and
  // the tamer are not drawn, only the partner on black.
  bool sceneryHidden = false;
  // The partner's isOnMap = 0.
  bool partnerHidden = false;
  // The partner's posData->scale, 1 being its model's size.
  glm::vec3 partnerScale{1.0F};
  // The camera the models are drawn with instead of the map's (the scene's GS_VIEWPOINT).
  std::optional<render::Camera> camera;
};

class FieldScene {
public:
  // Loads the field rules, the characters (models/tamer and models/partner) and the start map from
  // the VFS, which must outlive the scene, as must the renderer.
  static core::Result<std::unique_ptr<FieldScene>> create(render::Renderer& renderer,
                                                          const assets::VirtualFileSystem& files,
                                                          const FieldSceneDesc& desc);

  FieldScene(const FieldScene&) = delete;
  FieldScene& operator=(const FieldScene&) = delete;
  FieldScene(FieldScene&&) = delete;
  FieldScene& operator=(FieldScene&&) = delete;
  ~FieldScene();

  // One logic tick of the field with this intent. The owner of the game clock advances it next, by
  // the time the field played (Field::gameTimeRan), and then calls finishTick.
  void tick(const game::field::FieldInput& input, float seconds);
  // Ends the tick: follows the clock's time of day, and loads the map of a warp the player stepped
  // on, keeping the partner.
  void finishTick();
  // The player turns to the partner (Field::playerFacePartner).
  void playerFacePartner();
  // Starts a logic tick of every character on the field without moving it (Field::settle): each
  // tick begins with it, so a tick the field does not run draws them where they stand.
  void settleActors() { field_->settle(); }

  // The fishing's hold on the player (FishingScreen): one of the original's clips in place of the
  // walk's, nothing to give the player back to the field.
  void setPlayerClip(std::optional<std::uint32_t> clip);
  // A scene's clip of the player (by the original's id, startAnimation) held as setPlayerClip
  // holds it, but running while the field waits for the scene, as the partner's scene clip does;
  // nothing gives the player back to its motion.
  void playPlayerSceneClip(std::optional<std::uint32_t> clip);
  // Whether the clip setPlayerClip gave still plays.
  [[nodiscard]] bool playerClipPlaying() const;
  // Whether that clip has reached the end of its first pass, one that repeats too (animFrame ==
  // frameCount); true without a clip.
  [[nodiscard]] bool playerClipPlayedThrough() const;
  // A point in the space of one of the player's model nodes (the original's object index), in
  // original axes and units, in the world; nothing if the node is no joint of the skin.
  [[nodiscard]] std::optional<glm::vec3> playerNodePoint(std::uint32_t node,
                                                         glm::vec3 offset) const;
  // The same point for any character drawn on the field: the player, the partner or an NPC, by
  // its entity, with no offset (the node's origin); nothing for an entity not drawn or a node that
  // is no joint of its skin.
  [[nodiscard]] std::optional<glm::vec3> entityNodePoint(scene::Entity entity,
                                                         std::uint32_t node) const;
  // Turns the player in place.
  void setPlayerYaw(float yaw);
  // Moves everything the map draws by `offset` background pixels (render::MapCamera::setShake).
  void setScreenShake(glm::vec2 offset) { mapRenderer_->setShake(offset); }
  // More markers to draw with the click-to-move target's, until replaced (the fishing's bobber).
  void setExtraMarkers(std::vector<render::MapMarker> markers) {
    extraMarkers_ = std::move(markers);
  }
  // The items on the ground, drawn as their icons over their shadows (item.c: renderDroppedItem)
  // until replaced. The images load on first use; one that fails is logged and not drawn.
  void setGroundItems(std::vector<GroundItemSprite> items) { groundItems_ = std::move(items); }
  // Copies of a model of the packs drawn on the map at these world transforms until replaced for
  // that model (the partner's poop piles: partner_impl.c: renderPoop; the story's props:
  // field_story_props.hpp); no places draws none of it. A model loads the first time it has
  // places; one that fails is logged once and not drawn.
  void setProps(const assets::AssetId& model, std::vector<glm::mat4> places);
  // While held the camera stays where it points: map.c: tickCameraFollowPlayer follows the player
  // only in its state 0, never in a battle, where the player runs back on screen.
  void holdCamera(bool held) { cameraHeld_ = held; }
  // The map's camera, while a map is shown.
  [[nodiscard]] const render::MapCamera* camera() const;

  // Prepares the next frame: advances the animations by frameSeconds (not while paused; a
  // time-of-day fade under way goes on), places the characters between the last two ticks (alpha
  // from core::FixedStep) and points the camera at the player, except while the player walks out of
  // the map.
  void update(double frameSeconds, float alpha, bool paused);
  // The partner's conditions and happiness, which pick the clips it stands, walks and gestures
  // with (partner.c: setPartnerIdle, setPartnerSlowWalking, updateConditionAnimation).
  void setPartnerMood(const game::partner::Conditions& conditions, std::int32_t happiness);

  // The scene drawers for FrameDesc::scene, in order.
  [[nodiscard]] std::span<render::Drawer* const> drawers() {
    return stage_.sceneryHidden ? std::span<render::Drawer* const>{stageDrawers_}
                                : std::span<render::Drawer* const>{drawers_};
  }

  // The ground point under a window pixel (drawable pixels, as MouseMoved gives), for
  // click-to-move.
  [[nodiscard]] std::optional<glm::vec3> pickGround(glm::vec2 windowPixel) const;
  // Where a world point appears in the window, in drawable pixels; nothing if it is behind the
  // camera.
  [[nodiscard]] std::optional<glm::vec2> worldToWindow(glm::vec3 world) const;
  // The same point as fractions of the window's width and height from its top-left corner.
  [[nodiscard]] std::optional<glm::vec2> worldToWindowFraction(glm::vec3 world) const;

  // Draws the partner as this species (its model in the base pack, digimon/<species>/model) with
  // models/partner's animations, which are the same ids for every partner; nothing changes if it is
  // drawn so already. On a failure the partner keeps its model.
  core::Result<void> showPartnerSpecies(std::string_view species);
  // The radius of a species in the species table, 0 when unknown: it sizes the partner on the field
  // (showPartnerSpecies). Without it the field keeps the radius of field/rules.
  void attachSpeciesRadius(std::function<float(std::string_view)> radius) {
    speciesRadius_ = std::move(radius);
  }
  // The box of a species in the species table, which says whether its entity shows on screen.
  struct SpeciesBox {
    float radius = 0.0F;
    float height = 0.0F;
  };
  // Where the field's clips play their sounds (anim.c: tickAnimation, instruction 0x4000), such as
  // the steps of the walk and run clips: the player's always (its isOnScreen stays 1), the
  // partner's and the NPCs' while their species' box shows on screen (map.c:
  // entityIsOffScreen(entity, 320, 240)). Without it the clips play silent. The sound port must
  // outlive the scene.
  void attachSound(game::story::SoundPort* sound,
                   std::function<std::optional<SpeciesBox>(std::string_view)> box) {
    sound_ = sound;
    speciesBox_ = std::move(box);
  }
  // Enters the original's map with this index at this exit, keeping the partner (a scene's map
  // change: map.c: changeToDeathMap).
  core::Result<void> enterOriginalMap(std::int32_t index, std::int32_t exit);
  // What a scene does to the picture; a default FieldStage gives the field its own back.
  void setStage(const FieldStage& stage) { stage_ = stage; }
  [[nodiscard]] const FieldStage& stage() const { return stage_; }
  // Plays this clip of the partner (by the original's id, startAnimation) instead of the one its
  // motion picks, until nothing gives the partner back to its motion. A clip the model lacks is
  // logged and the partner keeps its motion.
  void playPartnerClip(std::optional<std::uint32_t> clipId);
  // Whether the partner's scene clip has reached the end of its first pass (animFrame ==
  // frameCount), one that repeats too.
  [[nodiscard]] bool partnerClipFinished() const;

  // The battle holds the partner (no script id) or the map's NPC with this script id: it stands,
  // faces and plays as the figure says, the field's motion (following, wandering, looking at the
  // player) set aside until releaseFighters. False when no such NPC is drawn.
  bool holdFighter(std::optional<std::int32_t> npc, const FighterFigure& figure);
  // Where a held fighter's feet show in the window, as fractions of its size (utils.c:
  // getEntityScreenPos(entity, 0): the root of its model, which stands where it stands); nothing
  // when the field does not draw it or it is behind the camera.
  [[nodiscard]] std::optional<glm::vec2> fighterFeetOnWindow(std::optional<std::int32_t> npc) const;
  // Where one node of a held fighter's model shows in the window, as fractions of its size
  // (utils.c: getEntityScreenPos(entity, node): the origin of that object as the pose leaves it);
  // nothing when the field does not draw the fighter, the node is no joint of its skin or it is
  // behind the camera.
  [[nodiscard]] std::optional<glm::vec2> fighterNodeOnWindow(std::optional<std::int32_t> npc,
                                                             std::uint32_t node) const;
  // The battle's time for the held fighters' clips, which run while the field waits.
  void runFighters(double seconds) { fighterSecondsDue_ += seconds; }
  // Whether the held fighter's clip, the figure's, has played through once (anim.c: animFlag 1
  // off); true when the field does not show it.
  [[nodiscard]] bool fighterPlayedThrough(std::optional<std::int32_t> npc,
                                          const FighterFigure& figure);
  // The model a held fighter is drawn with; null when the field does not draw it.
  [[nodiscard]] const assets::ModelData* fighterModel(std::optional<std::int32_t> npc) const;
  // The field takes the fighters back where they stand.
  void releaseFighters();
  // A held fighter's pose now, one local transform per node of fighterModel(npc); nothing when the
  // field does not draw it.
  [[nodiscard]] std::optional<std::span<const assets::NodeTransform>>
  fighterPose(std::optional<std::int32_t> npc) const;
  // What the techniques' effects drew on the battle's last frame, drawn until replaced: sprites
  // with the move's effect texture, objects of its effect model. Each texture and model loads on
  // first use; one that fails is logged and not drawn.
  void setEffects(std::vector<SceneEffect> effects) { effects_ = std::move(effects); }
  // The map's camera, which a scene's camera starts from.
  [[nodiscard]] const render::MapCamera& mapCamera() const { return mapRenderer_->camera(); }
  // The model the partner is drawn with.
  [[nodiscard]] const assets::AssetId& partnerModel() const {
    return partnerActor_->character.model;
  }

  [[nodiscard]] const game::field::Field& field() const { return *field_; }
  // The field's NPCs, for the story's NPC orders.
  [[nodiscard]] game::field::FieldNpcs& npcs() { return field_->npcs(); }
  // The scene's entities, for the story's stage to read where an NPC stands.
  [[nodiscard]] const scene::World& world() const { return world_; }
  // The story's side of the field (story_field_link.hpp): a script's map change, and whether the
  // story runs the map's cells (Field::startScriptedMapChange, Field::setStoryCells).
  void startScriptedMapChange(game::field::MapChange change, bool showName) {
    field_->startScriptedMapChange(std::move(change), showName);
  }
  void setStoryCells(bool storyCells) { field_->setStoryCells(storyCells); }
  // The map table the scene names its maps with, if the packs have one.
  [[nodiscard]] const assets::MapTable* mapTable() const {
    return mapTable_ ? &*mapTable_ : nullptr;
  }
  // The field the story's scripted moves drive (game::field::Field::holdForScript).
  [[nodiscard]] game::field::Field& field() { return *field_; }
  // The map's sprite objects, which scripts and the gym's courses move (overworld.c:
  // tickMoveObjectTo, moveMapObjectsWithLimit).
  [[nodiscard]] render::MapDepthLayers& mapLayers() { return mapRenderer_->layers(); }
  // How fast the clip with the original's animation id carries the player or the partner over the
  // ground, world units a second: its root's speed (anim.c: applyRootMomentum); 0 without it.
  [[nodiscard]] float clipGroundSpeed(scene::Entity entity, std::uint32_t clip) const;
  // Whether the clip a script last started on the player, the partner or a map NPC has played
  // through once (anim.c: animFrame reaching frameCount); true without such an actor.
  [[nodiscard]] bool clipPlayedThrough(scene::Entity entity) const;
  // Free exploration (game::field::Field::setExplore) and the answer to its choice
  // (Field::chooseDestination).
  void setExplore(bool explore) { field_->setExplore(explore); }
  void chooseDestination(std::optional<std::size_t> place) { field_->chooseDestination(place); }
  // Decides the map's script warp cells again after what their guards read changed
  // (Field::refreshScriptWarps).
  void refreshScriptWarps() { field_->refreshScriptWarps(); }
  // Puts a character at a ground point and facing at once, at the height it stands
  // (Field::placeAt); false when the map has no NPC with that script id.
  bool place(const Placement& placement);
  // The story's edits of the map shown, until a map is entered again (story_map_edits.cpp): an
  // object's flag (overworld.c: setMapObjectsFlag) and a rectangle of cells that stop movement
  // (map_collision.c: setImpassableRect).
  void setMapObjectFlag(std::int32_t object, std::int32_t flag);
  void blockCells(assets::CollisionCell from, std::int32_t width, std::int32_t height);
  [[nodiscard]] const assets::MapDocument& map() const { return map_.document; }
  // The key of the current map's name among the texts (nameKeyOf), empty when it has none.
  [[nodiscard]] const std::string& mapNameKey() const { return mapNameKey_; }
  // The key of a map's name among the texts (mapNameKeyOf with the scene's map table).
  [[nodiscard]] std::string nameKeyOf(std::string_view mapId) const;
  // Grows by one on every map entered, so the app notices a change (minimap, autotests).
  [[nodiscard]] std::uint32_t mapSerial() const { return mapSerial_; }
  [[nodiscard]] const std::optional<MapChangeTiming>& lastMapChange() const { return lastChange_; }

private:
  // A character drawn from the game pack's models/<name>.yaml.
  struct Actor {
    assets::CharacterModel character;
    std::unique_ptr<assets::ModelData> model;
    render::ModelHandle handle;
    std::unique_ptr<animation::AnimationPlayer> player;
    scene::Entity entity{};
    // The ground speeds of field/rules for walk and run, which the clips are played to match.
    float walkSpeed = 0.0F;
    float runSpeed = 0.0F;
    std::optional<scene::Motion> motion;
    // The clip the motion plays.
    std::optional<std::uint32_t> motionClip;
    // The clip a script plays on it (game::field::ScriptedActor), while it plays one.
    std::optional<std::uint32_t> scriptedClip;
    std::uint32_t scriptedStarts = 0;
    // A clip that replaces the motion's (setPlayerClip).
    std::optional<std::uint32_t> heldClip;
    // The battle's hold on it (holdFighter).
    std::optional<FighterFigure> fighter;
  };

  // A point in the space of one of an actor's model nodes (the original's object index), in
  // original axes and units, in the world; nothing if the node is no joint of the skin.
  [[nodiscard]] std::optional<glm::vec3> nodePoint(const Actor& actor, std::uint32_t node,
                                                   glm::vec3 offset) const;

  // Gives the model renderer the map camera once the background drawer has sized it to the frame.
  class CameraSync final : public render::Drawer {
  public:
    explicit CameraSync(FieldScene& owner) : owner_(&owner) {}
    void prepare(render::PrepareContext& context) override;
    void draw(render::DrawContext& /*context*/) override {}

  private:
    FieldScene* owner_;
  };

  explicit FieldScene(const assets::VirtualFileSystem& files);

  // What loading a character does with an animation whose clip the model lacks: the player and the
  // partner need them all; an NPC is drawn with the clips it has, as the original draws any
  // species on a map.
  enum class MissingClips : std::uint8_t { Refuse, Drop };

  // Loads a character; `otherModel` draws it with another model than its document's, with the same
  // animations.
  core::Result<std::unique_ptr<Actor>>
  loadActor(std::string_view characterId, scene::Entity entity,
            const game::field::MoverRules& rules,
            const std::optional<assets::AssetId>& otherModel = std::nullopt,
            MissingClips missingClips = MissingClips::Refuse);
  core::Result<void> enterMap(const std::string& mapId, std::int32_t exit,
                              std::optional<assets::SpawnPoint> at = std::nullopt);
  void animate(Actor& actor, double seconds);
  // Plays the sounds the actor's clip just ran through (attachSound).
  void playClipSounds(const Actor& actor);
  // map.c: entityIsOffScreen(entity, 320, 240) == 0: a corner of the box the actor's species makes
  // around it shows in the window.
  [[nodiscard]] bool onScreen(const Actor& actor) const;
  // Plays the clip a script picked for the actor; false when it has none.
  bool animateScripted(Actor& actor, double seconds);
  // Plays the clip the battle holds the actor in; false when it holds none
  // (field_scene_fighters.cpp).
  bool animateFighter(Actor& actor) const;
  [[nodiscard]] Actor* fighterActor(std::optional<std::int32_t> npc) const;
  // The player's, the partner's or a map NPC's actor, or none.
  [[nodiscard]] const Actor* actorOfEntity(scene::Entity entity) const;
  // field_scene_effects.cpp: the effects' textures (by move and palette) and models.
  struct EffectModelEntry {
    render::ModelHandle handle;
    std::size_t joints = 0;
  };
  std::optional<std::size_t> effectTexture(const std::string& move, std::int32_t palette);
  const EffectModelEntry* effectModel(const std::string& move);
  void addEffectBillboards(std::vector<render::MapBillboard>& billboards);
  void submitEffectModels();
  void animatePartnerClip(double seconds);
  // The partner's animation for its motion (partner.c: partnerTickWalking).
  // The partner while the field's follow moves it: the clip it picked; false when something
  // else moves it.
  bool animateFollowingPartner(Actor& actor, double seconds);
  // What the partner's conditions and happiness say to the field's follow.
  [[nodiscard]] game::field::PartnerMood partnerMood() const;
  game::partner::FieldAnimation partnerAnimation(const Actor& actor, scene::Motion motion,
                                                 double seconds);
  void submit(const Actor& actor, float alpha, glm::vec3 scale = glm::vec3{1.0F});
  // Places the map's 3D objects in the field and loads their models (map.c: loadMap, loadDoors).
  void placeMapObjects(const assets::MapEntry* entry);
  // Draws them (map_object.c: renderDoors).
  void submitMapObjects();
  void submitProps();
  void followClock();
  void applyLighting();
  // The map's NPCs drawn as their species with models/partner's animations (field_scene_npcs.cpp):
  // loaded when the story puts one on the map, dropped when it goes.
  void followNpcs();
  void drawNpcs(double animationSeconds, float alpha);
  // The ground items' billboards (field_scene_ground_items.cpp).
  void drawGroundItems();
  std::optional<std::size_t> groundItemTexture(std::optional<std::uint8_t> palette);

  const assets::VirtualFileSystem* files_;
  std::unique_ptr<render::MapRenderer> mapRenderer_;
  std::unique_ptr<render::ModelRenderer> models_;
  CameraSync cameraSync_{*this};
  std::array<render::Drawer*, 5> drawers_{};
  // The drawers while a scene hides the scenery: the models only.
  std::array<render::Drawer*, 3> stageDrawers_{};
  FieldStage stage_;
  // The partner's scene clip, and whether its actor plays it already (a new model's does not).
  std::optional<std::uint32_t> partnerClip_;
  bool partnerClipStarted_ = false;
  bool partnerClipPlays_ = false;
  // What the partner shows while something else than the field's follow moves it (a care walk,
  // making room; partner.c: partnerTickWalking): the motion it last had, the clip it walks or
  // stands with, the gesture under way, how long it has stood (STOP_DISTANCE_TIMER) and how many
  // plays of its idle clip it stands before the gesture (EMOTION_ANIM_TIMEOUT).
  struct PartnerLook {
    std::optional<scene::Motion> motion;
    game::partner::FieldAnimation base = game::partner::FieldAnimation::Idle;
    std::optional<game::partner::FieldAnimation> gesture;
    double standingSeconds = 0.0;
    std::int32_t idlePlays = 1;
  };
  PartnerLook partnerLook_;
  game::partner::Conditions partnerConditions_;
  std::int32_t partnerHappiness_ = 0;
  // The original rolls the wait with randomLimit; any sequence will do.
  ruleset::Random gestureRandom_{0};
  // The start count of the follow's clip the partner plays.
  std::uint32_t partnerFollowStarts_ = 0;
  // Whether the player's held clip is a scene's (playPlayerSceneClip), which runs while paused.
  bool playerSceneClip_ = false;

  scene::World world_;
  std::unique_ptr<game::field::Field> field_;
  std::optional<assets::MapTable> mapTable_;
  // The time of day shown, from the field's clock, and whether the current map ignores it.
  render::TimeOfDay time_ = render::TimeOfDay::Day;
  // The field rules' hours and light scales.
  render::TimeOfDaySchedule schedule_;
  const ruleset::GameClock* clock_ = nullptr;
  std::int32_t clockHour_ = 0;
  bool fixedTime_ = false;
  // The map's own lights, and how long until the time of day's change reaches them: the original
  // switches them when the palette fade ends.
  render::ModelLighting mapLighting_;
  double lightingDelay_ = 0.0;
  // What the camera centres on; it stays put during a map change's walk-out.
  std::optional<glm::vec3> cameraFocus_;
  bool cameraHeld_ = false;
  render::MapSceneData map_;
  std::string mapNameKey_;
  std::uint32_t mapSerial_ = 0;
  std::vector<render::MapMarker> extraMarkers_;
  std::vector<GroundItemSprite> groundItems_;
  // The billboard textures of the items' image by palette, and of the shadow (nullopt as the key),
  // or nothing for one that failed to load.
  std::map<std::optional<std::uint8_t>, std::optional<std::size_t>> groundItemTextures_;
  std::vector<SceneEffect> effects_;
  std::map<std::pair<std::string, std::int32_t>, std::optional<std::size_t>> effectTextures_;
  std::map<std::string, std::optional<EffectModelEntry>, std::less<>> effectModels_;
  std::optional<MapChangeTiming> lastChange_;
  std::unique_ptr<Actor> playerActor_;
  std::unique_ptr<Actor> partnerActor_;
  // The field rules' partner mover, for a partner loaded again with another species' model.
  game::field::MoverRules partnerMover_;
  std::function<float(std::string_view)> speciesRadius_;
  game::story::SoundPort* sound_ = nullptr;
  std::function<std::optional<SpeciesBox>(std::string_view)> speciesBox_;
  std::vector<std::unique_ptr<Actor>> npcActors_;
  // The NPCs whose model could not be loaded, logged once each.
  std::vector<scene::Entity> npcsNotShown_;
  // The battle time given to the held fighters since the last update, and in this update.
  double fighterSecondsDue_ = 0.0;
  double fighterSeconds_ = 0.0;
  // The models of the map's 3D objects, by door model id.
  std::map<std::int32_t, render::ModelHandle> mapObjectModels_;
  // setProps: each model asked for, its handle once loaded (none when it failed), and the copies.
  struct PropSet {
    std::optional<render::ModelHandle> model;
    std::vector<glm::mat4> places;
  };
  std::map<std::string, PropSet> props_;
};

// Where the player stands on the window, in percent (the settings field.player_x and
// field.player_y), for the original look's boxes that grow from the player (game_menu.c:
// createMenuBox and inventory.c: createInventoryView start from getEntityScreenPos(ENTITY_TABLE[0],
// 1), the player's second bone). Published as the game menu or the bag opens; the player does not
// move while either is open.
void publishPlayerPoint(ui::Ui& ui, const FieldScene& scene);

} // namespace dwe::app
