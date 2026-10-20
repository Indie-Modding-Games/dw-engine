#pragma once

#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/character_model.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/core/error.hpp>
#include <dwe/render/model_renderer.hpp>
#include <dwe/render/renderer.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace dwe::app {

// A development view (`dwe-game --model-viewer`): each character of the game pack (models/<name>.yaml) in a row,
// once per animation (idle, walk, run...), playing in place, under a slowly turning camera. It proves model loading,
// animation and skinning on real content before the field scene uses them.
class ModelViewer {
public:
    // Mounts the packs of `content` plus the game pack at `gamePack`, and loads these characters (asset ids such as
    // models/tamer).
    static core::Result<std::unique_ptr<ModelViewer>> create(render::Renderer& renderer,
                                                             const std::filesystem::path& content,
                                                             const std::filesystem::path& gamePack,
                                                             std::span<const std::string> characters);

    ModelViewer(const ModelViewer&) = delete;
    ModelViewer& operator=(const ModelViewer&) = delete;
    ModelViewer(ModelViewer&&) = delete;
    ModelViewer& operator=(ModelViewer&&) = delete;
    ~ModelViewer() = default;

    // Advances the animations and the camera by seconds and submits this frame's instances for a target this size.
    void update(double seconds, rhi::Extent extent);

    // The scene drawers, in this order: the opaque parts of the models, then the semi-transparent ones.
    [[nodiscard]] render::Drawer& opaqueDrawer() { return models_->opaque(); }
    [[nodiscard]] render::Drawer& translucentDrawer() { return models_->translucent(); }

private:
    struct Actor {
        std::size_t model = 0;
        glm::vec3 position{0.0F};
        std::string animation;
        std::unique_ptr<animation::AnimationPlayer> player;
    };

    explicit ModelViewer(std::unique_ptr<render::ModelRenderer> models) : models_(std::move(models)) {}

    std::unique_ptr<render::ModelRenderer> models_;
    std::vector<std::unique_ptr<assets::ModelData>> data_;
    std::vector<render::ModelHandle> handles_;
    std::vector<Actor> actors_;
    double time_ = 0.0;
};

} // namespace dwe::app
