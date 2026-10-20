#include <dwe/core/log.hpp>
#include <dwe/render/map_renderer.hpp>
#include <dwe/shaders/map.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <span>
#include <utility>
#include <vector>

namespace dwe::render {

namespace {

using assets::SpriteBlend;

// map.slang's vertex input.
template <typename Vertex>
constexpr std::array<rhi::VertexAttribute, 4> mapAttributes() {
    return {
        rhi::VertexAttribute{.location = 0,
                             .format = rhi::VertexFormat::Float4,
                             .offset = static_cast<std::uint32_t>(offsetof(Vertex, position))},
        rhi::VertexAttribute{.location = 1,
                             .format = rhi::VertexFormat::Float2,
                             .offset = static_cast<std::uint32_t>(offsetof(Vertex, uv))},
        rhi::VertexAttribute{.location = 2,
                             .format = rhi::VertexFormat::Uint2,
                             .offset = static_cast<std::uint32_t>(offsetof(Vertex, textures))},
        rhi::VertexAttribute{.location = 3,
                             .format = rhi::VertexFormat::Float4,
                             .offset = static_cast<std::uint32_t>(offsetof(Vertex, color))},
    };
}

struct MapPush {
    float pixelsPerTexel = 1.0F;
    float fade = 0.0F;
    // A TexelFilter, as the shader's uint.
    std::uint32_t texels = 0;
    // The palette rows faded from and to.
    std::uint32_t fromRow = 0;
    std::uint32_t toRow = 0;
};

// The colours of a palette row; the background's indices are bytes.
constexpr std::uint32_t PaletteColors = 256;

// map.slang: a texture whose texels are colours, not indices into a palette texture.
constexpr std::uint32_t NoPalette = rhi::TextureId::InvalidIndex;

// A rectangle of the screen as two triangles, corners already in clip space.
template <typename Vertex>
void appendQuad(std::vector<Vertex>& vertices, std::array<glm::vec4, 4> corners, glm::vec2 uvMin, glm::vec2 uvMax,
                glm::uvec2 textures, glm::vec4 color) {
    const Vertex topLeft{.position = corners[0], .uv = uvMin, .textures = textures, .color = color};
    const Vertex topRight{.position = corners[1], .uv = {uvMax.x, uvMin.y}, .textures = textures, .color = color};
    const Vertex bottomLeft{.position = corners[2], .uv = {uvMin.x, uvMax.y}, .textures = textures, .color = color};
    const Vertex bottomRight{.position = corners[3], .uv = uvMax, .textures = textures, .color = color};
    vertices.insert(vertices.end(), {topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight});
}

std::array<glm::vec4, 4> backgroundCorners(const MapCamera& camera, glm::vec2 min, glm::vec2 max, float depth) {
    return {camera.backgroundToClip(min, depth), camera.backgroundToClip({max.x, min.y}, depth),
            camera.backgroundToClip({min.x, max.y}, depth), camera.backgroundToClip(max, depth)};
}

// How a depth layer's blend is drawn: the opacity the pipeline's blend uses, as rhi::BlendMode documents.
float blendAlpha(SpriteBlend blend) {
    switch (blend) {
    case SpriteBlend::Average:
        return 0.5F;
    case SpriteBlend::AddQuarter:
        return 0.25F;
    default:
        return 1.0F;
    }
}

// A palette padded to PaletteColors with transparent colours, as RGBA bytes.
std::vector<assets::PaletteColor> paletteRow(std::span<const assets::PaletteColor> palette) {
    std::vector<assets::PaletteColor> row(PaletteColors);
    std::copy_n(palette.begin(), std::min<std::size_t>(palette.size(), PaletteColors), row.begin());
    return row;
}

// Palette indices as RGBA texels, the index in red (map.slang reads it back).
std::vector<std::byte> indexTexels(std::span<const std::uint8_t> indices) {
    std::vector<std::byte> texels(indices.size() * 4);
    for (std::size_t index = 0; index < indices.size(); ++index) {
        texels[index * 4] = std::byte{indices[index]};
        texels[(index * 4) + 3] = std::byte{255};
    }
    return texels;
}

} // namespace

core::Result<std::unique_ptr<MapRenderer>> MapRenderer::create(Renderer& renderer) {
    static constexpr auto Attributes = mapAttributes<Vertex>();
    const auto make = [&renderer](rhi::DepthState depth, rhi::BlendMode blend,
                                  std::string_view name) -> core::Result<rhi::GraphicsPipeline> {
        return renderer.createPipeline(
            {
                .spirv = shaders::Map,
                .vertex = {.stride = sizeof(Vertex), .attributes = Attributes},
                .depth = depth,
                .blend = blend,
                .name = name,
            },
            Pass::Scene);
    };
    const rhi::DepthState none{};
    const rhi::DepthState tested{.test = true, .write = false, .compare = rhi::CompareOp::LessOrEqual};
    const rhi::DepthState written{.test = true, .write = true, .compare = rhi::CompareOp::LessOrEqual};
    auto background = make(none, rhi::BlendMode::Opaque, "map background");
    auto opaque = make(written, rhi::BlendMode::Opaque, "map layers");
    auto alpha = make(tested, rhi::BlendMode::Alpha, "map layers, average");
    auto additive = make(tested, rhi::BlendMode::Additive, "map layers, additive");
    auto subtractive = make(tested, rhi::BlendMode::Subtractive, "map layers, subtractive");
    for (const auto* pipeline : {&background, &opaque, &alpha, &additive, &subtractive}) {
        if (!*pipeline) {
            return std::unexpected{pipeline->error()};
        }
    }
    const std::array<std::byte, 4> blackPixel{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
    auto black = renderer.createTexture({.width = 1, .height = 1}, blackPixel);
    if (!black) {
        return std::unexpected{black.error()};
    }
    Pipelines pipelines{
        .background = std::move(*background),
        .opaque = std::move(*opaque),
        .alpha = std::move(*alpha),
        .additive = std::move(*additive),
        .subtractive = std::move(*subtractive),
    };
    return std::unique_ptr<MapRenderer>{new MapRenderer{renderer, std::move(pipelines), *black}};
}

MapRenderer::MapRenderer(Renderer& renderer, Pipelines pipelines, TextureId black)
    : renderer_(&renderer), pipelines_(std::move(pipelines)), black_(black) {}

MapRenderer::~MapRenderer() {
    releaseMap();
    renderer_->destroyTexture(black_);
}

core::Result<TextureId> MapRenderer::makeTexture(rhi::Extent extent, std::span<const std::byte> pixels) {
    auto texture = renderer_->queueTexture(extent, pixels);
    if (texture) {
        return texture;
    }
    core::logWarning("map: cannot queue a {}x{} texture ({}); uploading it at once", extent.width, extent.height,
                     texture.error().message);
    return renderer_->createTexture(extent, pixels);
}

void MapRenderer::releaseMap() {
    for (const TextureId texture : {backgroundIndices_, palettes_}) {
        if (texture.valid()) {
            renderer_->destroyTexture(texture);
        }
    }
    for (const SpriteTexture& sprite : spriteTextures_) {
        renderer_->destroyTexture(sprite.id);
    }
    backgroundIndices_ = {};
    palettes_ = {};
    paletteRows_.clear();
    spriteTextures_.clear();
    placements_.clear();
    camera_ = MapCamera{MapCameraDesc{}};
    hasMap_ = false;
    layers_ = {};
}

core::Result<void> MapRenderer::setMap(const MapSceneData& scene, std::size_t variant) {
    releaseMap();
    const assets::MapImages& images = scene.images;
    if (images.backgroundPalettes.empty()) {
        return core::fail(std::format("map {} has no background palette", scene.document.id));
    }
    const auto fail = [this](const core::Error& error) -> core::Result<void> {
        releaseMap();
        return std::unexpected{error};
    };

    auto background = makeTexture({.width = images.background.width, .height = images.background.height},
                                  indexTexels(images.background.indices));
    if (!background) {
        return fail(background.error());
    }
    backgroundIndices_ = *background;

    // A row per variant and the snapshot row, a copy of the first until a fade is interrupted.
    for (const std::vector<assets::PaletteColor>& palette : images.backgroundPalettes) {
        paletteRows_.push_back(paletteRow(palette));
    }
    paletteRows_.push_back(paletteRows_.front());
    std::vector<assets::PaletteColor> paletteTexels;
    paletteTexels.reserve(paletteRows_.size() * PaletteColors);
    for (const std::vector<assets::PaletteColor>& row : paletteRows_) {
        paletteTexels.insert(paletteTexels.end(), row.begin(), row.end());
    }
    auto palettes = makeTexture({.width = PaletteColors, .height = static_cast<std::uint32_t>(paletteRows_.size())},
                                std::as_bytes(std::span{paletteTexels}));
    if (!palettes) {
        return fail(palettes.error());
    }
    palettes_ = *palettes;

    // One texture per sheet texture, shared by every variant: time-of-day sprites index the palette texture.
    for (const assets::MapSpriteSheet::Texture& source : images.sprites.textures) {
        const rhi::Extent extent{.width = source.width(), .height = source.height()};
        auto texture = source.timeOfDayPalette ? makeTexture(extent, indexTexels(source.indices.indices))
                                               : makeTexture(extent, source.colors.pixels);
        if (!texture) {
            return fail(texture.error());
        }
        spriteTextures_.push_back({.id = *texture,
                                   .size = {static_cast<float>(extent.width), static_cast<float>(extent.height)},
                                   .timeOfDayPalette = source.timeOfDayPalette});
    }
    placements_ = images.sprites.placements;

    camera_ = MapCamera{scene.camera};
    hasMap_ = true;
    if (scene.document.objects) {
        layers_ = MapDepthLayers{*scene.document.objects, camera_};
    }
    backgroundSize_ = scene.camera.backgroundSize;
    const auto shown = static_cast<std::uint32_t>(std::min(variant, images.backgroundPalettes.size() - 1));
    fromRow_ = shown;
    toRow_ = shown;
    fade_ = 1.0;
    return {};
}

bool MapRenderer::snapshotPalette() {
    const std::size_t snapshotRow = paletteRows_.size() - 1;
    std::vector<assets::PaletteColor>& snapshot = paletteRows_[snapshotRow];
    const std::vector<assets::PaletteColor>& from = paletteRows_[fromRow_];
    const std::vector<assets::PaletteColor>& to = paletteRows_[toRow_];
    const auto mix = [fade = timeOfDayFadeShown(fade_)](std::uint8_t lhs, std::uint8_t rhs) {
        return static_cast<std::uint8_t>(
            std::lround(std::lerp(static_cast<double>(lhs), static_cast<double>(rhs), fade)));
    };
    // from may be the snapshot row itself: each colour is read before it is written.
    for (std::size_t color = 0; color < PaletteColors; ++color) {
        snapshot[color] = {.red = mix(from[color].red, to[color].red),
                           .green = mix(from[color].green, to[color].green),
                           .blue = mix(from[color].blue, to[color].blue),
                           .alpha = mix(from[color].alpha, to[color].alpha)};
    }
    const auto written = renderer_->queueTextureUpdate(
        palettes_, {.x = 0, .y = static_cast<std::int32_t>(snapshotRow), .width = PaletteColors, .height = 1},
        std::as_bytes(std::span{snapshot}));
    if (!written) {
        core::logWarning("map: cannot update the palette snapshot: {}", written.error().message);
        return false;
    }
    fromRow_ = static_cast<std::uint32_t>(snapshotRow);
    return true;
}

void MapRenderer::setVariant(std::size_t variant, double fadeSeconds) {
    if (!hasMap_) {
        return;
    }
    const auto target = static_cast<std::uint32_t>(std::min(variant, paletteRows_.size() - 2));
    fadeSeconds_ = fadeSeconds;
    if (fadeSeconds <= 0.0) {
        fromRow_ = target;
        toRow_ = target;
        fade_ = 1.0;
        return;
    }
    if (fade_ >= 1.0) {
        if (target == toRow_) {
            return;
        }
        fromRow_ = toRow_;
    } else if (target == toRow_) {
        return;
    } else if (target == fromRow_) {
        // Back to where the fade came from: run it backwards from where it is.
        std::swap(fromRow_, toRow_);
        fade_ = 1.0 - fade_;
        return;
    } else if (!snapshotPalette()) {
        // Without the snapshot, fade from the nearer end.
        fromRow_ = fade_ >= 0.5 ? toRow_ : fromRow_;
    }
    toRow_ = target;
    fade_ = 0.0;
}

double timeOfDayFadeShown(double progress) {
    constexpr double Frames = 25.0;
    constexpr double FramesPerStep = 5.0;
    constexpr double FirstDivisor = 6.0;
    if (progress >= 1.0) {
        return 1.0;
    }
    const double frame = std::floor(std::max(0.0, progress) * Frames);
    return 1.0 / (FirstDivisor - std::floor(frame / FramesPerStep));
}

void MapRenderer::update(double animationSeconds, double fadeSeconds, glm::vec3 worldFocus) {
    focus_ = worldFocus;
    if (!hasMap_) {
        return;
    }
    layers_.update(animationSeconds);
    if (fade_ < 1.0) {
        fade_ = fadeSeconds_ > 0.0 ? std::min(1.0, fade_ + (fadeSeconds / fadeSeconds_)) : 1.0;
    }
}

void MapRenderer::prepare(PrepareContext& context) {
    if (!hasMap_) {
        return;
    }
    const rhi::Extent extent = context.extent();
    camera_.setViewport({static_cast<float>(extent.width), static_cast<float>(extent.height)});
    camera_.follow(focus_);
}

void MapRenderer::warnUploadFailure(std::size_t bytes) {
    if (!uploadWarned_) {
        uploadWarned_ = true;
        core::logWarning("map: no room in this frame's upload memory for {} bytes of vertices; the map is not drawn "
                         "(logged once)",
                         bytes);
    }
}

void MapRenderer::drawBackground(DrawContext& context) {
    if (!hasMap_) {
        return;
    }
    vertices_.clear();
    appendQuad(vertices_, backgroundCorners(camera_, {0.0F, 0.0F}, backgroundSize_, MapFarClip), {0.0F, 0.0F},
               {1.0F, 1.0F}, {backgroundIndices_.index, palettes_.index}, glm::vec4{1.0F});
    const auto upload = context.upload().upload(std::span<const Vertex>{vertices_});
    if (!upload) {
        warnUploadFailure(vertices_.size() * sizeof(Vertex));
        return;
    }
    rhi::CommandList& commands = context.commands();
    commands.bindPipeline(pipelines_.background);
    commands.pushConstants(MapPush{.pixelsPerTexel = camera_.scale(),
                                   .fade = static_cast<float>(timeOfDayFadeShown(fade_)),
                                   .texels = static_cast<std::uint32_t>(TexelFilter::All),
                                   .fromRow = fromRow_,
                                   .toRow = toRow_});
    commands.bindVertexBuffer(*upload);
    commands.draw(static_cast<std::uint32_t>(vertices_.size()));
}

void MapRenderer::addToRun(SpriteBlend blend, TexelFilter texels, std::uint32_t first) {
    if (runs_.empty() || runs_.back().blend != blend || runs_.back().texels != texels ||
        runs_.back().first + runs_.back().count != first) {
        runs_.push_back({.blend = blend, .texels = texels, .first = first, .count = 0});
    }
    runs_.back().count += 6;
}

void MapRenderer::drawLayers(DrawContext& context) {
    if (!hasMap_) {
        return;
    }
    // Far to near, one draw per run of quads with the same blend and texel filter.
    vertices_.clear();
    runs_.clear();
    const glm::mat4 viewProjection = camera_.viewProjection();
    const glm::vec3 right{camera_.view()[0][0], camera_.view()[1][0], camera_.view()[2][0]};
    const glm::uvec2 white{renderer_->whiteTexture().index, NoPalette};
    for (const MapMarker& marker : markers_) {
        const glm::vec3 side = glm::normalize(glm::vec3{right.x, 0.0F, right.z}) * (marker.width * 0.5F);
        const glm::vec3 up{0.0F, marker.height, 0.0F};
        const auto clip = [&viewProjection](glm::vec3 world) { return viewProjection * glm::vec4{world, 1.0F}; };
        const auto first = static_cast<std::uint32_t>(vertices_.size());
        appendQuad(vertices_,
                   {clip(marker.feet - side + up), clip(marker.feet + side + up), clip(marker.feet - side),
                    clip(marker.feet + side)},
                   {0.0F, 0.0F}, {1.0F, 1.0F}, white, marker.color);
        addToRun(SpriteBlend::Opaque, TexelFilter::All, first);
    }
    for (const DepthLayerQuad& quad : layers_.quads()) {
        if (quad.sprite >= placements_.size() ||
            placements_[quad.sprite].texture == assets::MapSpriteSheet::NoTexture) {
            continue;
        }
        const assets::MapSpriteSheet::Placement& placement = placements_[quad.sprite];
        const SpriteTexture& texture = spriteTextures_[placement.texture];
        const glm::vec2 uvMin = glm::vec2{placement.texel} / texture.size;
        const glm::vec2 uvMax = glm::vec2{placement.texel + placement.size} / texture.size;
        const auto corners = backgroundCorners(camera_, quad.position, quad.position + quad.size, quad.viewDepth);
        const glm::uvec2 textures{texture.id.index, texture.timeOfDayPalette ? palettes_.index : NoPalette};
        const auto plainStart = static_cast<std::uint32_t>(vertices_.size());
        appendQuad(vertices_, corners, uvMin, uvMax, textures, glm::vec4{1.0F});
        if (quad.blend == SpriteBlend::Opaque) {
            addToRun(SpriteBlend::Opaque, TexelFilter::All, plainStart);
            continue;
        }
        // A semi-transparent sprite mixes only its texels with STP; the others are opaque.
        addToRun(SpriteBlend::Opaque, TexelFilter::Plain, plainStart);
        const auto semiStart = static_cast<std::uint32_t>(vertices_.size());
        appendQuad(vertices_, corners, uvMin, uvMax, textures, {1.0F, 1.0F, 1.0F, blendAlpha(quad.blend)});
        addToRun(quad.blend, TexelFilter::SemiTransparent, semiStart);
    }
    // Bars where the background does not reach, drawn last over everything (ADR 0020).
    const PixelRect inside = camera_.backgroundRect();
    const glm::vec2 viewport = camera_.viewportSize();
    const auto toClip = [viewport](glm::vec2 window) {
        return glm::vec4{(window / viewport * 2.0F) - 1.0F, 0.0F, 1.0F};
    };
    const std::array<std::pair<glm::vec2, glm::vec2>, 4> bars{{
        {{0.0F, 0.0F}, {viewport.x, inside.origin.y}},
        {{0.0F, inside.origin.y + inside.size.y}, viewport},
        {{0.0F, inside.origin.y}, {inside.origin.x, inside.origin.y + inside.size.y}},
        {{inside.origin.x + inside.size.x, inside.origin.y}, {viewport.x, inside.origin.y + inside.size.y}},
    }};
    const auto barsStart = static_cast<std::uint32_t>(vertices_.size());
    for (const auto& [min, max] : bars) {
        if (max.x > min.x && max.y > min.y) {
            appendQuad(vertices_, {toClip(min), toClip({max.x, min.y}), toClip({min.x, max.y}), toClip(max)},
                       {0.0F, 0.0F}, {1.0F, 1.0F}, {black_.index, NoPalette}, glm::vec4{1.0F});
        }
    }
    if (vertices_.empty()) {
        return;
    }
    const auto upload = context.upload().upload(std::span<const Vertex>{vertices_});
    if (!upload) {
        warnUploadFailure(vertices_.size() * sizeof(Vertex));
        return;
    }
    rhi::CommandList& commands = context.commands();
    commands.bindVertexBuffer(*upload);
    // Sprites are sampled nearest: blending a texel with a transparent or STP neighbour would mix that colour in and
    // leave a dark fringe that passes the alpha test. A huge pixelsPerTexel makes map.slang's blend a step.
    constexpr float NearestSampling = 1.0e6F;
    MapPush push{.pixelsPerTexel = NearestSampling,
                 .fade = static_cast<float>(fade_),
                 .texels = 0,
                 .fromRow = fromRow_,
                 .toRow = toRow_};
    for (const Run& run : runs_) {
        push.texels = static_cast<std::uint32_t>(run.texels);
        switch (run.blend) {
        case SpriteBlend::Opaque:
            commands.bindPipeline(pipelines_.opaque);
            break;
        case SpriteBlend::Average:
            commands.bindPipeline(pipelines_.alpha);
            break;
        case SpriteBlend::Add:
        case SpriteBlend::AddQuarter:
            commands.bindPipeline(pipelines_.additive);
            break;
        case SpriteBlend::Subtract:
            commands.bindPipeline(pipelines_.subtractive);
            break;
        }
        commands.pushConstants(push);
        commands.draw(run.count, run.first);
    }
    const auto barVertices = static_cast<std::uint32_t>(vertices_.size()) - barsStart;
    if (barVertices > 0) {
        push.texels = static_cast<std::uint32_t>(TexelFilter::All);
        commands.bindPipeline(pipelines_.background);
        commands.pushConstants(push);
        commands.draw(barVertices, barsStart);
    }
}

} // namespace dwe::render
