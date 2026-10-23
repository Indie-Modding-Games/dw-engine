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
#include <dwe/render/camera.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/map_renderer.hpp>
#include <dwe/render/map_scene.hpp>
#include <dwe/render/model_renderer.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/scene/world.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

  // The fishing's hold on the player (FishingScreen): one of the original's clips in place of the
  // walk's, nothing to give the player back to the field.
  void setPlayerClip(std::optional<std::uint32_t> clip);
  // Whether the clip setPlayerClip gave still plays.
  [[nodiscard]] bool playerClipPlaying() const;
  // Whether that clip has reached the end of its first pass, one that repeats too (animFrame ==
  // frameCount); true without a clip.
  [[nodiscard]] bool playerClipPlayedThrough() const;
  // A point in the space of one of the player's model nodes (the original's object index), in
  // original axes and units, in the world; nothing if the node is no joint of the skin.
  [[nodiscard]] std::optional<glm::vec3> playerNodePoint(std::uint32_t node,
                                                         glm::vec3 offset) const;
  // Turns the player in place.
  void setPlayerYaw(float yaw);
  // More markers to draw with the click-to-move target's, until replaced (the fishing's bobber).
  void setExtraMarkers(std::vector<render::MapMarker> markers) {
    extraMarkers_ = std::move(markers);
  }
  // The items on the ground, drawn as their icons over their shadows (item.c: renderDroppedItem)
  // until replaced. The images load on first use; one that fails is logged and not drawn.
  void setGroundItems(std::vector<GroundItemSprite> items) { groundItems_ = std::move(items); }
  // Copies of a model of the packs drawn on the map at these world transforms until replaced (the
  // partner's poop piles: partner_impl.c: renderPoop). The model loads on first use; one that
  // fails is logged and not drawn.
  void setProps(const assets::AssetId& model, std::vector<glm::mat4> places);
  // The map's camera, while a map is shown.
  [[nodiscard]] const render::MapCamera* camera() const;

  // Prepares the next frame: advances the animations by frameSeconds (not while paused; a
  // time-of-day fade under way goes on), places the characters between the last two ticks (alpha
  // from core::FixedStep) and points the camera at the player, except while the player walks out of
  // the map.
  void update(double frameSeconds, float alpha, bool paused);

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
  // Free exploration (game::field::Field::setExplore) and the answer to its choice
  // (Field::chooseDestination).
  void setExplore(bool explore) { field_->setExplore(explore); }
  void chooseDestination(std::optional<std::size_t> place) { field_->chooseDestination(place); }
  // Decides the map's script warp cells again after what their guards read changed
  // (Field::refreshScriptWarps).
  void refreshScriptWarps() { field_->refreshScriptWarps(); }
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
    // The clip a script plays on it (game::field::ScriptedActor), while it plays one.
    std::optional<std::uint32_t> scriptedClip;
    // A clip that replaces the motion's (setPlayerClip).
    std::optional<std::uint32_t> heldClip;
  };

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
  // Plays the clip a script picked for the actor; false when it has none.
  bool animateScripted(Actor& actor, double seconds);
  void animatePartnerClip(double seconds);
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
  render::MapSceneData map_;
  std::string mapNameKey_;
  std::uint32_t mapSerial_ = 0;
  std::vector<render::MapMarker> extraMarkers_;
  std::vector<GroundItemSprite> groundItems_;
  // The billboard textures of the items' image by palette, and of the shadow (nullopt as the key),
  // or nothing for one that failed to load.
  std::map<std::optional<std::uint8_t>, std::optional<std::size_t>> groundItemTextures_;
  std::optional<MapChangeTiming> lastChange_;
  std::unique_ptr<Actor> playerActor_;
  std::unique_ptr<Actor> partnerActor_;
  // The field rules' partner mover, for a partner loaded again with another species' model.
  game::field::MoverRules partnerMover_;
  std::vector<std::unique_ptr<Actor>> npcActors_;
  // The NPCs whose model could not be loaded, logged once each.
  std::vector<scene::Entity> npcsNotShown_;
  // The models of the map's 3D objects, by door model id.
  std::map<std::int32_t, render::ModelHandle> mapObjectModels_;
  // setProps: the model asked for, its handle once loaded, and the copies.
  std::optional<assets::AssetId> propModelId_;
  std::optional<render::ModelHandle> propModel_;
  std::vector<glm::mat4> props_;
};

} // namespace dwe::app
