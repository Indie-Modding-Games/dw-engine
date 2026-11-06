// HD scenery (docs/modulos/apps/game.md, «Escenarios en alta definición»): the setting, and that it
// changes only the look of the field, never what the player walks into.

#include <dwe/assets/map_images.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_cutouts.hpp>
#include <dwe/scene/world.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "hd_scenery_setting.hpp"
#include "packages/game/field/tests/fixture.hpp"
#include "user_settings.hpp"

namespace dwe::app {
namespace {

using game::field::testing::fill;

// What one tick of the field leaves behind: where the player and the partner stand and face, the
// triggers stepped on, a map change, and where the map's camera shows the player and what a click
// there picks (click-to-move).
struct TickRecord {
  glm::vec3 player{0.0F};
  float yaw = 0.0F;
  glm::vec3 partner{0.0F};
  std::vector<std::int32_t> triggers;
  bool mapChangePending = false;
  std::optional<std::string> mapChange;
  std::optional<glm::vec2> onScreen;
  std::optional<glm::vec3> picked;

  friend bool operator==(const TickRecord&, const TickRecord&) = default;
};

// A walk through a room: east over a script trigger, back west, running, onto a warp, then up into
// a wall, along which the player slides. The game's HD scenery setting is set as given and the
// map's draw is prepared each tick as the renderer prepares it (its camera follows the player; with
// HD scenery its cut-outs are looked up).
std::vector<TickRecord> walk(bool hdScenery) {
  UserSettings settings;
  HdSceneryChanger changer{settings, std::nullopt};
  REQUIRE(changer.change(ui::UiValue{hdScenery}) == hdScenery);

  assets::MapDocument room = game::field::testing::roomMap("room");
  fill(room, {.column = 30, .row = 44}, {.column = 70, .row = 44},
       assets::MapCollision::BlockedBit);
  fill(room, {.column = 50, .row = 50}, {.column = 50, .row = 50}, 112);
  room.warps[2].targetMapId = "house";
  room.warps[2].targetExit = 0;
  fill(room, {.column = 55, .row = 50}, {.column = 55, .row = 50}, 54);

  // A background of 16 x 16 pixels with a sprite cut out of it, for the cut-out lookups.
  assets::MapImages images;
  images.background = {
      .width = 16, .height = 16, .indices = std::vector<std::uint8_t>(256, 3), .palette = {}};
  images.backgroundPalettes.emplace_back(256, assets::PaletteColor{.red = 1, .alpha = 255});
  images.sprites.textures.push_back({.timeOfDayPalette = true,
                                     .indices = {.width = 4,
                                                 .height = 4,
                                                 .indices = std::vector<std::uint8_t>(16, 3),
                                                 .palette = {}},
                                     .colors = {}});
  images.sprites.placements.push_back({.texture = 0, .texel = {0, 0}, .size = {4, 4}});
  render::MapCutouts cutouts{images};
  render::MapCamera camera{render::mapCameraDesc(room), {1280.0F, 720.0F}};

  scene::World world;
  game::field::Field field{world, game::field::testing::originalRules()};
  std::vector<TickRecord> ticks;
  const auto run = [&](game::field::FieldInput input, int count) {
    field.setInput(input);
    for (int tick = 0; tick < count; ++tick) {
      field.tick(1.0F / 60.0F);
      camera.follow(field.player().position);
      if (changer.on()) {
        CHECK(cutouts.isCutout(0, {2.0F, 2.0F}));
      }
      TickRecord record{.player = field.player().position,
                        .yaw = field.player().yaw,
                        .partner = field.partner().position,
                        .triggers = {},
                        .mapChangePending = field.mapChangePending(),
                        .mapChange = std::nullopt,
                        .onScreen = camera.worldToWindow(field.player().position),
                        .picked = std::nullopt};
      for (const game::field::TriggerStep& step : field.triggerSteps()) {
        record.triggers.push_back(step.trigger);
      }
      if (const auto change = field.takeMapChange()) {
        record.mapChange = change->map;
      }
      if (record.onScreen) {
        record.picked = camera.pickGround(*record.onScreen);
      }
      ticks.push_back(std::move(record));
    }
  };
  // From the warp (spawning there fires nothing): east over the trigger, back west onto the warp.
  field.enterMapAt(room, {.position = {50.0F, 0.0F, 50.0F}, .yaw = 0.0F});
  run({.stick = {1.0F, 0.0F}}, 120);
  run({.stick = {-1.0F, 0.0F}, .run = true}, 60);
  // From the middle, up into the wall, along which the player slides.
  field.enterMapAt(room, {.position = {0.0F, 0.0F, 0.0F}, .yaw = std::numbers::pi_v<float>});
  run({.stick = {0.0F, 1.0F}, .run = true}, 60);
  return ticks;
}

} // namespace

TEST_CASE("HD scenery is on unless the player turned it off, and the choice is saved") {
  CHECK(startHdScenery(UserSettings{}));
  const auto read = parseUserSettings("hd_scenery: false\n");
  REQUIRE(read.has_value());
  CHECK_FALSE(startHdScenery(*read));

  const test_support::TempFolder folder;
  const std::filesystem::path file = folder.path() / "settings.yaml";
  UserSettings settings;
  HdSceneryChanger changer{settings, file};
  CHECK(changer.on());
  CHECK_FALSE(changer.change(ui::UiValue{false}));
  CHECK(loadUserSettings(file).hdScenery == std::optional<bool>{false});
  // Anything but a boolean changes nothing.
  CHECK_FALSE(changer.change(ui::UiValue{std::string{"on"}}));
  CHECK(changer.change(ui::UiValue{true}));
  CHECK(loadUserSettings(file).hdScenery == std::optional<bool>{true});
}

TEST_CASE("HD scenery changes no collision, trigger, map change or camera of a walk") {
  const std::vector<TickRecord> off = walk(false);
  const std::vector<TickRecord> on = walk(true);
  REQUIRE(off.size() == on.size());
  // The walk did meet the wall, the trigger and the warp.
  bool slid = false;
  bool stepped = false;
  bool warped = false;
  for (const TickRecord& tick : off) {
    slid = slid || tick.player.x > 0.0F;
    stepped = stepped || !tick.triggers.empty();
    warped = warped || tick.mapChangePending;
  }
  CHECK(slid);
  CHECK(stepped);
  CHECK(warped);
  for (std::size_t tick = 0; tick < off.size(); ++tick) {
    CAPTURE(tick);
    CHECK(on[tick] == off[tick]);
  }
}

} // namespace dwe::app
