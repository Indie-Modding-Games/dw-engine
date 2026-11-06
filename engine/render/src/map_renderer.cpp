#include <dwe/core/log.hpp>
#include <dwe/render/hd_scenery_wrap.hpp>
#include <dwe/render/map_renderer.hpp>
#include <dwe/render/scenery_upscale.hpp>
#include <dwe/shaders/map.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace dwe::render {

// An upscale of the background on its thread: its result, set before finished.
struct MapRenderer::UpscaleJob {
  std::optional<UpscaledScenery> result;
  // Whether it came from the cache, and how long it took.
  bool fromCache = false;
  double milliseconds = 0.0;
  std::atomic<bool> finished{false};
};

namespace {

using assets::SpriteBlend;

// map.slang's vertex input.
template <typename Vertex>
constexpr std::array<rhi::VertexAttribute, 7> mapAttributes() {
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
      rhi::VertexAttribute{.location = 4,
                           .format = rhi::VertexFormat::Uint2,
                           .offset = static_cast<std::uint32_t>(offsetof(Vertex, sampling))},
      rhi::VertexAttribute{.location = 5,
                           .format = rhi::VertexFormat::Float2,
                           .offset = static_cast<std::uint32_t>(offsetof(Vertex, sourceUv))},
      rhi::VertexAttribute{.location = 6,
                           .format = rhi::VertexFormat::Float4,
                           .offset = static_cast<std::uint32_t>(offsetof(Vertex, rect))},
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
  // The scenery's grade (MapGrade): 0 draws the colours as they are (the bars, a neutral grade).
  std::uint32_t graded = 0;
  float foliageHue = 0.0F;
  float foliage = 0.0F;
  float saturation = 1.0F;
  float gainRed = 1.0F;
  float gainGreen = 1.0F;
  float gainBlue = 1.0F;
  float lift = 0.0F;
  // HD scenery's filtered background for Sampling::Screen and CutoutScreen, and where it has the
  // window's top-left pixel (MapRenderer::hdScreenShift).
  std::uint32_t screen = rhi::TextureId::InvalidIndex;
  std::uint32_t screenShiftX = 0;
  std::uint32_t screenShiftY = 0;
  // The upscaled background's variant textures and weights, for Sampling::UpscaledPrefilter.
  std::uint32_t upscaled0 = rhi::TextureId::InvalidIndex;
  std::uint32_t upscaled1 = rhi::TextureId::InvalidIndex;
  std::uint32_t upscaled2 = rhi::TextureId::InvalidIndex;
  float upscaledWeight0 = 0.0F;
  float upscaledWeight1 = 0.0F;
  float upscaledWeight2 = 0.0F;
  // Where the background's texel u falls in them: u * upscaledScale + upscaledShift.
  float upscaledScale = 2.0F;
  float upscaledShift = 0.0F;
};

// The push constants of a draw with the scenery's grade.
MapPush withGrade(MapPush push, const MapGrade& grade) {
  if (grade.neutral()) {
    push.graded = 0;
    return push;
  }
  push.graded = 1;
  push.foliageHue = grade.foliageHue / 360.0F;
  push.foliage = grade.foliage;
  push.saturation = grade.saturation;
  push.gainRed = grade.gain.r;
  push.gainGreen = grade.gain.g;
  push.gainBlue = grade.gain.b;
  push.lift = grade.lift;
  return push;
}

// The colours of a palette row; the background's indices are bytes.
constexpr std::uint32_t PaletteColors = 256;

// map.slang: a texture whose texels are colours, not indices into a palette texture.
constexpr std::uint32_t NoPalette = rhi::TextureId::InvalidIndex;

// How map.slang samples a quad's texels: its Sampling constants.
enum class Sampling : std::uint32_t {
  Sharp = 0,
  Smooth = 1,
  Cutout = 2,
  // HD scenery's background pass, and the reads of what it drew (MapPush::screen).
  Prefilter = 3,
  Screen = 4,
  CutoutScreen = 5,
  // The background pass at best quality: the upscaled background resampled.
  UpscaledPrefilter = 6,
};

// The upscaler's version in the background cache keys: change it when its output changes.
constexpr std::uint32_t SceneryUpscaleVersion = 1;
// Rows of an upscaled variant uploaded per queued update, so no update outgrows a frame's upload
// memory.
constexpr std::size_t UpscaledBandBytes = std::size_t{4} << 20U;

// The background doubled in every variant, from the cache or made (and kept there); nothing when
// cancelled.
std::optional<UpscaledScenery>
upscaledBackground(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> indices,
                   std::span<const std::vector<assets::PaletteColor>> palettes,
                   const UpscaleCache* cache, const std::stop_token& stop, bool& fromCache) {
  const UpscaleOptions options;
  UpscaleKey key;
  key.add(SceneryUpscaleVersion).add(width).add(height).add(options.dither).add(options.sharpen);
  key.add(std::as_bytes(indices));
  for (const std::vector<assets::PaletteColor>& palette : palettes) {
    key.add(std::as_bytes(std::span{palette}));
  }
  const std::size_t variantTexels = std::size_t{width} * 2 * std::size_t{height} * 2;
  const std::size_t bytes = variantTexels * sizeof(assets::PaletteColor) * palettes.size();
  if (cache != nullptr) {
    if (auto kept = cache->load(key.value(), bytes)) {
      fromCache = true;
      UpscaledScenery scenery{
          .width = width * 2, .height = height * 2, .scale = 2, .shift = 0.0F, .variants = {}};
      for (std::size_t variant = 0; variant < palettes.size(); ++variant) {
        std::vector<assets::PaletteColor>& texels = scenery.variants.emplace_back(variantTexels);
        std::memcpy(texels.data(),
                    kept->data() + (variant * variantTexels * sizeof(assets::PaletteColor)),
                    variantTexels * sizeof(assets::PaletteColor));
      }
      return scenery;
    }
  }
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  UpscaledScenery scenery = upscaleScenery(width, height, indices, palettes, options);
  if (stop.stop_requested() || scenery.variants.size() != palettes.size()) {
    return std::nullopt;
  }
  if (cache != nullptr) {
    std::vector<std::byte> data;
    data.reserve(bytes);
    for (const std::vector<assets::PaletteColor>& texels : scenery.variants) {
      const auto view = std::as_bytes(std::span{texels});
      data.insert(data.end(), view.begin(), view.end());
    }
    cache->store(key.value(), data);
  }
  return scenery;
}

// A quad's sampling (map.slang): the original's pixels by default; with HD scenery the smooth
// filter over `rect` (texels, first and last), reading the colours from `source` between
// sourceMin and sourceMax (uv) for a cut-out.
struct QuadSampling {
  Sampling mode = Sampling::Sharp;
  std::uint32_t source = NoPalette;
  glm::vec2 sourceMin{0.0F};
  glm::vec2 sourceMax{0.0F};
  glm::vec4 rect{0.0F};
};

// A rectangle of the screen as two triangles, corners already in clip space.
template <typename Vertex>
void appendQuad(std::vector<Vertex>& vertices, std::array<glm::vec4, 4> corners, glm::vec2 uvMin,
                glm::vec2 uvMax, glm::uvec2 textures, glm::vec4 color,
                const QuadSampling& sampling = {}) {
  const glm::uvec2 mode{static_cast<std::uint32_t>(sampling.mode), sampling.source};
  // `right` and `bottom` pick each corner's coordinates from the minimum or the maximum.
  const auto vertex = [&](std::size_t corner, bool right, bool bottom) {
    const auto pick = [right, bottom](glm::vec2 min, glm::vec2 max) {
      return glm::vec2{right ? max.x : min.x, bottom ? max.y : min.y};
    };
    return Vertex{.position = corners[corner],
                  .uv = pick(uvMin, uvMax),
                  .textures = textures,
                  .color = color,
                  .sampling = mode,
                  .sourceUv = pick(sampling.sourceMin, sampling.sourceMax),
                  .rect = sampling.rect};
  };
  const Vertex topLeft = vertex(0, false, false);
  const Vertex topRight = vertex(1, true, false);
  const Vertex bottomLeft = vertex(2, false, true);
  const Vertex bottomRight = vertex(3, true, true);
  vertices.insert(vertices.end(),
                  {topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight});
}

std::array<glm::vec4, 4> backgroundCorners(const MapCamera& camera, glm::vec2 min, glm::vec2 max,
                                           float depth) {
  return {camera.backgroundToClip(min, depth), camera.backgroundToClip({max.x, min.y}, depth),
          camera.backgroundToClip({min.x, max.y}, depth), camera.backgroundToClip(max, depth)};
}

// How a depth layer's blend is drawn: the opacity the pipeline's blend uses, as rhi::BlendMode
// documents.
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
  const rhi::DepthState tested{
      .test = true, .write = false, .compare = rhi::CompareOp::LessOrEqual};
  const rhi::DepthState written{
      .test = true, .write = true, .compare = rhi::CompareOp::LessOrEqual};
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
  const std::array<std::byte, 4> blackPixel{std::byte{0}, std::byte{0}, std::byte{0},
                                            std::byte{255}};
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
  releaseHdTarget();
  for (const SpriteTexture& texture : billboardTextures_) {
    renderer_->destroyTexture(texture.id);
  }
  renderer_->destroyTexture(black_);
}

core::Result<TextureId> MapRenderer::makeTexture(rhi::Extent extent,
                                                 std::span<const std::byte> pixels) {
  auto texture = renderer_->queueTexture(extent, pixels);
  if (texture) {
    return texture;
  }
  core::logWarning("map: cannot queue a {}x{} texture ({}); uploading it at once", extent.width,
                   extent.height, texture.error().message);
  return renderer_->createTexture(extent, pixels);
}

core::Result<std::size_t> MapRenderer::addBillboardTexture(const assets::Image& image) {
  auto texture =
      makeTexture({.width = image.width, .height = image.height}, std::span{image.pixels});
  if (!texture) {
    return std::unexpected{texture.error()};
  }
  billboardTextures_.push_back(
      {.id = *texture,
       .size = {static_cast<float>(image.width), static_cast<float>(image.height)},
       .timeOfDayPalette = false,
       .indices = {},
       .colors = {}});
  return billboardTextures_.size() - 1;
}

void MapRenderer::releaseMap() {
  cancelUpscale();
  releaseUpscaled();
  replacement_.reset();
  snapshotWeights_.clear();
  for (const TextureId texture : {backgroundIndices_, palettes_, backgroundColors_}) {
    if (texture.valid()) {
      renderer_->destroyTexture(texture);
    }
  }
  for (const SpriteTexture& sprite : spriteTextures_) {
    renderer_->destroyTexture(sprite.id);
    if (sprite.colors.valid()) {
      renderer_->destroyTexture(sprite.colors);
    }
  }
  backgroundIndices_ = {};
  palettes_ = {};
  backgroundColors_ = {};
  hdTargetFilled_ = false;
  backgroundIndexCopy_ = {};
  resolvedPalette_.clear();
  resolvedTexels_.clear();
  paletteRows_.clear();
  spriteTextures_.clear();
  placements_.clear();
  cutouts_ = {};
  camera_ = MapCamera{MapCameraDesc{}, camera_.viewportSize()};
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

  // The map's textures are uploaded now, at load time, so its first frame already shows them all:
  // queued, a large map's would share the next frame's upload memory with whatever else was
  // queued and could wait a frame.
  auto background = renderer_->createTexture(
      {.width = images.background.width, .height = images.background.height},
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
  auto palettes = renderer_->createTexture(
      {.width = PaletteColors, .height = static_cast<std::uint32_t>(paletteRows_.size())},
      std::as_bytes(std::span{paletteTexels}));
  if (!palettes) {
    return fail(palettes.error());
  }
  palettes_ = *palettes;

  // One texture per sheet texture, shared by every variant: time-of-day sprites index the palette
  // texture.
  for (const assets::MapSpriteSheet::Texture& source : images.sprites.textures) {
    const rhi::Extent extent{.width = source.width(), .height = source.height()};
    auto texture = source.timeOfDayPalette
                       ? renderer_->createTexture(extent, indexTexels(source.indices.indices))
                       : renderer_->createTexture(extent, source.colors.pixels);
    if (!texture) {
      return fail(texture.error());
    }
    spriteTextures_.push_back(
        {.id = *texture,
         .size = {static_cast<float>(extent.width), static_cast<float>(extent.height)},
         .timeOfDayPalette = source.timeOfDayPalette,
         .indices = source.timeOfDayPalette ? source.indices.indices : std::vector<std::uint8_t>{},
         .colors = {}});
  }
  placements_ = images.sprites.placements;
  cutouts_ = MapCutouts{images};
  backgroundIndexCopy_ = {.width = images.background.width,
                          .height = images.background.height,
                          .indices = images.background.indices,
                          .palette = {}};

  camera_ = MapCamera{scene.camera, camera_.viewportSize()};
  hasMap_ = true;
  if (scene.document.objects) {
    layers_ = MapDepthLayers{*scene.document.objects, camera_};
  }
  backgroundSize_ = scene.camera.backgroundSize;
  const auto shown =
      static_cast<std::uint32_t>(std::min(variant, images.backgroundPalettes.size() - 1));
  fromRow_ = shown;
  toRow_ = shown;
  fade_ = 1.0;
  // The snapshot row starts as a copy of the first variant.
  snapshotWeights_.assign(images.backgroundPalettes.size(), 0.0);
  snapshotWeights_.front() = 1.0;
  if (images.replacement) {
    replacement_ = sceneryFromReplacement(*images.replacement);
  }
  if (hdActive()) {
    resolveBackgroundColors(true);
    startUpscale();
  }
  return {};
}

bool MapRenderer::updateImages(const assets::MapImages& images) {
  if (!hasMap_ || images.sprites.textures.size() != spriteTextures_.size()) {
    core::logWarning("map: the images to update do not match the map shown");
    return false;
  }
  const auto upload = [this](TextureId texture, std::uint32_t width, std::uint32_t height,
                             std::span<const std::byte> texels) {
    const auto written = renderer_->queueTextureUpdate(
        texture, {.x = 0, .y = 0, .width = width, .height = height}, texels);
    if (!written) {
      core::logWarning("map: cannot update a texture: {}", written.error().message);
      return false;
    }
    return true;
  };
  bool ok = upload(backgroundIndices_, images.background.width, images.background.height,
                   indexTexels(images.background.indices));
  // The signs changed some pixels of the background and of its cut-outs alike.
  cutouts_ = MapCutouts{images};
  backgroundIndexCopy_.indices = images.background.indices;
  resolvedPalette_.clear();
  // Smooth draws until the new pixels are upscaled (from the cache, when they were before). The
  // player's images stay: they were drawn with whatever signs the player chose.
  if (!replacement_) {
    releaseUpscaled();
    if (hdScenery_) {
      startUpscale();
    }
  }
  for (std::size_t index = 0; index < spriteTextures_.size(); ++index) {
    const assets::MapSpriteSheet::Texture& source = images.sprites.textures[index];
    if (source.timeOfDayPalette != spriteTextures_[index].timeOfDayPalette ||
        static_cast<float>(source.width()) != spriteTextures_[index].size.x ||
        static_cast<float>(source.height()) != spriteTextures_[index].size.y) {
      core::logWarning("map: sprite texture {} changed shape; left as it was", index);
      ok = false;
      continue;
    }
    if (source.timeOfDayPalette) {
      spriteTextures_[index].indices = source.indices.indices;
    }
    ok = (source.timeOfDayPalette ? upload(spriteTextures_[index].id, source.width(),
                                           source.height(), indexTexels(source.indices.indices))
                                  : upload(spriteTextures_[index].id, source.width(),
                                           source.height(), source.colors.pixels)) &&
         ok;
  }
  return ok;
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
  snapshotWeights_ = shownWeights();
  // from may be the snapshot row itself: each colour is read before it is written.
  for (std::size_t color = 0; color < PaletteColors; ++color) {
    snapshot[color] = {.red = mix(from[color].red, to[color].red),
                       .green = mix(from[color].green, to[color].green),
                       .blue = mix(from[color].blue, to[color].blue),
                       .alpha = mix(from[color].alpha, to[color].alpha)};
  }
  const auto written = renderer_->queueTextureUpdate(
      palettes_,
      {.x = 0, .y = static_cast<std::int32_t>(snapshotRow), .width = PaletteColors, .height = 1},
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

void MapRenderer::setStageCamera(const std::optional<MapCameraDesc>& desc) {
  if (!desc) {
    stageCamera_.reset();
    return;
  }
  const glm::vec2 viewport = (stageCamera_ ? *stageCamera_ : camera_).viewportSize();
  stageCamera_.emplace(*desc);
  stageCamera_->setViewport(viewport);
  stageCamera_->followBackgroundPixel(desc->backgroundSize * 0.5F);
}

void MapRenderer::prepare(PrepareContext& context) {
  if (!hasMap_) {
    return;
  }
  const rhi::Extent extent = context.extent();
  const glm::vec2 viewport{static_cast<float>(extent.width), static_cast<float>(extent.height)};
  camera_.setViewport(viewport);
  if (focusPixel_) {
    camera_.followBackgroundPixel(*focusPixel_);
  } else {
    camera_.follow(focus_);
  }
  if (stageCamera_) {
    stageCamera_->setViewport(viewport);
    stageCamera_->followBackgroundPixel(stageCamera_->desc().backgroundSize * 0.5F);
  }
  if (hdActive() && !stageCamera_) {
    resolveBackgroundColors(false);
    pollUpscale();
    keepHdTarget(extent);
    hdShift_ = glm::ivec2{glm::round(camera_.scroll() * camera_.scale())};
  }
}

void MapRenderer::setHdScenery(bool on) {
  const bool was = hdScenery_;
  hdScenery_ = on;
  if (replacement_) {
    return;
  }
  if (!on) {
    releaseHdTarget();
    cancelUpscale();
    releaseUpscaled();
  } else if (!was) {
    startUpscale();
  }
}

void MapRenderer::setSceneryQuality(SceneryQuality quality) {
  if (quality == quality_) {
    return;
  }
  quality_ = quality;
  if (replacement_) {
    return;
  }
  releaseUpscaled();
  startUpscale();
}

bool MapRenderer::waitForUpscale(std::chrono::milliseconds timeout) const {
  if (!upscaleJob_) {
    return true;
  }
  const auto until = std::chrono::steady_clock::now() + timeout;
  while (!upscaleJob_->finished.load(std::memory_order_acquire)) {
    if (std::chrono::steady_clock::now() >= until) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return true;
}

void MapRenderer::cancelUpscale() {
  if (!upscaleJob_) {
    return;
  }
  // The job under way is for other pixels: it finishes on its own and its result is dropped.
  for (UpscaleWork& work : upscaleWork_) {
    if (work.job == upscaleJob_) {
      work.thread.request_stop();
    }
  }
  upscaleJob_.reset();
}

void MapRenderer::startUpscale() {
  cancelUpscale();
  if (hasMap_ && replacement_) {
    core::logInfo("map: drawing the player's background, {}x{} in {} variants", replacement_->width,
                  replacement_->height, replacement_->variants.size());
    uploadUpscaled(*replacement_);
    return;
  }
  if (!hasMap_ || !hdScenery_ || quality_ != SceneryQuality::Upscaled || paletteRows_.size() < 2 ||
      backgroundIndexCopy_.indices.empty()) {
    return;
  }
  auto job = std::make_shared<UpscaleJob>();
  // The variants' rows, without the snapshot row.
  std::vector<std::vector<assets::PaletteColor>> palettes(paletteRows_.begin(),
                                                          paletteRows_.end() - 1);
  std::jthread thread{[job, cache = upscaleCache_, width = backgroundIndexCopy_.width,
                       height = backgroundIndexCopy_.height, indices = backgroundIndexCopy_.indices,
                       palettes = std::move(palettes)](const std::stop_token& stop) {
    const auto start = std::chrono::steady_clock::now();
    job->result =
        upscaledBackground(width, height, indices, palettes, cache.get(), stop, job->fromCache);
    job->milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    job->finished.store(true, std::memory_order_release);
  }};
  upscaleJob_ = job;
  upscaleWork_.push_back({.job = std::move(job), .thread = std::move(thread)});
}

void MapRenderer::pollUpscale() {
  // Finished threads are joined at once (cancelled ones too).
  std::erase_if(upscaleWork_, [this](const UpscaleWork& work) {
    return work.job != upscaleJob_ && work.job->finished.load(std::memory_order_acquire);
  });
  if (upscaleJob_ && upscaleJob_->finished.load(std::memory_order_acquire)) {
    std::optional<UpscaledScenery> result = std::move(upscaleJob_->result);
    if (result) {
      core::logInfo("map: upscaled background {}x{} in {} variants, {} in {:.0f} ms", result->width,
                    result->height, result->variants.size(),
                    upscaleJob_->fromCache ? "from the cache" : "made", upscaleJob_->milliseconds);
    }
    upscaleJob_.reset();
    std::erase_if(upscaleWork_, [](const UpscaleWork& work) {
      return work.job->finished.load(std::memory_order_acquire);
    });
    if (result) {
      uploadUpscaled(*result);
    } else {
      core::logWarning("map: the upscaled background could not be made; HD scenery stays smooth");
    }
  }
  if (!upscaledReady_ && !upscaled_.empty() &&
      std::ranges::none_of(upscaled_, [this](TextureId texture) {
        return renderer_->textureWritePending(texture);
      })) {
    upscaledReady_ = true;
    hdTargetFilled_ = false;
  }
}

void MapRenderer::uploadUpscaled(const UpscaledScenery& scenery) {
  releaseUpscaled();
  upscaledScale_ = scenery.scale;
  upscaledShift_ = scenery.shift;
  const rhi::Extent extent{.width = scenery.width, .height = scenery.height};
  const std::size_t rowBytes = std::size_t{scenery.width} * sizeof(assets::PaletteColor);
  const auto bandRows =
      static_cast<std::uint32_t>(std::max<std::size_t>(1, UpscaledBandBytes / rowBytes));
  for (const std::vector<assets::PaletteColor>& texels : scenery.variants) {
    auto texture = renderer_->queueTexture(extent);
    if (!texture) {
      core::logWarning("map: cannot make the upscaled background's texture: {}",
                       texture.error().message);
      releaseUpscaled();
      return;
    }
    upscaled_.push_back(*texture);
    const auto bytes = std::as_bytes(std::span{texels});
    for (std::uint32_t row = 0; row < scenery.height; row += bandRows) {
      const std::uint32_t rows = std::min(bandRows, scenery.height - row);
      const auto written = renderer_->queueTextureUpdate(
          *texture,
          {.x = 0, .y = static_cast<std::int32_t>(row), .width = scenery.width, .height = rows},
          bytes.subspan(row * rowBytes, rows * rowBytes));
      if (!written) {
        core::logWarning("map: cannot upload the upscaled background: {}", written.error().message);
        releaseUpscaled();
        return;
      }
    }
  }
}

void MapRenderer::releaseUpscaled() {
  for (const TextureId texture : upscaled_) {
    renderer_->destroyTexture(texture);
  }
  upscaled_.clear();
  if (upscaledReady_) {
    hdTargetFilled_ = false;
  }
  upscaledReady_ = false;
}

std::vector<double> MapRenderer::rowWeights(std::uint32_t row) const {
  if (row + 1 == paletteRows_.size()) {
    return snapshotWeights_;
  }
  std::vector<double> weights(snapshotWeights_.size(), 0.0);
  if (row < weights.size()) {
    weights[row] = 1.0;
  }
  return weights;
}

std::vector<double> MapRenderer::shownWeights() const {
  const double fade = timeOfDayFadeShown(fade_);
  std::vector<double> weights = rowWeights(toRow_);
  if (fade >= 1.0) {
    return weights;
  }
  const std::vector<double> from = rowWeights(fromRow_);
  for (std::size_t variant = 0; variant < weights.size(); ++variant) {
    weights[variant] = std::lerp(from[variant], weights[variant], fade);
  }
  return weights;
}

std::optional<TextureScene> MapRenderer::hdSceneryScene() const {
  if (!hasMap_ || !hdActive() || stageCamera_ || !hdTarget_.valid() || !backgroundColors_.valid()) {
    return std::nullopt;
  }
  return TextureScene{.target = hdTarget_, .drawers = hdSceneryDrawers_, .keep = true};
}

void MapRenderer::keepHdTarget(rhi::Extent extent) {
  if (hdTarget_.valid() && hdTargetExtent_ == extent) {
    return;
  }
  // A frame that names the old target skips its pass and filters on the fly.
  releaseHdTarget();
  auto target = renderer_->createSceneTexture(extent);
  if (!target) {
    core::logWarning("map: cannot make the HD scenery's {}x{} target ({}); filtering on the fly",
                     extent.width, extent.height, target.error().message);
    return;
  }
  hdTarget_ = *target;
  hdTargetExtent_ = extent;
}

void MapRenderer::releaseHdTarget() {
  if (hdTarget_.valid()) {
    renderer_->destroyTexture(std::exchange(hdTarget_, TextureId{}));
  }
  hdTargetExtent_ = {};
  hdTargetFilled_ = false;
}

glm::uvec2 MapRenderer::hdScreenShift() const {
  const glm::ivec2 size{static_cast<int>(hdTargetExtent_.width),
                        static_cast<int>(hdTargetExtent_.height)};
  if (size.x <= 0 || size.y <= 0) {
    return glm::uvec2{0};
  }
  return glm::uvec2{((hdShift_ % size) + size) % size};
}

void MapRenderer::drawHdScenery(DrawContext& context) {
  if (!hasMap_ || !hdActive() || stageCamera_ || !backgroundColors_.valid() ||
      context.extent() != hdTargetExtent_) {
    return;
  }
  const glm::ivec2 size{static_cast<int>(hdTargetExtent_.width),
                        static_cast<int>(hdTargetExtent_.height)};
  // The window's pixels to filter: all of them, or the strips the scroll uncovered since the target
  // was last filled.
  const std::vector<glm::ivec4> regions =
      hdUncoveredRegions(size, hdShift_ - hdFilledShift_, hdTargetFilled_);
  // Colours still waiting for the upload memory are filtered again once they are there.
  hdTargetFilled_ = !renderer_->textureWritePending(backgroundColors_);
  hdFilledShift_ = hdShift_;
  if (regions.empty()) {
    return;
  }

  // Each region is the scaled background's pixels region + hdShift_; they wrap around the target,
  // so a region is drawn in up to four pieces, each a quad over the target's pixels it covers.
  // The background's colours filtered there, unshaded: drawBackground and the cut-outs shade what
  // they read. Alpha keeps each pixel's class, 0 where nothing is drawn (the shader gives 0 past
  // the background's edges, where a pixel of the target may still hold another one).
  const float scale = camera_.scale();
  const glm::vec2 targetSize{size};
  const float depth = MapCamera::depthBufferValue(MapFarClip);
  vertices_.clear();
  for (const glm::ivec4& region : regions) {
    for (const glm::ivec3& across :
         hdWrappedSpan(region.x + hdShift_.x, region.z + hdShift_.x, size.x)) {
      for (const glm::ivec3& down :
           hdWrappedSpan(region.y + hdShift_.y, region.w + hdShift_.y, size.y)) {
        if (across.z <= 0 || down.z <= 0) {
          continue;
        }
        const glm::vec2 targetMin = glm::vec2{across.y, down.y} / targetSize * 2.0F - 1.0F;
        const glm::vec2 targetMax =
            glm::vec2{across.y + across.z, down.y + down.z} / targetSize * 2.0F - 1.0F;
        const glm::vec2 scaledMin{across.x, down.x};
        const glm::vec2 scaledMax{across.x + across.z, down.x + down.z};
        appendQuad(
            vertices_,
            {glm::vec4{targetMin, depth, 1.0F}, glm::vec4{targetMax.x, targetMin.y, depth, 1.0F},
             glm::vec4{targetMin.x, targetMax.y, depth, 1.0F}, glm::vec4{targetMax, depth, 1.0F}},
            scaledMin / scale / backgroundSize_, scaledMax / scale / backgroundSize_,
            glm::uvec2{backgroundColors_.index, NoPalette}, glm::vec4{1.0F},
            QuadSampling{.mode = upscaledReady_ ? Sampling::UpscaledPrefilter : Sampling::Prefilter,
                         .rect = {0.0F, 0.0F, backgroundSize_ - 1.0F}});
      }
    }
  }
  const auto upload = context.upload().upload(std::span<const Vertex>{vertices_});
  if (!upload) {
    warnUploadFailure(vertices_.size() * sizeof(Vertex));
    hdTargetFilled_ = false;
    return;
  }
  MapPush push{.pixelsPerTexel = scale,
               .fade = 1.0F,
               .texels = static_cast<std::uint32_t>(TexelFilter::All),
               .fromRow = 0,
               .toRow = 0,
               .screen = rhi::TextureId::InvalidIndex,
               .screenShiftX = 0,
               .screenShiftY = 0};
  if (upscaledReady_) {
    push.upscaledScale = static_cast<float>(upscaledScale_);
    push.upscaledShift = upscaledShift_;
    // The (at most three) variants the colours shown now mix, heaviest first.
    const std::vector<double> weights = shownWeights();
    std::vector<std::size_t> order(std::min(weights.size(), upscaled_.size()));
    for (std::size_t variant = 0; variant < order.size(); ++variant) {
      order[variant] = variant;
    }
    std::ranges::stable_sort(order, [&weights](std::size_t lhs, std::size_t rhs) {
      return weights[lhs] > weights[rhs];
    });
    order.resize(std::min<std::size_t>(order.size(), 3));
    double total = 0.0;
    for (const std::size_t variant : order) {
      total += weights[variant];
    }
    const std::array<std::uint32_t*, 3> textures{&push.upscaled0, &push.upscaled1, &push.upscaled2};
    const std::array<float*, 3> shares{&push.upscaledWeight0, &push.upscaledWeight1,
                                       &push.upscaledWeight2};
    for (std::size_t slot = 0; slot < order.size(); ++slot) {
      *textures[slot] = upscaled_[order[slot]].index;
      *shares[slot] = total > 0.0 ? static_cast<float>(weights[order[slot]] / total)
                                  : (slot == 0 ? 1.0F : 0.0F);
    }
    for (std::size_t slot = order.size(); slot < 3; ++slot) {
      *textures[slot] = *textures[0];
    }
  }
  rhi::CommandList& commands = context.commands();
  commands.bindPipeline(pipelines_.background);
  commands.pushConstants(push);
  commands.bindVertexBuffer(*upload);
  commands.draw(static_cast<std::uint32_t>(vertices_.size()));
}

void MapRenderer::resolveBackgroundColors(bool atLoad) {
  // The colours texelAt shows: the two rows mixed by the fade (exact at either end).
  const double fade = timeOfDayFadeShown(fade_);
  const std::vector<assets::PaletteColor>& from = paletteRows_[fromRow_];
  const std::vector<assets::PaletteColor>& to = paletteRows_[toRow_];
  std::vector<assets::PaletteColor> palette(PaletteColors);
  const auto mix = [fade](std::uint8_t lhs, std::uint8_t rhs) {
    return static_cast<std::uint8_t>(
        std::lround(std::lerp(static_cast<double>(lhs), static_cast<double>(rhs), fade)));
  };
  for (std::size_t color = 0; color < PaletteColors; ++color) {
    palette[color] = fade >= 1.0
                         ? to[color]
                         : assets::PaletteColor{.red = mix(from[color].red, to[color].red),
                                                .green = mix(from[color].green, to[color].green),
                                                .blue = mix(from[color].blue, to[color].blue),
                                                .alpha = mix(from[color].alpha, to[color].alpha)};
  }
  if (backgroundColors_.valid() && palette == resolvedPalette_) {
    return;
  }
  if (!resolveColors(backgroundColors_,
                     {.width = backgroundIndexCopy_.width, .height = backgroundIndexCopy_.height},
                     backgroundIndexCopy_.indices, palette, atLoad)) {
    if (!backgroundColors_.valid()) {
      hdScenery_ = false;
    }
    return;
  }
  hdTargetFilled_ = false;
  for (SpriteTexture& sprite : spriteTextures_) {
    if (sprite.timeOfDayPalette &&
        !resolveColors(sprite.colors,
                       {.width = static_cast<std::uint32_t>(sprite.size.x),
                        .height = static_cast<std::uint32_t>(sprite.size.y)},
                       sprite.indices, palette, atLoad)) {
      return;
    }
  }
  resolvedPalette_ = std::move(palette);
}

bool MapRenderer::resolveColors(TextureId& texture, rhi::Extent extent,
                                std::span<const std::uint8_t> indices,
                                std::span<const assets::PaletteColor> palette, bool atLoad) {
  resolvedTexels_.resize(indices.size());
  for (std::size_t texel = 0; texel < indices.size(); ++texel) {
    resolvedTexels_[texel] = palette[indices[texel]];
  }
  const auto texels = std::as_bytes(std::span{resolvedTexels_});
  if (!texture.valid()) {
    auto made = atLoad ? renderer_->createTexture(extent, texels) : makeTexture(extent, texels);
    if (!made) {
      core::logWarning("map: cannot make a texture of HD scenery's colours: {}",
                       made.error().message);
      return false;
    }
    texture = *made;
    return true;
  }
  if (const auto written = renderer_->queueTextureUpdate(
          texture, {.x = 0, .y = 0, .width = extent.width, .height = extent.height}, texels);
      !written) {
    core::logWarning("map: cannot update a texture of HD scenery's colours: {}",
                     written.error().message);
    return false;
  }
  return true;
}

void MapRenderer::warnUploadFailure(std::size_t bytes) {
  if (!uploadWarned_) {
    uploadWarned_ = true;
    core::logWarning(
        "map: no room in this frame's upload memory for {} bytes of vertices; the map is not drawn "
        "(logged once)",
        bytes);
  }
}

void MapRenderer::drawBackground(DrawContext& context) {
  if (!hasMap_ || stageCamera_) {
    return;
  }
  vertices_.clear();
  // HD scenery: what the background pass filtered this frame, else the filter on the fly.
  const bool smooth = hdActive() && backgroundColors_.valid();
  const bool screen = smooth && hdTargetReady(context.extent());
  QuadSampling sampling;
  if (screen) {
    sampling.mode = Sampling::Screen;
  } else if (smooth) {
    sampling = {.mode = Sampling::Smooth, .rect = {0.0F, 0.0F, backgroundSize_ - 1.0F}};
  }
  appendQuad(vertices_, backgroundCorners(camera_, {0.0F, 0.0F}, backgroundSize_, MapFarClip),
             {0.0F, 0.0F}, {1.0F, 1.0F},
             smooth ? glm::uvec2{backgroundColors_.index, NoPalette}
                    : glm::uvec2{backgroundIndices_.index, palettes_.index},
             glm::vec4{glm::vec3{shade_}, 1.0F}, sampling);
  const auto upload = context.upload().upload(std::span<const Vertex>{vertices_});
  if (!upload) {
    warnUploadFailure(vertices_.size() * sizeof(Vertex));
    return;
  }
  rhi::CommandList& commands = context.commands();
  commands.bindPipeline(pipelines_.background);
  const glm::uvec2 shift = hdScreenShift();
  commands.pushConstants(
      withGrade(MapPush{.pixelsPerTexel = camera_.scale(),
                        .fade = static_cast<float>(timeOfDayFadeShown(fade_)),
                        .texels = static_cast<std::uint32_t>(TexelFilter::All),
                        .fromRow = fromRow_,
                        .toRow = toRow_,
                        .screen = screen ? hdTarget_.index : rhi::TextureId::InvalidIndex,
                        .screenShiftX = shift.x,
                        .screenShiftY = shift.y},
                grade_));
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

void MapRenderer::appendBillboard(const MapBillboard& billboard, const MapCamera& shown) {
  const glm::mat4 viewProjection = shown.viewProjection();
  const glm::vec3 right{shown.view()[0][0], shown.view()[1][0], shown.view()[2][0]};
  const glm::vec3 up{shown.view()[0][1], shown.view()[1][1], shown.view()[2][1]};
  if (billboard.texture >= billboardTextures_.size()) {
    return;
  }
  const SpriteTexture& texture = billboardTextures_[billboard.texture];
  const glm::vec2 half = billboard.size * 0.5F;
  // Upright: the camera's right and up, turned on screen. Flat: the world's x and z turned about
  // the world's up, the far side first.
  const float cosine = std::cos(billboard.turn);
  const float sine = std::sin(billboard.turn);
  const glm::vec3 screenRight = (right * cosine) - (up * sine);
  const glm::vec3 screenUp = (up * cosine) + (right * sine);
  const bool card = !billboard.flat && billboard.card.has_value();
  const glm::vec3 across = billboard.flat ? glm::vec3{cosine, 0.0F, sine} * half.x
                           : card         ? *billboard.card * half.x
                                          : screenRight * half.x;
  const glm::vec3 along = billboard.flat ? glm::vec3{sine, 0.0F, -cosine} * half.y
                          : card         ? glm::vec3{0.0F, half.y, 0.0F}
                                         : screenUp * half.y;
  // The pivot on the centre: the quad's middle moves away from it.
  const glm::vec2 shift =
      billboard.flat || card ? glm::vec2{0.0F} : (glm::vec2{0.5F} - billboard.pivot);
  const glm::vec3 middle = billboard.centre + (screenRight * (shift.x * billboard.size.x)) -
                           (screenUp * (shift.y * billboard.size.y));
  const auto clip = [&viewProjection](glm::vec3 world) {
    return viewProjection * glm::vec4{world, 1.0F};
  };
  const std::array<glm::vec4, 4> corners{
      clip(middle - across + along), clip(middle + across + along), clip(middle - across - along),
      clip(middle + across - along)};
  const glm::vec2 uvMin = billboard.texel / texture.size;
  const glm::vec2 uvMax = (billboard.texel + billboard.texels) / texture.size;
  const glm::uvec2 textures{texture.id.index, NoPalette};
  const glm::vec4 color{billboard.color, 1.0F};
  const auto plainStart = static_cast<std::uint32_t>(vertices_.size());
  appendQuad(vertices_, corners, uvMin, uvMax, textures, color);
  if (billboard.blend == SpriteBlend::Opaque) {
    addToRun(SpriteBlend::Opaque, TexelFilter::All, plainStart);
    return;
  }
  addToRun(SpriteBlend::Opaque, TexelFilter::Plain, plainStart);
  const auto semiStart = static_cast<std::uint32_t>(vertices_.size());
  appendQuad(vertices_, corners, uvMin, uvMax, textures,
             {billboard.color, blendAlpha(billboard.blend)});
  addToRun(billboard.blend, TexelFilter::SemiTransparent, semiStart);
}

const rhi::GraphicsPipeline& MapRenderer::pipelineOf(SpriteBlend blend) const {
  switch (blend) {
  case SpriteBlend::Opaque:
    return pipelines_.opaque;
  case SpriteBlend::Average:
    return pipelines_.alpha;
  case SpriteBlend::Add:
  case SpriteBlend::AddQuarter:
    return pipelines_.additive;
  case SpriteBlend::Subtract:
    return pipelines_.subtractive;
  }
  return pipelines_.opaque;
}

void MapRenderer::drawGround(DrawContext& context) {
  if (!hasMap_ || stageCamera_ || groundBillboards_.empty()) {
    return;
  }
  vertices_.clear();
  runs_.clear();
  for (const MapBillboard& billboard : groundBillboards_) {
    appendBillboard(billboard, camera_);
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
  // Sampled nearest, as the billboards over the models (drawLayers).
  constexpr float NearestSampling = 1.0e6F;
  MapPush push{.pixelsPerTexel = NearestSampling,
               .fade = static_cast<float>(fade_),
               .texels = 0,
               .fromRow = fromRow_,
               .toRow = toRow_,
               .screen = rhi::TextureId::InvalidIndex,
               .screenShiftX = 0,
               .screenShiftY = 0};
  for (const Run& run : runs_) {
    push.texels = static_cast<std::uint32_t>(run.texels);
    commands.bindPipeline(pipelineOf(run.blend));
    commands.pushConstants(push);
    commands.draw(run.count, run.first);
  }
}

void MapRenderer::drawLayers(DrawContext& context) {
  if (!hasMap_) {
    return;
  }
  const MapCamera& shown = camera();
  // Far to near, one draw per run of quads with the same blend and texel filter.
  vertices_.clear();
  runs_.clear();
  const glm::mat4 viewProjection = shown.viewProjection();
  const glm::vec3 right{shown.view()[0][0], shown.view()[1][0], shown.view()[2][0]};
  const glm::uvec2 white{renderer_->whiteTexture().index, NoPalette};
  for (const MapMarker& marker : markers_) {
    const glm::vec3 side =
        glm::normalize(glm::vec3{right.x, 0.0F, right.z}) * (marker.width * 0.5F);
    const glm::vec3 up{0.0F, marker.height, 0.0F};
    const auto clip = [&viewProjection](glm::vec3 world) {
      return viewProjection * glm::vec4{world, 1.0F};
    };
    const auto first = static_cast<std::uint32_t>(vertices_.size());
    appendQuad(vertices_,
               {clip(marker.feet - side + up), clip(marker.feet + side + up),
                clip(marker.feet - side), clip(marker.feet + side)},
               {0.0F, 0.0F}, {1.0F, 1.0F}, white, marker.color);
    addToRun(SpriteBlend::Opaque, TexelFilter::All, first);
  }
  for (const MapBillboard& billboard : billboards_) {
    appendBillboard(billboard, shown);
  }
  // HD scenery: a cut-out of the background takes its colours from the background under it; any
  // other sprite smooths its own texels, inside its rectangle of the sheet.
  const bool screen = hdActive() && backgroundColors_.valid() && hdTargetReady(context.extent());
  const auto layerSampling = [this, screen](const DepthLayerQuad& quad,
                                            const assets::MapSpriteSheet::Placement& placement) {
    if (!hdActive() || !backgroundColors_.valid()) {
      return QuadSampling{};
    }
    if (quad.size == glm::vec2{placement.size} && cutouts_.isCutout(quad.sprite, quad.position)) {
      if (screen) {
        return QuadSampling{.mode = Sampling::CutoutScreen};
      }
      return QuadSampling{.mode = Sampling::Cutout,
                          .source = backgroundColors_.index,
                          .sourceMin = quad.position / backgroundSize_,
                          .sourceMax = (quad.position + quad.size) / backgroundSize_,
                          .rect = {0.0F, 0.0F, backgroundSize_ - 1.0F}};
    }
    return QuadSampling{
        .mode = Sampling::Smooth,
        .rect = {glm::vec2{placement.texel}, glm::vec2{placement.texel + placement.size} - 1.0F}};
  };
  // A stage of its own has no depth layers of the map.
  for (const DepthLayerQuad& quad :
       stageCamera_ ? std::span<const DepthLayerQuad>{} : layers_.quads()) {
    if (quad.sprite >= placements_.size() ||
        placements_[quad.sprite].texture == assets::MapSpriteSheet::NoTexture) {
      continue;
    }
    const assets::MapSpriteSheet::Placement& placement = placements_[quad.sprite];
    const SpriteTexture& texture = spriteTextures_[placement.texture];
    const glm::vec2 uvMin = glm::vec2{placement.texel} / texture.size;
    const glm::vec2 uvMax = glm::vec2{placement.texel + placement.size} / texture.size;
    const auto corners =
        backgroundCorners(shown, quad.position, quad.position + quad.size, quad.viewDepth);
    const QuadSampling sampling = layerSampling(quad, placement);
    // Smoothed, a time-of-day sprite reads its colours resolved like the background's.
    const glm::uvec2 textures =
        sampling.mode == Sampling::Smooth && texture.colors.valid()
            ? glm::uvec2{texture.colors.index, NoPalette}
            : glm::uvec2{texture.id.index, texture.timeOfDayPalette ? palettes_.index : NoPalette};
    const auto plainStart = static_cast<std::uint32_t>(vertices_.size());
    appendQuad(vertices_, corners, uvMin, uvMax, textures, glm::vec4{glm::vec3{shade_}, 1.0F},
               sampling);
    if (quad.blend == SpriteBlend::Opaque) {
      addToRun(SpriteBlend::Opaque, TexelFilter::All, plainStart);
      continue;
    }
    // A semi-transparent sprite mixes only its texels with STP; the others are opaque.
    addToRun(SpriteBlend::Opaque, TexelFilter::Plain, plainStart);
    const auto semiStart = static_cast<std::uint32_t>(vertices_.size());
    appendQuad(vertices_, corners, uvMin, uvMax, textures,
               {shade_, shade_, shade_, blendAlpha(quad.blend)}, sampling);
    addToRun(quad.blend, TexelFilter::SemiTransparent, semiStart);
  }
  // Bars where the background does not reach, drawn last over everything (ADR 0020).
  const PixelRect inside = shown.backgroundRect();
  const glm::vec2 viewport = shown.viewportSize();
  const auto toClip = [viewport](glm::vec2 window) {
    return glm::vec4{(window / viewport * 2.0F) - 1.0F, 0.0F, 1.0F};
  };
  const std::array<std::pair<glm::vec2, glm::vec2>, 4> bars{{
      {{0.0F, 0.0F}, {viewport.x, inside.origin.y}},
      {{0.0F, inside.origin.y + inside.size.y}, viewport},
      {{0.0F, inside.origin.y}, {inside.origin.x, inside.origin.y + inside.size.y}},
      {{inside.origin.x + inside.size.x, inside.origin.y},
       {viewport.x, inside.origin.y + inside.size.y}},
  }};
  const auto barsStart = static_cast<std::uint32_t>(vertices_.size());
  for (const auto& [min, max] : bars) {
    if (max.x > min.x && max.y > min.y) {
      appendQuad(vertices_,
                 {toClip(min), toClip({max.x, min.y}), toClip({min.x, max.y}), toClip(max)},
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
  // Sprites are sampled nearest: blending a texel with a transparent or STP neighbour would mix
  // that colour in and leave a dark fringe that passes the alpha test. A huge pixelsPerTexel makes
  // map.slang's blend a step.
  constexpr float NearestSampling = 1.0e6F;
  // The fade the background shows: sprites with clut -1 share its palette, as in the original.
  const glm::uvec2 shift = hdScreenShift();
  MapPush push =
      withGrade(MapPush{.pixelsPerTexel = NearestSampling,
                        .fade = static_cast<float>(timeOfDayFadeShown(fade_)),
                        .texels = 0,
                        .fromRow = fromRow_,
                        .toRow = toRow_,
                        .screen = screen ? hdTarget_.index : rhi::TextureId::InvalidIndex,
                        .screenShiftX = shift.x,
                        .screenShiftY = shift.y},
                grade_);
  for (const Run& run : runs_) {
    push.texels = static_cast<std::uint32_t>(run.texels);
    commands.bindPipeline(pipelineOf(run.blend));
    commands.pushConstants(push);
    commands.draw(run.count, run.first);
  }
  const auto barVertices = static_cast<std::uint32_t>(vertices_.size()) - barsStart;
  if (barVertices > 0) {
    push.texels = static_cast<std::uint32_t>(TexelFilter::All);
    push.graded = 0;
    commands.bindPipeline(pipelines_.background);
    commands.pushConstants(push);
    commands.draw(barVertices, barsStart);
  }
}

} // namespace dwe::render
