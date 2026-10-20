#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/bindless.hpp>
#include <dwe/rhi/pipeline.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace dwe::render {

// A textured, tinted rectangle in pixels from the top left of the target.
struct Quad {
    glm::vec2 position{0.0F};
    glm::vec2 size{0.0F};
    glm::vec2 uvMin{0.0F};
    glm::vec2 uvMax{1.0F};
    // Multiplies the texture's color.
    glm::vec4 color{1.0F};
    TextureId texture{};
    rhi::SamplerKind sampler = rhi::SamplerKind::LinearClamp;
};

// One vertex of a QuadBatch, as the quad shader reads it.
struct QuadVertex {
    glm::vec2 position;
    glm::vec2 uv;
    glm::vec4 color;
    // Texture index and sampler.
    glm::uvec2 texture;
};

// Appends the two triangles (six vertices) of quad to vertices.
void appendQuad(const Quad& quad, std::vector<QuadVertex>& vertices);

// Screen-space textured quads, drawn in one call in the order they were added: sprites, a minimap, debug images. It
// keeps its quads until clear(), and draws in either pass (in the scene pass without depth testing).
class QuadBatch final : public Drawer {
public:
    static core::Result<QuadBatch> create(Renderer& renderer, rhi::BlendMode blend = rhi::BlendMode::Alpha);

    void add(const Quad& quad) { appendQuad(quad, vertices_); }
    void clear() { vertices_.clear(); }
    [[nodiscard]] std::size_t size() const { return vertices_.size() / 6; }

    void draw(DrawContext& context) override;

private:
    QuadBatch(rhi::GraphicsPipeline scene, rhi::GraphicsPipeline ui);

    rhi::GraphicsPipeline scene_;
    rhi::GraphicsPipeline ui_;
    std::vector<QuadVertex> vertices_;
};

} // namespace dwe::render
