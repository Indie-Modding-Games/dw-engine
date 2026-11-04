#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_layers.hpp>
#include <dwe/render/map_renderer.hpp>
#include <dwe/render/map_scene.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using dwe::assets::MapObjectInstance;
using dwe::assets::MapSprite;
using dwe::assets::MapSpriteFrame;
using dwe::assets::MapSpriteSheet;
using dwe::assets::SpriteBlend;
using dwe::render::DepthLayerQuad;
using dwe::render::MapCamera;
using dwe::render::MapDepthLayers;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

MapSprite sprite(std::int32_t texX, std::int32_t clut, glm::ivec3 position = {0, 0, 0},
                 std::int32_t transparency = 4) {
  return {.texX = texX,
          .texY = 8,
          .width = 16,
          .height = 24,
          .position = position,
          .clut = clut,
          .transparency = transparency};
}

const dwe::render::OriginalMapCamera roomCamera{
    .viewpoint = {-650, -800, -1400}, .reference = {0, 0, 0}, .projection = 686, .tiles = {5, 3}};

} // namespace

TEST_CASE(
    "depth layers get the original's ordering-table entries and animate at 20 frames per second") {
  const MapCamera camera{dwe::render::mapCameraDesc(roomCamera)};
  dwe::assets::MapObjects objects;
  objects.sprites = {
      sprite(0, -1, {0, 0, 0}),             // the reference point, far from the camera
      sprite(0, -1, {-1250, -1550, -2700}), // next to the (doubled) eye: the nearest entry
      sprite(0, -1, {0, 10000, 0}),         // someY 10000: just in front of the background
      sprite(0, -1, {100, 0, 100}, 1),
  };
  objects.instances = {
      {.frames = {{.sprite = 0, .time = 2},
                  {.sprite = MapSpriteFrame::BlankSprite, .time = 1},
                  {.sprite = 3, .time = 1}},
       .x = 10,
       .y = 20,
       .flag = 0},
      {.frames = {{.sprite = 1, .time = 1}}, .x = 0, .y = 0, .flag = 0},
      {.frames = {{.sprite = 2, .time = 1}}, .x = 0, .y = 0, .flag = 0},
      {.frames = {{.sprite = 0, .time = 1}}, .x = 0, .y = 0, .flag = MapObjectInstance::HiddenFlag},
      {.frames = {{.sprite = 0, .time = 1}},
       .x = 0,
       .y = 0,
       .flag = MapObjectInstance::ScreenSpaceFlag},
      {.frames = {{.sprite = MapSpriteFrame::BlankSprite, .time = 1}, {.sprite = 3, .time = 1}},
       .x = 0,
       .y = 0,
       .flag = 0},
  };
  MapDepthLayers layers{objects, camera};
  REQUIRE(layers.instanceCount() == 6);

  const float referenceDepth = camera.viewDepth(dwe::render::mapCameraDesc(roomCamera).target);
  CHECK(layers.orderValue(0) == static_cast<std::int32_t>(referenceDepth / 16.0F));
  CHECK(layers.orderValue(1) == 0x23);
  CHECK(layers.orderValue(2) == 0xFFE);
  CHECK(layers.orderValue(4) == 0x1E);
  // The depth of an instance comes from its first sprite that is not blank.
  CHECK(layers.orderValue(5) == dwe::render::mapSpriteOrderValue(objects.sprites[3], camera));

  // Hidden, screen-space and blank instances are not drawn; the rest go from far to near.
  auto quads = layers.quads();
  REQUIRE(quads.size() == 3);
  CHECK(quads[0].instance == 2);
  CHECK(quads[1].instance == 0);
  CHECK(quads[2].instance == 1);
  CHECK(quads[1].position == glm::vec2{10.0F, 20.0F});
  CHECK(quads[1].size == glm::vec2{16.0F, 24.0F});
  CHECK(quads[1].viewDepth == doctest::Approx(static_cast<float>(layers.orderValue(0)) * 16.0F));

  // Frame 0 lasts 2 frames, then the blank one 1 frame, then sprite 3 with its additive blend.
  layers.tick();
  CHECK(std::ranges::count_if(layers.quads(),
                              [](const DepthLayerQuad& quad) { return quad.instance == 0; }) == 1);
  layers.tick();
  CHECK(std::ranges::count_if(layers.quads(),
                              [](const DepthLayerQuad& quad) { return quad.instance == 0; }) == 0);
  layers.update(1.0 / 20.0);
  quads = layers.quads();
  const auto third =
      std::ranges::find_if(quads, [](const DepthLayerQuad& quad) { return quad.instance == 0; });
  REQUIRE(third != quads.end());
  CHECK(third->sprite == 3);
  CHECK(third->blend == SpriteBlend::Add);

  layers.setHidden(0, true);
  CHECK(std::ranges::none_of(layers.quads(),
                             [](const DepthLayerQuad& quad) { return quad.instance == 0; }));
}

TEST_CASE("a restarted animation shows its first frame for a whole original frame") {
  // Elecmon's sparks on gias02 (object 41, frames of 1): routine 21 restarts them from the
  // script; renderMapOverlays draws frame 0 that frame and only then counts it, so the next
  // picture comes a whole frame (3 vblanks) later, whatever was left of the clock.
  const MapCamera camera{dwe::render::mapCameraDesc(roomCamera)};
  dwe::assets::MapObjects objects;
  objects.sprites = {sprite(0, -1, {0, 0, 0}), sprite(0, -1, {0, 0, 0})};
  objects.instances = {{.frames = {{.sprite = 0, .time = 1}, {.sprite = 1, .time = 1}},
                        .x = 10,
                        .y = 20,
                        .flag = 0}};
  MapDepthLayers layers{objects, camera};
  layers.update(0.049);
  layers.restartAnimation(0);
  layers.update(0.017);
  REQUIRE(layers.quads().size() == 1);
  CHECK(layers.quads().front().sprite == 0);
  layers.update(0.017);
  CHECK(layers.quads().front().sprite == 0);
  layers.update(0.017);
  CHECK(layers.quads().front().sprite == 1);
}

TEST_CASE("in one ordering-table entry the instance added last is drawn first") {
  const MapCamera camera{dwe::render::mapCameraDesc(roomCamera)};
  dwe::assets::MapObjects objects;
  objects.sprites = {sprite(0, -1, {0, 0, 0}), sprite(0, -1, {-1250, -1550, -2700})};
  const auto still = [](std::int32_t spriteIndex) {
    return MapObjectInstance{
        .frames = {{.sprite = spriteIndex, .time = 1}}, .x = 0, .y = 0, .flag = 0};
  };
  // Instances 0, 2 and 3 share the reference point's entry; 1 is nearer.
  objects.instances = {still(0), still(1), still(0), still(0)};
  MapDepthLayers layers{objects, camera};
  REQUIRE(layers.orderValue(0) == layers.orderValue(2));
  REQUIRE(layers.orderValue(1) < layers.orderValue(0));
  const auto order = [&layers] {
    std::vector<std::size_t> instances;
    for (const DepthLayerQuad& quad : layers.quads()) {
      instances.push_back(quad.instance);
    }
    return instances;
  };
  CHECK(order() == std::vector<std::size_t>{3, 2, 0, 1});

  // Hiding and showing again keeps each instance's place.
  layers.setHidden(2, true);
  CHECK(order() == std::vector<std::size_t>{3, 0, 1});
  layers.setHidden(0, true);
  layers.setHidden(1, true);
  CHECK(order() == std::vector<std::size_t>{3});
  layers.setHidden(2, false);
  layers.setHidden(1, false);
  CHECK(order() == std::vector<std::size_t>{3, 2, 1});
  // Out-of-range instances are ignored.
  layers.setHidden(99, true);
  CHECK(order() == std::vector<std::size_t>{3, 2, 1});
  // An effect that puts an instance in a near entry draws it last (overworld.c:
  // renderNinjamonEffect: orderValue = 10).
  layers.setOrderValue(3, 10);
  CHECK(layers.orderValue(3) == 10);
  CHECK(order() == std::vector<std::size_t>{2, 1, 3});
}

TEST_CASE("instances start hidden with their hidden flag until shown") {
  const MapCamera camera{dwe::render::mapCameraDesc(roomCamera)};
  dwe::assets::MapObjects objects;
  objects.sprites = {sprite(0, -1)};
  objects.instances = {
      {.frames = {{.sprite = 0, .time = 1}}, .x = 0, .y = 0, .flag = MapObjectInstance::HiddenFlag},
      {.frames = {{.sprite = 0, .time = 1}}, .x = 0, .y = 0, .flag = 0},
  };
  MapDepthLayers layers{objects, camera};
  REQUIRE(layers.quads().size() == 1);
  CHECK(layers.quads()[0].instance == 1);
  layers.setHidden(0, false);
  CHECK(layers.quads().size() == 2);
  // A hidden instance keeps animating, as the original counts its frames.
  layers.setHidden(1, true);
  CHECK(layers.quads().size() == 1);
}

TEST_CASE(
    "a depth-layer sprite covers models from its ordering-table entry back, and none one entry in front") {
  const MapCamera camera{dwe::render::mapCameraDesc(roomCamera)};
  const dwe::render::MapCameraDesc desc = dwe::render::mapCameraDesc(roomCamera);
  const glm::vec3 forward = glm::normalize(desc.target - desc.eye);
  const glm::mat4 viewProjection = camera.viewProjection();
  // The depth a model's vertex at this view depth writes, as the GPU computes it from the camera's
  // matrices.
  const auto modelDepth = [&](float viewDepth) {
    const glm::vec3 world = desc.eye + (forward * viewDepth);
    REQUIRE(camera.viewDepth(world) == doctest::Approx(viewDepth).epsilon(1e-5));
    const glm::vec4 clip = viewProjection * glm::vec4{world, 1.0F};
    return clip.z / clip.w;
  };
  for (const std::int32_t entry : {0x23, 0x100, 0x400, 0xC00, 0xFF5}) {
    CAPTURE(entry);
    const float plane = dwe::render::mapSpriteDepth(entry);
    const glm::vec4 spriteClip = camera.backgroundToClip(desc.backgroundSize * 0.5F, plane);
    const float spriteDepth = spriteClip.z / spriteClip.w;
    CHECK(spriteDepth == doctest::Approx(MapCamera::depthBufferValue(plane)).epsilon(1e-6));
    // The layer pipelines test LessOrEqual: the sprite's texel is drawn over a model where its
    // depth is not greater than the model's.
    const auto covers = [spriteDepth](float model) { return spriteDepth <= model; };
    const float step = dwe::render::OrderingTableStep;
    CHECK(covers(modelDepth(plane + step)));
    CHECK_FALSE(covers(modelDepth(plane - step)));
    // Within an entry, where the depth buffer still tells half a step apart.
    if (entry <= 0x400) {
      CHECK(covers(modelDepth(plane + (step * 0.5F))));
      CHECK_FALSE(covers(modelDepth(plane - (step * 0.5F))));
    }
  }
}

TEST_CASE("the time of day picks the background variant") {
  const dwe::render::TimeOfDaySchedule schedule{.dayStart = 6,
                                                .duskStart = 16,
                                                .nightStart = 20,
                                                .duskLight = {1.0F, 0.7F, 0.7F},
                                                .nightLight = glm::vec3{0.5F}};
  CHECK(dwe::render::timeOfDayAt(5, schedule) == dwe::render::TimeOfDay::Night);
  CHECK(dwe::render::timeOfDayAt(6, schedule) == dwe::render::TimeOfDay::Day);
  CHECK(dwe::render::timeOfDayAt(15, schedule) == dwe::render::TimeOfDay::Day);
  CHECK(dwe::render::timeOfDayAt(16, schedule) == dwe::render::TimeOfDay::Dusk);
  CHECK(dwe::render::timeOfDayAt(19, schedule) == dwe::render::TimeOfDay::Dusk);
  CHECK(dwe::render::timeOfDayAt(20, schedule) == dwe::render::TimeOfDay::Night);
  CHECK(dwe::render::timeOfDayChangesAt(16, schedule));
  CHECK_FALSE(dwe::render::timeOfDayChangesAt(17, schedule));
  CHECK(dwe::render::timeOfDayLightScale(dwe::render::TimeOfDay::Dusk, false, schedule).g ==
        doctest::Approx(0.7F));
  CHECK(dwe::render::timeOfDayLightScale(dwe::render::TimeOfDay::Night, false, schedule).r ==
        doctest::Approx(0.5F));
  CHECK(dwe::render::timeOfDayLightScale(dwe::render::TimeOfDay::Night, true, schedule).r ==
        doctest::Approx(1.0F));
  dwe::render::MapSceneData scene;
  scene.images.backgroundPalettes.resize(3);
  CHECK(scene.backgroundVariant(dwe::render::TimeOfDay::Night, false) == 2);
  CHECK(scene.backgroundVariant(dwe::render::TimeOfDay::Night, true) == 0);
  scene.images.backgroundPalettes.resize(1);
  CHECK(scene.backgroundVariant(dwe::render::TimeOfDay::Dusk, false) == 0);
}

TEST_CASE("the time-of-day fade steps like the original's") {
  using dwe::render::timeOfDayFadeShown;
  CHECK(timeOfDayFadeShown(0.0) == doctest::Approx(1.0 / 6.0));
  CHECK(timeOfDayFadeShown(4.9 / 25.0) == doctest::Approx(1.0 / 6.0));
  CHECK(timeOfDayFadeShown(5.0 / 25.0) == doctest::Approx(1.0 / 5.0));
  CHECK(timeOfDayFadeShown(24.0 / 25.0) == doctest::Approx(1.0 / 2.0));
  CHECK(timeOfDayFadeShown(1.0) == doctest::Approx(1.0));
}

TEST_CASE(
    "the user's disc, extracted: the route's maps load with their backgrounds, pages and depth layers") {
  const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
  if (!mountedContent) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return;
  }
  const std::filesystem::path& content = *mountedContent;
  const dwe::assets::DiskFileSystem disk;
  const auto packs = dwe::assets::findPacks(disk, content);
  REQUIRE_MESSAGE(packs.has_value(), errorOf(packs));
  const auto opened = dwe::assets::openPacks(disk, packs->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE_MESSAGE(opened.has_value(), errorOf(opened));
  const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));

  struct Expected {
    std::string_view id;
    glm::ivec2 size;
    std::size_t variants;
    std::size_t pages;
  };
  for (const Expected& expected :
       {Expected{.id = "twna01", .size = {768, 640}, .variants = 3, .pages = 4},
        Expected{.id = "room08", .size = {640, 384}, .variants = 1, .pages = 3}}) {
    CAPTURE(expected.id);
    const auto scene = dwe::render::loadMapSceneData(*vfs, expected.id);
    REQUIRE_MESSAGE(scene.has_value(), errorOf(scene));
    const dwe::assets::MapImages& images = scene->images;
    CHECK(images.background.width == static_cast<std::uint32_t>(expected.size.x));
    CHECK(images.background.height == static_cast<std::uint32_t>(expected.size.y));
    CHECK(scene->camera.backgroundSize == glm::vec2{expected.size});
    CHECK(images.backgroundPalettes.size() == expected.variants);
    CHECK(images.pages.size() == expected.pages);
    REQUIRE(scene->document.objects.has_value());
    const auto& objects = scene->document.objects.value_or(dwe::assets::MapObjects{});

    // Every sprite finds its page and palette.
    CHECK(std::ranges::count(images.sprites.placements, MapSpriteSheet::NoTexture,
                             &MapSpriteSheet::Placement::texture) == 0);

    const MapCamera camera{scene->camera};
    MapDepthLayers layers{objects, camera};
    CHECK(layers.instanceCount() == objects.instances.size());
    for (std::size_t instance = 0; instance < layers.instanceCount(); ++instance) {
      const std::int32_t order = layers.orderValue(instance);
      CHECK(((order >= 0x23 && order <= 0xFF6) || order == 0xFFE || order == 0x1E));
    }
    MESSAGE(expected.id << ": " << objects.sprites.size() << " sprites, " << layers.instanceCount()
                        << " instances, " << layers.quads().size() << " drawn, "
                        << images.sprites.textures.size() << " sprite textures");
  }
}
