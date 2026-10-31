#pragma once

#include <dwe/assets/image.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_layers.hpp>
#include <dwe/render/map_scene.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/pipeline.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace dwe::render {

// An upright, camera-facing marker standing on a world point, drawn with depth like a model: for
// debugging the camera and the depth layers before the models exist.
struct MapMarker {
  glm::vec3 feet{0.0F};
  float width = 60.0F;
  float height = 160.0F;
  glm::vec4 color{1.0F, 0.2F, 0.6F, 1.0F};
};

// A textured quad in the world, drawn with depth after the markers and before the depth layers:
// upright and facing the camera, centred on its point, as the original's items on the ground
// (item.c: renderOverworldItem), or flat on the ground around it, as their shadows
// (renderDroppedItemShadow).
struct MapBillboard {
  glm::vec3 centre{0.0F};
  // World units: the width and height of an upright one, the x and z sides of a flat one.
  glm::vec2 size{0.0F};
  bool flat = false;
  // A texture of addBillboardTexture, and the rectangle of it drawn, in texels.
  std::size_t texture = 0;
  glm::vec2 texel{0.0F};
  glm::vec2 texels{0.0F};
  // A semi-transparent one mixes its texels with STP with this blend and draws the others opaque,
  // as the depth-layer sprites.
  assets::SpriteBlend blend = assets::SpriteBlend::Opaque;
  // Multiplies the texels' colours: the original's 0x80 is 1.
  glm::vec3 color{1.0F};
  // An upright one's point on the centre, as fractions of its size from its top-left corner, and
  // its turn on screen around it, clockwise in radians (GsSPRITE mx, my and rotate).
  glm::vec2 pivot{0.5F};
  float turn = 0.0F;
};

// How long the original fades the background palette when the time of day changes: 25 frames of its
// 20 per second (map.c: tickDaytimeTransition).
inline constexpr double TimeOfDayFadeSeconds = 25.0 / MapDepthLayers::FramesPerSecond;

// How far toward the new palette the original shows a fade that is `progress` (0 to 1) through:
// map.c: tickDaytimeTransition moves each colour A - (A - B) / (6 - frame / 5) of the way from the
// old palette, so the fade steps through 1/6, 1/5, 1/4, 1/3 and 1/2, five frames each, and jumps to
// the new palette at the end.
double timeOfDayFadeShown(double progress);

// Draws a field map (docs/formats/mapas.md, ADR 0020): the prerendered background of the current
// time of day, then, after the models drawn with camera(), the depth-layer sprites with depth so
// they cover the models behind them, and black bars where the background does not reach. Put
// background() first in the scene pass, the models next and layers() last.
class MapRenderer {
public:
  // The renderer must outlive the map renderer.
  static core::Result<std::unique_ptr<MapRenderer>> create(Renderer& renderer);

  MapRenderer(const MapRenderer&) = delete;
  MapRenderer& operator=(const MapRenderer&) = delete;
  MapRenderer(MapRenderer&&) = delete;
  MapRenderer& operator=(MapRenderer&&) = delete;
  ~MapRenderer();

  // Queues the uploads of a map's background, palettes and sprite textures and sets up its camera
  // and depth layers, showing the palette variant `variant` (MapSceneData::backgroundVariant).
  // Nothing waits for the GPU: call it between frames, and the map draws in the next one. Fails,
  // keeping no map, when the map has no background palette or a texture cannot be made.
  core::Result<void> setMap(const MapSceneData& scene, std::size_t variant);

  // Switches to another palette variant, fading over fadeSeconds (0 switches at once). A switch
  // during a fade continues from the colours shown at that moment.
  void setVariant(std::size_t variant, double fadeSeconds = TimeOfDayFadeSeconds);
  // Multiplies the background's and the depth layers' colours, from 1 (as they are) to 0 (black):
  // a scene fading the map's palettes to black.
  void setShade(float shade) { shade_ = shade; }

  // Advances the sprite animations by animationSeconds and the palette fade by fadeSeconds (a game
  // may pause one and not the other), and sets the world point the camera centres on. The camera
  // takes the size of the frame's targets when backgroundDrawer() prepares, before the drawers
  // after it.
  void update(double animationSeconds, double fadeSeconds, glm::vec3 worldFocus);

  // Markers drawn by layerDrawer() before the depth layers, so the layers cover them as they cover
  // models.
  // The camera's shake (MapCamera::setShake), in background pixels.
  void setShake(glm::vec2 offset) { camera_.setShake(offset); }
  void setMarkers(std::span<const MapMarker> markers) {
    markers_.assign(markers.begin(), markers.end());
  }

  // Uploads a texture for billboards: RGBA whose alpha is 0 (not drawn), 128 (STP) or 255. It
  // stays across maps until the map renderer goes. Returns its index for MapBillboard::texture.
  core::Result<std::size_t> addBillboardTexture(const assets::Image& image);

  // Billboards drawn by layerDrawer() after the markers, in this order, until replaced.
  void setBillboards(std::span<const MapBillboard> billboards) {
    billboards_.assign(billboards.begin(), billboards.end());
  }

  [[nodiscard]] bool hasMap() const { return hasMap_; }

  // A camera of a stage of its own instead of the map's (a tournament arena: std_setup.c:
  // STD_initializeTournament): while set, camera() is it, centred on its background, and neither
  // the background nor the depth layers are drawn (the models stand on the cleared frame); the
  // markers, the billboards and the bars stay. Unset, the map's camera is back.
  void setStageCamera(const std::optional<MapCameraDesc>& desc);
  // The map's camera, or the stage's: the view and projection models must be drawn with. Only
  // valid with a map; sized to the frame being drawn once backgroundDrawer() has prepared it.
  [[nodiscard]] const MapCamera& camera() const { return stageCamera_ ? *stageCamera_ : camera_; }

  [[nodiscard]] MapDepthLayers& layers() { return layers_; }

  [[nodiscard]] Drawer& backgroundDrawer() { return background_; }
  [[nodiscard]] Drawer& layerDrawer() { return foreground_; }

private:
  class BackgroundDrawer final : public Drawer {
  public:
    explicit BackgroundDrawer(MapRenderer& owner) : owner_(&owner) {}
    void prepare(PrepareContext& context) override { owner_->prepare(context); }
    void draw(DrawContext& context) override { owner_->drawBackground(context); }

  private:
    MapRenderer* owner_;
  };

  class LayerDrawer final : public Drawer {
  public:
    explicit LayerDrawer(MapRenderer& owner) : owner_(&owner) {}
    void draw(DrawContext& context) override { owner_->drawLayers(context); }

  private:
    MapRenderer* owner_;
  };

  struct Pipelines {
    rhi::GraphicsPipeline background;
    rhi::GraphicsPipeline opaque;
    rhi::GraphicsPipeline alpha;
    rhi::GraphicsPipeline additive;
    rhi::GraphicsPipeline subtractive;
  };

  // A texture of the sprite sheet: its id, its size, and whether its texels index the background's
  // palettes.
  struct SpriteTexture {
    TextureId id;
    glm::vec2 size{0.0F};
    bool timeOfDayPalette = false;
  };

  // One vertex of map.slang.
  struct Vertex {
    glm::vec4 position;
    glm::vec2 uv;
    glm::uvec2 textures;
    glm::vec4 color;
  };

  // How a semi-transparent sprite's texels are drawn (map.slang): all drawn ones, those without STP
  // or those with.
  enum class TexelFilter : std::uint8_t { All = 0, Plain = 1, SemiTransparent = 2 };

  // A run of layer quads drawn with one pipeline and texel filter.
  struct Run {
    assets::SpriteBlend blend;
    TexelFilter texels;
    std::uint32_t first;
    std::uint32_t count;
  };

  MapRenderer(Renderer& renderer, Pipelines pipelines, TextureId black);

  core::Result<TextureId> makeTexture(rhi::Extent extent, std::span<const std::byte> pixels);
  void releaseMap();
  // Writes the colours shown now to the snapshot row of the palette texture, to fade from them.
  bool snapshotPalette();
  void prepare(PrepareContext& context);
  void drawBackground(DrawContext& context);
  void drawLayers(DrawContext& context);
  void addToRun(assets::SpriteBlend blend, TexelFilter texels, std::uint32_t first);
  void warnUploadFailure(std::size_t bytes);

  Renderer* renderer_;
  Pipelines pipelines_;
  TextureId black_;
  BackgroundDrawer background_{*this};
  LayerDrawer foreground_{*this};

  // The default camera until a map is set; hasMap_ says whether it is one.
  MapCamera camera_{MapCameraDesc{}};
  // setStageCamera's.
  std::optional<MapCamera> stageCamera_;
  glm::vec3 focus_{0.0F};
  bool hasMap_ = false;
  MapDepthLayers layers_;
  // The background's palette indices; the palette texture has a row of 256 colours per variant and
  // a last row, the snapshot, with the colours of an interrupted fade.
  TextureId backgroundIndices_{};
  TextureId palettes_{};
  std::vector<std::vector<assets::PaletteColor>> paletteRows_;
  std::vector<SpriteTexture> spriteTextures_;
  std::vector<assets::MapSpriteSheet::Placement> placements_;
  std::vector<MapMarker> markers_;
  std::vector<SpriteTexture> billboardTextures_;
  std::vector<MapBillboard> billboards_;
  glm::vec2 backgroundSize_{0.0F};
  // The palette rows faded between: fade_ 0 shows fromRow_, 1 toRow_.
  std::uint32_t fromRow_ = 0;
  std::uint32_t toRow_ = 0;
  double fade_ = 1.0;
  float shade_ = 1.0F;
  double fadeSeconds_ = 0.0;
  // Per-frame buffers, kept to draw without allocating.
  std::vector<Vertex> vertices_;
  std::vector<Run> runs_;
  bool uploadWarned_ = false;
};

} // namespace dwe::render
