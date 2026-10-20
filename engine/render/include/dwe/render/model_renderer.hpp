#pragma once

#include <dwe/assets/model_data.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/render/camera.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/render/slot_map.hpp>
#include <dwe/rhi/buffer.hpp>
#include <dwe/rhi/pipeline.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace dwe::render {

// A light that shines the same way everywhere: the direction it travels, in world space, and its color.
struct DirectionalLight {
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    glm::vec3 color{0.0F};
};

// How models are lit, as the original lit them: an ambient color plus three directional lights (a map's lights).
// A sum of 1 leaves a texel as it is; more brightens it, up to twice.
struct ModelLighting {
    std::array<DirectionalLight, 3> lights{};
    glm::vec3 ambient{1.0F};
};

// A neutral light for tools and viewers: soft ambient and a key light from above and in front.
ModelLighting defaultModelLighting();

// A model added to a ModelRenderer: its slot and the slot's generation, so a handle kept after remove() never reaches
// a model added later in the same slot.
using ModelHandle = SlotKey;

// One model drawn once this frame: where (world transform) and in what pose (one matrix per joint of its skin, as
// animation::computeJointMatrices gives them; the model's rest pose when empty).
struct ModelInstance {
    ModelHandle model;
    glm::mat4 world{1.0F};
    std::span<const glm::mat4> joints;
};

// How a material is blended, from its alpha mode and the original's semi-transparency equation.
struct MaterialBlend {
    rhi::BlendMode mode = rhi::BlendMode::Opaque;
    // The alpha the equation uses: 0.5 makes Alpha the original's average, 0.25 makes Additive B + F/4.
    float alpha = 1.0F;
    // Semi-transparent materials draw after the opaque ones, back to front, without writing depth.
    bool translucent = false;
};

MaterialBlend materialBlend(const assets::ModelMaterial& material);

// Draws models in the scene pass: uploads each model once (vertices and indices in one buffer, textures in the
// bindless table) and draws the instances submitted for the frame with the camera and the lighting, skinned on the GPU
// (ADR 0021).
//
// It draws through two drawers, both for FrameDesc::scene: opaque() draws the opaque and alpha-masked primitives, and
// translucent() the semi-transparent ones, sorted back to front per instance by its origin's view-space depth, without
// writing depth. A game puts other scene drawers between them (background, opaque models, map layers, translucent
// models): the translucent parts must blend over everything opaque. opaque() must come first: its prepare() takes the
// instances submitted so far and uploads the frame's joints, and translucent() draws from what it prepared.
class ModelRenderer {
public:
    // The renderer must outlive the model renderer.
    static core::Result<std::unique_ptr<ModelRenderer>> create(Renderer& renderer);
    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;
    ModelRenderer(ModelRenderer&&) = delete;
    ModelRenderer& operator=(ModelRenderer&&) = delete;
    ~ModelRenderer();

    // Uploads a model; the data may be dropped afterwards. For load time: the vertices and indices go up in one
    // upload that waits for the GPU, while the textures are queued (Renderer::queueTexture), so call it between frames
    // for the model to draw in the next one.
    core::Result<ModelHandle> add(const assets::ModelData& model);
    // Frees a model and its textures. Its handle, and every copy of it, is rejected from then on.
    void remove(ModelHandle model);
    // How many joint matrices an instance of the model takes (0 for a handle that is not live).
    [[nodiscard]] std::size_t jointCount(ModelHandle model) const;

    void setCamera(const Camera& camera) { camera_ = camera; }
    void setLighting(const ModelLighting& lighting) { lighting_ = lighting; }
    // Draws the instance in the next frame; false (and nothing drawn) for a handle that is not live. The joints are
    // copied.
    bool submit(const ModelInstance& instance);
    // Forgets the instances submitted so far. The next frame takes them (opaque().prepare()), but a frame that is not
    // drawn (a minimised window) takes nothing: call this before submitting each frame's instances.
    void clear();

    // The drawers for FrameDesc::scene, opaque first.
    [[nodiscard]] Drawer& opaque() { return opaque_; }
    [[nodiscard]] Drawer& translucent() { return translucent_; }

private:
    struct Primitive {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        std::int32_t vertexOffset = 0;
        std::uint32_t material = 0;
    };
    struct Material {
        rhi::TextureId texture;
        std::uint32_t flags = 0;
        float alphaCutoff = 0.5F;
        glm::vec4 baseColor{1.0F};
        MaterialBlend blend;
        bool doubleSided = false;
    };
    struct GpuModel {
        // The vertices, then the indices from indexOffset.
        rhi::Buffer geometry;
        std::size_t indexOffset = 0;
        std::vector<Primitive> primitives;
        std::vector<Material> materials;
        std::vector<rhi::TextureId> textures;
        std::vector<glm::mat4> restJoints;
        bool hasTranslucent = false;
    };
    struct Submitted {
        ModelHandle model;
        glm::mat4 world;
        std::size_t firstJoint = 0;
        std::size_t jointCount = 0;
    };
    // An instance taken for the frame, with its joints in the upload ring.
    struct Prepared {
        ModelHandle model;
        float viewDepth = 0.0F;
        std::uint64_t joints = 0;
    };
    struct Translucent {
        float viewDepth = 0.0F;
        const GpuModel* model = nullptr;
        const Prepared* instance = nullptr;
        const Primitive* primitive = nullptr;
    };

    class OpaqueDrawer final : public Drawer {
    public:
        explicit OpaqueDrawer(ModelRenderer& owner) : owner_(&owner) {}
        void prepare(PrepareContext& context) override { owner_->prepare(context); }
        void draw(DrawContext& context) override { owner_->drawOpaque(context); }

    private:
        ModelRenderer* owner_;
    };
    class TranslucentDrawer final : public Drawer {
    public:
        explicit TranslucentDrawer(ModelRenderer& owner) : owner_(&owner) {}
        void draw(DrawContext& context) override { owner_->drawTranslucent(context); }

    private:
        ModelRenderer* owner_;
    };

    explicit ModelRenderer(Renderer& renderer) : renderer_(&renderer), opaque_(*this), translucent_(*this) {}
    [[nodiscard]] const rhi::GraphicsPipeline& pipeline(const Material& material) const;
    void prepare(PrepareContext& context);
    void drawOpaque(DrawContext& context);
    void drawTranslucent(DrawContext& context);
    void record(rhi::CommandList& commands, const GpuModel& model, const Prepared& instance, const Primitive& primitive,
                const rhi::GraphicsPipeline*& bound) const;

    Renderer* renderer_;
    OpaqueDrawer opaque_;
    TranslucentDrawer translucent_;
    // Opaque pipelines by cull (back, none), then translucent ones by blend mode and cull.
    std::vector<rhi::GraphicsPipeline> pipelines_;
    SlotMap<GpuModel> models_;
    Camera camera_;
    ModelLighting lighting_ = defaultModelLighting();
    // Submitted since the last prepare, with their joints already in world space.
    std::vector<Submitted> submitted_;
    std::vector<glm::mat4> joints_;
    // What the current frame draws: taken from submitted_ by prepare(). Kept with the scratch below to reuse their
    // memory from frame to frame.
    std::vector<Prepared> prepared_;
    std::vector<Translucent> translucentScratch_;
    std::uint64_t frameAddress_ = 0;
    // Instances dropped last frame because the upload ring was full: logged when it changes, not every frame.
    std::size_t droppedLast_ = 0;
};

} // namespace dwe::render
