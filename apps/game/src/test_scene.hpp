#pragma once

#include <dwe/core/error.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/quad_batch.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/buffer.hpp>
#include <dwe/rhi/pipeline.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace dwe::app {

// A small scene that exercises the render foundation until the field scene exists: a spinning cube cut by a
// checkered floor (depth testing, vertex and index buffers, matrices through the upload ring), and in the UI pass a
// texture and a small "minimap" texture updated in place.
class TestScene {
public:
    // With textureChurn, it also makes and destroys a small texture every frame, to check texture lifetimes.
    static core::Result<std::unique_ptr<TestScene>> create(render::Renderer& renderer, bool textureChurn = false);

    TestScene(const TestScene&) = delete;
    TestScene& operator=(const TestScene&) = delete;
    TestScene(TestScene&&) = delete;
    TestScene& operator=(TestScene&&) = delete;
    ~TestScene();

    // Advances the animation to seconds since the start.
    void update(double seconds);

    [[nodiscard]] render::Drawer& sceneDrawer() { return meshes_; }
    [[nodiscard]] render::Drawer& uiDrawer() { return quads_; }

private:
    struct Meshes final : render::Drawer {
        std::optional<rhi::GraphicsPipeline> pipeline;
        std::optional<rhi::Buffer> vertices;
        std::optional<rhi::Buffer> indices;
        render::TextureId checker;
        float angle = 0.0F;

        void draw(render::DrawContext& context) override;
    };

    TestScene(render::Renderer& renderer, render::QuadBatch quads, bool textureChurn);

    void churnTexture();

    render::Renderer* renderer_;
    Meshes meshes_;
    render::QuadBatch quads_;
    render::TextureId minimap_;
    std::int32_t minimapDot_ = -1;
    bool textureChurn_;
    render::TextureId churned_;
    std::uint32_t churnCount_ = 0;
};

} // namespace dwe::app
