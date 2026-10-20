#pragma once

#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/character_model.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/map_renderer.hpp>
#include <dwe/render/map_scene.hpp>
#include <dwe/render/model_renderer.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/scene/world.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dwe::app {

struct FieldSceneDesc {
    // The map and exit to start at; field/warps' start when not given.
    std::optional<std::string> startMap;
    std::int32_t startExit = 0;
    // Start the clock at this time of day instead of the field rules' start hour.
    std::optional<render::TimeOfDay> time;
};

// How long the last map change took, in milliseconds: reading the map through the VFS, entering it in the field and
// handing it to the map renderer (its uploads happen in the next frame).
struct MapChangeTiming {
    std::string from;
    std::string to;
    double loadMilliseconds = 0.0;
    double totalMilliseconds = 0.0;
};

// The field as the player sees it (R2.1-R2.9): the map drawn by MapRenderer with its camera, the player and the
// partner drawn by ModelRenderer with their animations, and game::field moving them. The app feeds it intents once
// per logic tick and draws its drawers in this order: background, models (opaque), map depth layers, models
// (translucent). It knows nothing of input devices or of the UI: the app reads its state for those.
class FieldScene {
public:
    // Loads the field rules, the characters (models/tamer and models/partner) and the start map from the VFS, which
    // must outlive the scene, as must the renderer.
    static core::Result<std::unique_ptr<FieldScene>>
    create(render::Renderer& renderer, const assets::VirtualFileSystem& files, const FieldSceneDesc& desc);

    FieldScene(const FieldScene&) = delete;
    FieldScene& operator=(const FieldScene&) = delete;
    FieldScene(FieldScene&&) = delete;
    FieldScene& operator=(FieldScene&&) = delete;
    ~FieldScene();

    // One logic tick with this intent. A warp the player steps on loads its map at once, keeping the partner.
    void tick(const game::field::FieldInput& input, float seconds);

    // Prepares the next frame: advances the animations by frameSeconds (not while paused; a time-of-day fade under
    // way goes on), places the characters between the last two ticks (alpha from core::FixedStep) and points the
    // camera at the player, except while the player walks out of the map.
    void update(double frameSeconds, float alpha, bool paused);

    // The scene drawers for FrameDesc::scene, in order.
    [[nodiscard]] std::span<render::Drawer* const> drawers() { return drawers_; }

    // The ground point under a window pixel (drawable pixels, as MouseMoved gives), for click-to-move.
    [[nodiscard]] std::optional<glm::vec3> pickGround(glm::vec2 windowPixel) const;
    // Where a world point appears in the window, in drawable pixels; nothing if it is behind the camera.
    [[nodiscard]] std::optional<glm::vec2> worldToWindow(glm::vec3 world) const;

    [[nodiscard]] const game::field::Field& field() const { return *field_; }
    [[nodiscard]] const assets::MapDocument& map() const { return map_.document; }
    // The key of the current map's name among the texts (nameKeyOf), empty when it has none.
    [[nodiscard]] const std::string& mapNameKey() const { return mapNameKey_; }
    // The key of a map's name among the texts (localization::mapNameKey of the base pack's map table), empty when the
    // map has no name. The one place the app looks a map's name up.
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

    core::Result<std::unique_ptr<Actor>> loadActor(std::string_view characterId, scene::Entity entity,
                                                   const game::field::MoverRules& rules);
    core::Result<void> enterMap(const std::string& mapId, std::int32_t exit);
    void animate(Actor& actor, double seconds);
    void submit(const Actor& actor, float alpha);
    void followClock();
    void applyLighting();

    const assets::VirtualFileSystem* files_;
    std::unique_ptr<render::MapRenderer> mapRenderer_;
    std::unique_ptr<render::ModelRenderer> models_;
    CameraSync cameraSync_{*this};
    std::array<render::Drawer*, 5> drawers_{};

    scene::World world_;
    std::unique_ptr<game::field::Field> field_;
    std::optional<assets::MapTable> mapTable_;
    // The time of day shown, from the field's clock, and whether the current map ignores it.
    render::TimeOfDay time_ = render::TimeOfDay::Day;
    // The field rules' hours and light scales.
    render::TimeOfDaySchedule schedule_;
    std::int32_t clockHour_ = 0;
    bool fixedTime_ = false;
    // The map's own lights, and how long until the time of day's change reaches them: the original switches them
    // when the palette fade ends.
    render::ModelLighting mapLighting_;
    double lightingDelay_ = 0.0;
    // What the camera centres on; it stays put during a map change's walk-out.
    std::optional<glm::vec3> cameraFocus_;
    render::MapSceneData map_;
    std::string mapNameKey_;
    std::uint32_t mapSerial_ = 0;
    std::optional<MapChangeTiming> lastChange_;
    std::unique_ptr<Actor> playerActor_;
    std::unique_ptr<Actor> partnerActor_;
};

} // namespace dwe::app
