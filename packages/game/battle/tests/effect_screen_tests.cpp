#include <dwe/assets/effect_script.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/effect_runner.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using dwe::assets::EffectScript;
using dwe::game::battle::EffectBlend;
using dwe::game::battle::EffectRunner;
using dwe::game::battle::EffectScreenPoint;
using dwe::game::battle::EffectScreenQuad;

namespace {

class NoSounds final : public dwe::game::battle::EffectSounds {
public:
  void play(std::size_t /*slot*/, std::int32_t /*sound*/) override {}
  std::optional<std::int64_t> playHeld(std::size_t /*slot*/, std::int32_t /*sound*/) override {
    return std::nullopt;
  }
  void stop(std::int64_t /*voice*/) override {}
};

// A made-up technique whose one sub-effect runs `render` (instructions from 0x0122 on) as its
// render script every frame, over `data` (data items after the instance table).
EffectScript renderingScript(std::string_view render, std::string_view data) {
  constexpr std::string_view EffectYaml = R"(move: "made_up_screen"
header:
  sub_effect_count: 1
)";
  const std::string scriptYaml = std::string{R"(start_script:
- label: "start"
  at: "0x0100"
  op: spawn
  count: 1
  table: "base+0x0010"
  stride: 28
  ordinal: 0
- at: "0x0102"
  op: push_address
  address: "base+0x0120"
- at: "0x0104"
  op: push_address
  address: "base+0x0122"
- at: "0x0106"
  op: sub
  sub: set_sub_effect_scripts
  args:
    tick_script: "l_0120"
    render_script: "l_0122"
- at: "0x0108"
  op: return
- label: "l_0120"
  at: "0x0120"
  op: return
- label: "l_0122"
)"} + std::string{render} + R"(
init_script:
- label: "init"
  at: "0x01f0"
  op: stop
data:
- at: "0x0010"
  kind: instances
  count: 1
  stride: 28
  free: true
)" + std::string{data};
  const auto effect = dwe::core::parseYaml(EffectYaml);
  const auto script = dwe::core::parseYaml(scriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE_MESSAGE(script.has_value(), (script ? std::string{} : script.error().message));
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  return std::move(*read);
}

// The screen quads of one frame of a made-up technique.
std::vector<EffectScreenQuad> screenQuads(EffectRunner& runner) {
  std::vector<EffectScreenQuad> quads;
  for (const auto& draw : runner.draws()) {
    if (const auto* quad = std::get_if<EffectScreenQuad>(&draw.shape)) {
      quads.push_back(*quad);
    }
  }
  return quads;
}

std::vector<EffectScreenQuad> firstFrame(EffectRunner& runner, const EffectScript& script) {
  const std::size_t slot = runner.load(script);
  REQUIRE(runner.start(slot, 0, 1).has_value());
  runner.tick();
  runner.render();
  return screenQuads(runner);
}

// Winter Blast's tile offsets and tiles (base+0x0122 and base+0x012a), at 0x40 and 0x48.
constexpr std::string_view TilesData = R"(- at: "0x0040"
  kind: tile_offsets
  size: 8
  values: [0, 0, 128, 160]
- at: "0x0048"
  kind: tile_map
  size: 99
  tiles:
  - [3, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3]
  - [1, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1]
  - [3, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3]
  - [2, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2]
  - [3, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3]
  - [1, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1]
  - [3, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3]
  - [3, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2]
  - [3, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3]
)";

// render_scrolling_background with scroll (37, -5) and brightness 60.
constexpr std::string_view ScrollingRender = R"(  at: "0x0122"
  op: push_address
  address: "base+0x0048"
- at: "0x0124"
  op: push_address
  address: "base+0x0040"
- at: "0x0126"
  op: push_immediate
  value: 37
- at: "0x0128"
  op: push_immediate
  value: -5
- at: "0x012a"
  op: push_immediate
  value: 60
- at: "0x012c"
  op: sub
  sub: render_scrolling_background
  args:
    tile_map: "&base+0x0048"
    tile_offsets: "&base+0x0040"
    scroll_x: 37
    scroll_y: -5
    brightness: 60
- at: "0x012e"
  op: return)";

} // namespace

TEST_CASE("a scrolling background lays its 11 by 9 tiles over the screen, wrapped by its scroll") {
  NoSounds sounds;
  EffectRunner runner{sounds};
  const std::vector<EffectScreenQuad> quads =
      firstFrame(runner, renderingScript(ScrollingRender, TilesData));
  REQUIRE(quads.size() == 99);
  // Scroll x 37: the second column of tiles first, 5 pixels right of 0xc0 left of the centre.
  // Scroll y -5 wraps to 251: the eighth row first, 27 pixels below 0x98 up.
  const EffectScreenQuad& first = quads.front();
  CHECK(first.corners[0] == EffectScreenPoint{.x = -187, .y = -125});
  CHECK(first.corners[3] == EffectScreenPoint{.x = -155, .y = -93});
  // Row 7, column 1: tile 2, whose offset is 128; added in grey 60, 31 texels a side.
  CHECK(first.u == 128);
  CHECK(first.v == 0);
  CHECK(first.texelWidth == 31);
  CHECK(first.color == dwe::game::battle::EffectVector{.x = 60, .y = 60, .z = 60});
  CHECK(first.blend == EffectBlend::Add);
  CHECK(first.order == 0x1E);
  CHECK(quads[1].u == 160); // row 7, column 2: tile 3
  // The last: row 6, column 0 (tile 3, offset 160), 10 and 8 tiles on.
  const EffectScreenQuad& last = quads.back();
  CHECK(last.corners[0] == EffectScreenPoint{.x = 133, .y = 131});
  CHECK(last.u == 160);
}

TEST_CASE("a scrolling background repeats its tiles out to a wider screen's edges") {
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.screen = {.x = 427, .y = 240};
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads =
      firstFrame(runner, renderingScript(ScrollingRender, TilesData));
  // Two more columns each side, the same rows.
  REQUIRE(quads.size() == 15 * 9);
  CHECK(quads.front().corners[0] == EffectScreenPoint{.x = -251, .y = -125});
  // The grid wraps: columns 10 and 0 of row 7 (tiles 2 and 3) come before column 1.
  CHECK(quads[0].u == 128);
  CHECK(quads[1].u == 160);
  CHECK(quads[2].corners[0] == EffectScreenPoint{.x = -187, .y = -125});
}

TEST_CASE("a scrolling background is placed from the projection's centre") {
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.centre = {.x = 40, .y = -10};
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads =
      firstFrame(runner, renderingScript(ScrollingRender, TilesData));
  // The original's 11 x 9 tiles, 40 right and 10 up of where they are round the screen's centre.
  REQUIRE(quads.size() == 11 * 9);
  CHECK(quads.front().corners[0] == EffectScreenPoint{.x = -147, .y = -135});
}

TEST_CASE("parallax sprites move by their depth times the offset and wrap as the original's do") {
  constexpr std::string_view Render = R"(  at: "0x0122"
  op: push_address
  address: "base+0x00a0"
- at: "0x0124"
  op: push_immediate
  value: 10
- at: "0x0126"
  op: push_immediate
  value: -300
- at: "0x0128"
  op: sub
  sub: render_parallax_sprites
  args:
    sprites: "&base+0x00a0"
    offset_x: 10
    offset_y: -300
- at: "0x012a"
  op: return)";
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: parallax_sprites
  size: 22
  entries:
  - {size: 32, x: 200, y: 200, depth: 243, u: 32}
  - {size: 32, x: 250, y: 0, depth: 115, u: 96}
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  const std::vector<EffectScreenQuad> quads = firstFrame(runner, renderingScript(Render, Data));
  REQUIRE(quads.size() == 2);
  // 243 * 210 >> 7 = 398, 0xc8 left: 198; 243 * -100 >> 7 = -190, + 280, 0xa0 up: -70; side
  // 32 * 243 >> 8 = 30.
  CHECK(quads[0].corners[0] == EffectScreenPoint{.x = 198, .y = -70});
  CHECK(quads[0].corners[3] == EffectScreenPoint{.x = 228, .y = -40});
  CHECK(quads[0].u == 32);
  CHECK(quads[0].clutRow == 1);
  CHECK(quads[0].blend == EffectBlend::Opaque);
  // 115 * 260 >> 7 = 233: 33; 115 * -300 >> 7 = -270, + 280: -150; side 14.
  CHECK(quads[1].corners[0] == EffectScreenPoint{.x = 33, .y = -150});
  CHECK(quads[1].corners[3] == EffectScreenPoint{.x = 47, .y = -136});
  CHECK(quads[1].u == 96);
}

namespace {

// A made-up technique with a ribbon on its instance (stride 0x80, the points at instance+0x1c),
// turned by the camera's rotation when `facing`; its tick moves the points and its render draws
// the ribbon at scale 1.
EffectScript ribbonScript(bool facing) {
  constexpr std::string_view EffectYaml = R"(move: "made_up_aurora"
header:
  sub_effect_count: 1
)";
  const std::string turn = facing ? R"(- at: "0x010a"
  op: push_address
  address: "instance+0x0010"
- at: "0x010c"
  op: sub
  sub: get_camera_rotation
  args:
    out: "&instance+0x0010"
)"
                                  : "";
  const std::string scriptYaml = std::string{R"(start_script:
- label: "start"
  at: "0x0100"
  op: spawn
  count: 1
  table: "base+0x0060"
  stride: 128
  ordinal: 0
- at: "0x0102"
  op: push_address
  address: "base+0x0220"
- at: "0x0104"
  op: push_address
  address: "base+0x0230"
- at: "0x0106"
  op: sub
  sub: set_sub_effect_scripts
  args:
    tick_script: "l_0220"
    render_script: "l_0230"
- at: "0x0108"
  op: sub
  sub: init_transform
)"} + turn + R"(- at: "0x0110"
  op: push_address
  address: "instance+0x001c"
- at: "0x0112"
  op: sub
  sub: init_ribbon_points
  args:
    points: "&instance+0x001c"
- at: "0x0114"
  op: return
- label: "l_0220"
  at: "0x0220"
  op: push_address
  address: "instance+0x001c"
- at: "0x0222"
  op: sub
  sub: tick_ribbon_points
  args:
    points: "&instance+0x001c"
- at: "0x0224"
  op: return
- label: "l_0230"
  at: "0x0230"
  op: push_address
  address: "instance+0x001c"
- at: "0x0232"
  op: push_address
  address: "base+0x0040"
- at: "0x0234"
  op: push_immediate
  value: 0
- at: "0x0236"
  op: sub
  sub: render_ribbon
  args:
    points: "&instance+0x001c"
    scale: "&base+0x0040"
    unknown0: 0
- at: "0x0238"
  op: return
init_script:
- label: "init"
  at: "0x0250"
  op: stop
data:
- at: "0x0040"
  kind: vector
  values: [4096, 4096, 4096]
- at: "0x0060"
  kind: instances
  count: 1
  stride: 128
  free: true
)";
  const auto effect = dwe::core::parseYaml(EffectYaml);
  const auto script = dwe::core::parseYaml(scriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE_MESSAGE(script.has_value(), (script ? std::string{} : script.error().message));
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  return std::move(*read);
}

std::optional<dwe::game::battle::EffectRibbon> ribbonOf(const EffectRunner& runner) {
  for (const auto& draw : runner.draws()) {
    if (const auto* ribbon = std::get_if<dwe::game::battle::EffectRibbon>(&draw.shape)) {
      return *ribbon;
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("a ribbon's points sway within their bounds and its colour fades in round its three") {
  using dwe::game::battle::EffectRibbon;
  using dwe::game::battle::EffectVector;
  NoSounds sounds;
  EffectRunner runner{sounds};
  const std::size_t slot = runner.load(ribbonScript(false));
  REQUIRE(runner.start(slot, 0, 1).has_value());
  for (int frame = 1; frame <= 4; ++frame) {
    runner.tick();
  }
  runner.render();
  const std::optional<EffectRibbon> ribbon = ribbonOf(runner);
  REQUIRE(ribbon.has_value());
  for (std::size_t i = 0; i < EffectRibbon::Points; ++i) {
    // x from -0x708, 0x190 apart; y and z within 0x96; the strip 0x1f4 above and 0xc8 below.
    CHECK(ribbon->top.at(i).x == -0x708 + (static_cast<std::int32_t>(i) * 0x190));
    CHECK(ribbon->bottom.at(i).y - ribbon->top.at(i).y == 0x1F4 + 0xC8);
    CHECK(ribbon->top.at(i).y + 0x1F4 >= -0x96);
    CHECK(ribbon->top.at(i).y + 0x1F4 <= 0x96);
    CHECK(ribbon->top.at(i).z >= -0x96);
    CHECK(ribbon->top.at(i).z <= 0x96);
  }
  // Frame 4: the first colour 4 tenths of the way to the second (0x7c to 0: 75, 0x7c, 0x7c), half
  // faded in (2048).
  CHECK(ribbon->color == EffectVector{.x = 37, .y = 62, .z = 62});
}

TEST_CASE("a ribbon turned by the camera's rotation faces the camera") {
  using dwe::game::battle::EffectRibbon;
  NoSounds sounds;
  EffectRunner runner{sounds};
  // A camera turned a quarter round y (RotMatrixY 1024).
  dwe::game::battle::EffectView view;
  view.camera.m = {{{0, 0, 4096}, {0, 4096, 0}, {-4096, 0, 0}}};
  runner.setView(view);
  const std::size_t slot = runner.load(ribbonScript(true));
  REQUIRE(runner.start(slot, 0, 1).has_value());
  runner.tick();
  runner.render();
  const std::optional<EffectRibbon> ribbon = ribbonOf(runner);
  REQUIRE(ribbon.has_value());
  // The view-to-world rotation turns the strip's x into z (within the angles' rounding:
  // matrixToEuler2's arctangent).
  CHECK(std::abs(ribbon->top.front().x) <= 8);
  CHECK(std::abs(ribbon->top.front().z + 0x708) <= 8);
  CHECK(std::abs(ribbon->top.back().z - 0x708) <= 8);
}

namespace {

// One instruction of a made-up script at `address`, then the next free address.
std::string instruction(std::int32_t& address, std::string_view body) {
  std::string out = std::format("- at: \"0x{:04x}\"\n{}\n", address, body);
  address += 2;
  return out;
}

// render_screen_sprite (its args only count them) of a 16 x 32 sprite at u 96 pivoting on (8, 31),
// with the given place, turn, entry, scale x and palette row; a value given as a string is a
// variable of the data.
using SpriteArgument = std::variant<std::int32_t, std::string_view>;
std::string screenSprite(std::int32_t& address, SpriteArgument x, SpriteArgument y,
                         std::int32_t turn, SpriteArgument entry, std::int32_t scaleX,
                         std::int32_t row) {
  std::string out;
  for (const SpriteArgument& value :
       std::array<SpriteArgument, 13>{x, y, 96, 0, 16, 32, 8, 31, turn, entry, scaleX, 4096, row}) {
    if (const auto* variable = std::get_if<std::string_view>(&value)) {
      out += instruction(
          address, std::format("  op: push_variable\n  variable: \"{}\"\n  width: 4", *variable));
    } else {
      out += instruction(
          address, std::format("  op: push_immediate\n  value: {}", std::get<std::int32_t>(value)));
    }
  }
  return out + instruction(address, "  op: sub\n  sub: render_screen_sprite\n  args:\n"
                                    "    x: 0\n    y: 0\n    u_offset: 0\n    v_offset: 0\n"
                                    "    width: 0\n    height: 0\n    pivot_x: 0\n    pivot_y: 0\n"
                                    "    rotate: 0\n    depth: 0\n    scale_x: 0\n    scale_y: 0\n"
                                    "    clut_row: 0");
}

// A render script (from 0x0122) of `body`'s instructions and a return.
std::string renderOf(const auto& body) {
  std::int32_t address = 0x0122;
  std::string out = body(address);
  out += instruction(address, "  op: return");
  // renderingScript gives the first line's dash.
  return "  " + out.substr(2);
}

} // namespace

TEST_CASE("a screen sprite is scaled and turned round its pivot, from the projection's centre or "
          "the screen's") {
  const std::string render = renderOf([](std::int32_t& at) {
    return screenSprite(at, 20, 30, 0, 100, 8192, 0) +
           screenSprite(at, 20, 30, 90, 3000, 4096, -5) +
           screenSprite(at, 20, 30, 0, 4096, 4096, 0);
  });
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.centre = {.x = 10, .y = -4};
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads = firstFrame(runner, renderingScript(render, ""));
  // The third sorts past the ordering table's last entry: it is not drawn.
  REQUIRE(quads.size() == 2);
  // Twice as wide round the pivot, from the projection's centre (10, -4).
  CHECK(quads[0].corners[0] == EffectScreenPoint{.x = 14, .y = -5});
  CHECK(quads[0].corners[3] == EffectScreenPoint{.x = 46, .y = 27});
  CHECK(quads[0].u == 96);
  CHECK(quads[0].texelWidth == 16);
  CHECK(quads[0].texelHeight == 32);
  CHECK(quads[0].clutRow == 0);
  CHECK(quads[0].blend == EffectBlend::Add);
  CHECK(quads[0].order == 100);
  // A quarter turn clockwise: the top left corner (-8, -31) from the pivot goes to (31, -8); a
  // negative row is the screen's centre and palette row 5.
  CHECK(quads[1].corners[0] == EffectScreenPoint{.x = 51, .y = 22});
  CHECK(quads[1].corners[3] == EffectScreenPoint{.x = 19, .y = 38});
  CHECK(quads[1].clutRow == 5);
  CHECK(quads[1].order == 3000);
}

TEST_CASE("a point projects to the screen as the original's perspective does") {
  const std::string render = renderOf([](std::int32_t& at) {
    std::string out;
    for (const std::string_view address :
         {"base+0x00a0", "base+0x00b0", "base+0x00b4", "base+0x00b8"}) {
      out += instruction(at, std::format("  op: push_address\n  address: \"{}\"", address));
    }
    out += instruction(at, "  op: sub\n  sub: project_to_screen\n  args:\n    position: 0\n"
                           "    x: 0\n    y: 0\n    depth: 0");
    return out + screenSprite(at, "base+0x00b0", "base+0x00b4", 0, "base+0x00b8", 4096, -1);
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: vector
  size: 12
  values: [100, -50, 0]
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.camera.t = {.x = 0, .y = 0, .z = 1000};
  view.projection = 512;
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads = firstFrame(runner, renderingScript(render, Data));
  REQUIRE(quads.size() == 1);
  // H / SZ = 512 / 1000 in 16.16 (33554): x 100 -> 51 and y -50 -> -26, the pivot (8, 31) from
  // there; the entry is SZ >> 4.
  CHECK(quads[0].corners[0] == EffectScreenPoint{.x = 43, .y = -57});
  CHECK(quads[0].order == 62);
}

TEST_CASE("a screen fade darkens the whole screen behind everything; an overlay veils it from "
          "the projection's centre") {
  const std::string render = renderOf([](std::int32_t& at) {
    std::string out = instruction(at, "  op: push_immediate\n  value: 64");
    out += instruction(at, "  op: sub\n  sub: render_screen_fade\n  args:\n    brightness: 0");
    out += instruction(at, "  op: push_immediate\n  value: 10");
    out += instruction(at, "  op: push_address\n  address: \"base+0x00a0\"");
    out += instruction(at, "  op: push_immediate\n  value: 6");
    return out + instruction(at, "  op: sub\n  sub: render_screen_overlay\n  args:\n    depth: 0\n"
                                 "    color: 0\n    blend: 0");
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: color
  size: 12
  values: [10, 20, 30]
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.screen = {.x = 427, .y = 240};
  view.centre = {.x = 5, .y = -3};
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads = firstFrame(runner, renderingScript(render, Data));
  REQUIRE(quads.size() == 2);
  // The fade: the screen's corners (53 more each side of the wider screen), 64 / 128 of white
  // subtracted at entry 0xffe.
  CHECK_FALSE(quads[0].textured);
  CHECK(quads[0].corners[0] == EffectScreenPoint{.x = -213, .y = -120});
  CHECK(quads[0].corners[3] == EffectScreenPoint{.x = 213, .y = 120});
  CHECK(quads[0].color == dwe::game::battle::EffectVector{.x = 127, .y = 127, .z = 127});
  CHECK(quads[0].blend == EffectBlend::Subtract);
  CHECK(quads[0].order == 0xFFE);
  // The overlay: from the projection's centre, its colour, mode 6 (semi-transparent, rate 2) at
  // entry 10.
  CHECK(quads[1].corners[0] == EffectScreenPoint{.x = -208, .y = -123});
  CHECK(quads[1].color == dwe::game::battle::EffectVector{.x = 10, .y = 20, .z = 30});
  CHECK(quads[1].blend == EffectBlend::Subtract);
  CHECK(quads[1].order == 10);
}

namespace {

std::vector<dwe::game::battle::EffectScreenLine> screenLines(const EffectRunner& runner) {
  std::vector<dwe::game::battle::EffectScreenLine> lines;
  for (const auto& draw : runner.draws()) {
    if (const auto* line = std::get_if<dwe::game::battle::EffectScreenLine>(&draw.shape)) {
      lines.push_back(*line);
    }
  }
  return lines;
}

std::string pushes(std::int32_t& at, std::initializer_list<std::string_view> values) {
  std::string out;
  for (const std::string_view value : values) {
    out += instruction(at, value.starts_with("base")
                               ? std::format("  op: push_address\n  address: \"{}\"", value)
                               : std::format("  op: push_immediate\n  value: {}", value));
  }
  return out;
}

// copy_vector of the zero vector at base+0x00f0 into the instance's place and angles.
std::string zeroInstance(std::int32_t& at);

std::string subOf(std::int32_t& at, std::string_view name, std::size_t count) {
  std::string out = std::format("  op: sub\n  sub: {}\n  args:", name);
  for (std::size_t i = 0; i < count; ++i) {
    out += std::format("\n    a{}: 0", i);
  }
  return instruction(at, out);
}

std::string zeroInstance(std::int32_t& at) {
  std::string out;
  for (const std::string_view offset : {"instance+0x0004", "instance+0x0010"}) {
    out += instruction(at, std::format("  op: push_address\n  address: \"{}\"", offset)) +
           pushes(at, {"base+0x00f0"}) + subOf(at, "copy_vector", 2);
  }
  return out;
}

} // namespace

TEST_CASE("a line is drawn from the projection's centre, four times round at half the colour "
          "with flag 0x20") {
  const std::string render = renderOf([](std::int32_t& at) {
    return pushes(at, {"10", "20", "30", "40", "base+0x00a0", "100", "0"}) +
           subOf(at, "render_line", 7) +
           pushes(at, {"10", "20", "30", "40", "base+0x00a0", "100", "32"}) +
           subOf(at, "render_line", 7) +
           pushes(at, {"10", "20", "30", "40", "base+0x00a0", "32", "0"}) +
           subOf(at, "render_line", 7);
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: color
  size: 12
  values: [200, 101, 7]
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.centre = {.x = 5, .y = -3};
  runner.setView(view);
  firstFrame(runner, renderingScript(render, Data));
  const auto lines = screenLines(runner);
  // The last sorts at entry 0x20, which is not drawn.
  REQUIRE(lines.size() == 5);
  CHECK(lines[0].from == EffectScreenPoint{.x = 15, .y = 17});
  CHECK(lines[0].to == EffectScreenPoint{.x = 35, .y = 37});
  CHECK(lines[0].color == dwe::game::battle::EffectVector{.x = 200, .y = 101, .z = 7});
  CHECK(lines[0].blend == EffectBlend::Opaque);
  CHECK(lines[0].order == 100);
  CHECK(lines[1].from == EffectScreenPoint{.x = 16, .y = 17});
  CHECK(lines[2].from == EffectScreenPoint{.x = 15, .y = 18});
  CHECK(lines[1].color == dwe::game::battle::EffectVector{.x = 100, .y = 50, .z = 3});
  CHECK(lines[1].blend == EffectBlend::Add);
}

TEST_CASE("wireframes project through the fixed effect camera from the screen's centre until it "
          "is restored") {
  const std::string render = renderOf([](std::int32_t& at) {
    std::string out = subOf(at, "setup_fixed_camera", 0);
    out += pushes(at, {"base+0x00d0"}) + subOf(at, "get_viewport_distance_2", 1);
    // A 1000 x 1000 square at y 0 through the origin, two lines each way.
    out += pushes(at, {"base+0x00a0", "base+0x00b0", "base+0x00c0", "-500", "500", "1", "-500",
                       "500", "1", "0"}) +
           subOf(at, "render_wireframe_grid", 10);
    out += subOf(at, "restore_camera", 0);
    return out + pushes(at, {"base+0x00a0", "base+0x00a0", "base+0x00c0"}) +
           subOf(at, "render_wireframe_box", 3);
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: vector
  size: 12
  values: [0, 0, 0]
- at: "0x00b0"
  kind: vector
  size: 12
  values: [0, 0, 0]
- at: "0x00c0"
  kind: color
  size: 12
  values: [1, 2, 3]
- at: "0x00d0"
  kind: int32
  size: 4
  value: 0
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.centre = {.x = 40, .y = 40};
  view.projection = 900;
  view.camera.t = {.x = 0, .y = 0, .z = 1000};
  runner.setView(view);
  firstFrame(runner, renderingScript(render, Data));
  const auto lines = screenLines(runner);
  // Two lines each way, then the box's 12 edges through the battle's camera: a zero box at the
  // origin is the screen's centre.
  REQUIRE(lines.size() == 4 + 12);
  // The fixed camera (H 512, 2000 away): (-500, 0, -500) at depth 1500 is at x -171 and
  // (-500, 0, 500) at depth 2500 at x -103, whatever the projection's centre.
  CHECK(lines[0].from == EffectScreenPoint{.x = -171, .y = 0});
  CHECK(lines[0].to == EffectScreenPoint{.x = -103, .y = 0});
  CHECK(lines[0].order == 0xFFD);
  CHECK(lines[4].from == EffectScreenPoint{.x = 0, .y = 0});
  CHECK(lines[4].order == 0x22);
  CHECK(lines[4].color == dwe::game::battle::EffectVector{.x = 1, .y = 2, .z = 3});
}

TEST_CASE("palette loads rewrite a row of the effect's palette from a colour on, and keep it") {
  const std::string render = renderOf([](std::int32_t& at) {
    return pushes(at, {"10000", "2", "base+0x00a0", "3"}) + subOf(at, "queue_clut_load", 4) +
           pushes(at, {"3", "14", "3", "base+0x00e0"}) + subOf(at, "load_clut_colors", 4) +
           pushes(at, {"4", "0", "17", "base+0x00e0"}) + subOf(at, "load_clut_colors", 4);
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: clut_row
  size: 40
  colors: ["0x0001", "0x0002", "0x0003", "0x0004", "0x0005", "0x0006", "0x0007", "0x0008",
           "0x0009", "0x000a", "0x000b", "0x000c", "0x000d", "0x000e", "0x000f", "0x0010",
           "0x0011", "0x0012", "0x0013", "0x8014"]
- at: "0x00e0"
  kind: clut_colors
  size: 24
  colors:
  - [31, 0, 0, 1]
  - [0, 31, 0, 0]
  - [1, 2, 3, 0]
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  const std::size_t slot = runner.load(renderingScript(render, Data));
  REQUIRE(runner.start(slot, 0, 1).has_value());
  runner.tick();
  runner.render();
  const dwe::game::battle::EffectPalettes& palettes = runner.palettes(slot);
  // Row 2: the 16 colours from the fourth on.
  REQUIRE(palettes.contains(2));
  CHECK(palettes.at(2).colors[0] == 0x0004);
  CHECK(palettes.at(2).colors[15] == 0x0013);
  // Row 3: three colours from the 15th, red with STP and green; the third falls past the row.
  REQUIRE(palettes.contains(3));
  CHECK_FALSE(palettes.at(3).colors[13].has_value());
  CHECK(palettes.at(3).colors[14] == 0x801F);
  CHECK(palettes.at(3).colors[15] == 0x03E0);
  // A count past 16 loads nothing.
  CHECK_FALSE(palettes.contains(4));
  // The rows stay as the scripts left them.
  runner.tick();
  CHECK(runner.palettes(slot).size() == 2);
}

TEST_CASE("a particle flash draws its texels four times round its point, mirrored, in two rows") {
  const std::string render = renderOf([](std::int32_t& at) {
    return pushes(at, {"10", "20", "128", "0", "32", "32", "1024", "100", "1"}) +
           subOf(at, "render_particle_flash", 9) +
           pushes(at, {"10", "20", "128", "0", "32", "32", "1024", "32", "1"}) +
           subOf(at, "render_particle_flash", 9) +
           pushes(at, {"400", "20", "128", "0", "32", "32", "1024", "100", "1"}) +
           subOf(at, "render_particle_flash", 9);
  });
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.centre = {.x = 5, .y = -5};
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads = firstFrame(runner, renderingScript(render, ""));
  // Only the first: the second sorts in front of entry 0x21 and the third is off the screen.
  REQUIRE(quads.size() == 8);
  // 32 pixels a side; the top left copy's upper row, 16 high, its texels' upper half.
  CHECK(quads[0].corners[0] == EffectScreenPoint{.x = -17, .y = -17});
  CHECK(quads[0].corners[3] == EffectScreenPoint{.x = 15, .y = -1});
  CHECK(quads[0].u == 128);
  CHECK(quads[0].texelWidth == 31);
  CHECK(quads[0].texelHeight == 15);
  CHECK(quads[0].blend == EffectBlend::Add);
  CHECK(quads[0].clutRow == 1);
  CHECK(quads[0].order == 100);
  // Its lower row ends on the point.
  CHECK(quads[1].corners[0] == EffectScreenPoint{.x = -17, .y = -1});
  CHECK(quads[1].corners[3] == EffectScreenPoint{.x = 15, .y = 15});
  CHECK(quads[1].v == 16);
  // The copy to the right starts at its far edge: its texels run the other way.
  CHECK(quads[2].corners[0] == EffectScreenPoint{.x = 47, .y = -17});
  CHECK(quads[2].corners[1] == EffectScreenPoint{.x = 15, .y = -17});
  // The copy below starts at its bottom edge.
  CHECK(quads[4].corners[0] == EffectScreenPoint{.x = -17, .y = 47});
  CHECK(quads[6].corners[0] == EffectScreenPoint{.x = 47, .y = 47});
}

TEST_CASE("an effect without sub-effects never runs its scripts") {
  constexpr std::string_view EffectYaml = R"(move: "made_up_empty"
header:
  sub_effect_count: 0
)";
  // Both scripts work on a sub-effect, which it has none of.
  constexpr std::string_view ScriptYaml = R"(start_script:
- label: "start"
  at: "0x0100"
  op: sub
  sub: hit_target
- at: "0x0102"
  op: stop
init_script:
- label: "init"
  at: "0x0110"
  op: sub
  sub: hit_target
- at: "0x0112"
  op: stop
data: []
)";
  const auto effect = dwe::core::parseYaml(EffectYaml);
  const auto script = dwe::core::parseYaml(ScriptYaml);
  REQUIRE(effect.has_value());
  REQUIRE(script.has_value());
  auto read = dwe::assets::readEffectScript(*effect, *script);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  NoSounds sounds;
  EffectRunner runner{sounds};
  const std::size_t slot = runner.load(std::move(*read));
  CHECK_FALSE(runner.start(slot, 0, 1).has_value());
  runner.tick();
  runner.render();
  CHECK_FALSE(runner.running(slot));
  CHECK(runner.draws().empty());
}

TEST_CASE("battle_effect.c BTL_render2DTexturedQuad: a textured quad from a place and size or from "
          "four points, from the projection's centre, mixed by its rate") {
  const std::string render = renderOf([](std::int32_t& at) {
    // x, y, width, height, entry, colour, u, v, texel width and height, palette row, rate.
    std::string out =
        pushes(at, {"10", "20", "30", "40", "256", "base+0x00a0", "1", "2", "3", "4", "2", "64"}) +
        subOf(at, "render_2d_quad", 12);
    out += pushes(at, {"base+0x00b0", "base+0x00c0", "base+0x00d0", "base+0x00e0", "300",
                       "base+0x00a0", "0", "0", "31", "31", "0", "-32"}) +
           subOf(at, "render_2d_quad", 12);
    // Entry 0x20 is too near: nothing.
    return out +
           pushes(at, {"0", "0", "8", "8", "32", "base+0x00a0", "0", "0", "8", "8", "0", "0"}) +
           subOf(at, "render_2d_quad", 12);
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: color
  size: 12
  values: [100, 90, 80]
- at: "0x00b0"
  kind: vector
  size: 12
  values: [-5, -6, 0]
- at: "0x00c0"
  kind: vector
  size: 12
  values: [7, -6, 0]
- at: "0x00d0"
  kind: vector
  size: 12
  values: [-5, 8, 0]
- at: "0x00e0"
  kind: vector
  size: 12
  values: [9, 8, 0]
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.centre = {.x = 5, .y = 6};
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads = firstFrame(runner, renderingScript(render, Data));
  REQUIRE(quads.size() == 2);
  CHECK(quads[0].corners == std::array<EffectScreenPoint, 4>{EffectScreenPoint{.x = 15, .y = 26},
                                                             EffectScreenPoint{.x = 45, .y = 26},
                                                             EffectScreenPoint{.x = 15, .y = 66},
                                                             EffectScreenPoint{.x = 45, .y = 66}});
  CHECK(quads[0].u == 1);
  CHECK(quads[0].v == 2);
  CHECK(quads[0].texelWidth == 3);
  CHECK(quads[0].texelHeight == 4);
  CHECK(quads[0].clutRow == 2);
  CHECK(quads[0].blend == EffectBlend::Subtract);
  CHECK(quads[0].order == 256);
  CHECK(quads[0].color == dwe::game::battle::EffectVector{.x = 100, .y = 90, .z = 80});
  CHECK(quads[1].corners[0] == EffectScreenPoint{.x = 0, .y = 0});
  CHECK(quads[1].corners[3] == EffectScreenPoint{.x = 14, .y = 14});
  CHECK(quads[1].blend == EffectBlend::Add);
}

TEST_CASE("battle_effect.c BTL_render3DTexturedQuad: four world points projected, at their average "
          "depth") {
  const std::string render = renderOf([](std::int32_t& at) {
    return pushes(at, {"base+0x00b0", "base+0x00c0", "base+0x00d0", "base+0x00e0", "base+0x00a0",
                       "0", "0", "15", "15", "1", "32"}) +
           subOf(at, "render_3d_quad", 11);
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: color
  size: 12
  values: [128, 128, 128]
- at: "0x00b0"
  kind: vector
  size: 12
  values: [-100, -100, 0]
- at: "0x00c0"
  kind: vector
  size: 12
  values: [100, -100, 0]
- at: "0x00d0"
  kind: vector
  size: 12
  values: [-100, 100, 0]
- at: "0x00e0"
  kind: vector
  size: 12
  values: [100, 100, 0]
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.camera.t = {.x = 0, .y = 0, .z = 1000};
  view.projection = 512;
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads = firstFrame(runner, renderingScript(render, Data));
  REQUIRE(quads.size() == 1);
  CHECK(quads[0].corners == std::array<EffectScreenPoint, 4>{EffectScreenPoint{.x = -52, .y = -52},
                                                             EffectScreenPoint{.x = 51, .y = -52},
                                                             EffectScreenPoint{.x = -52, .y = 51},
                                                             EffectScreenPoint{.x = 51, .y = 51}});
  CHECK(quads[0].order == 62);
  CHECK(quads[0].clutRow == 1);
  CHECK(quads[0].blend == EffectBlend::Add);
}

TEST_CASE("battle_effect.c BTL_renderRingTube: ten points a ring, a quad between each two rings, "
          "every other ring's texels 24 to the right") {
  const std::string render = renderOf([](std::int32_t& at) {
    // Centre, two rings, radii, heights, grey levels, first ring's texels.
    return pushes(at, {"base+0x00a0", "2", "base+0x00b0", "base+0x00b8", "base+0x00c0", "1"}) +
           subOf(at, "render_ring_tube", 6);
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: vector
  size: 12
  values: [0, 0, 0]
- at: "0x00b0"
  kind: int32_array
  size: 8
  values: [100, 100]
- at: "0x00b8"
  kind: int32_array
  size: 8
  values: [0, -100]
- at: "0x00c0"
  kind: int32_array
  size: 8
  values: [77, 0]
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.camera.t = {.x = 0, .y = 0, .z = 1000};
  view.projection = 512;
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads = firstFrame(runner, renderingScript(render, Data));
  REQUIRE(quads.size() == 10);
  // The first point of a ring is at +z (_sin(0x80)): 1100 deep, the centre of the screen; the
  // next ring's is 100 above it.
  CHECK(quads[0].corners[3] == EffectScreenPoint{.x = 0, .y = 0});
  CHECK(quads[0].corners[2] == EffectScreenPoint{.x = 0, .y = -47});
  CHECK(quads[0].order == 68);
  CHECK(quads[0].u == 24);
  CHECK(quads[0].texelWidth == 0x17);
  CHECK(quads[0].texelHeight == 0x1F);
  CHECK(quads[0].color == dwe::game::battle::EffectVector{.x = 77, .y = 77, .z = 77});
  CHECK(quads[0].blend == EffectBlend::Add);
  // The last quad closes the ring back to its first points.
  CHECK(quads[9].corners[0] == quads[0].corners[2]);
  CHECK(quads[9].corners[1] == quads[0].corners[3]);
}

TEST_CASE("battle_effect.c BTL_renderRadialWaves: six rays of 27 quads out from the instance, the "
          "first and last quads with their own texels") {
  const std::string render = renderOf([](std::int32_t& at) {
    return zeroInstance(at) + pushes(at, {"base+0x00a0"}) + subOf(at, "render_radial_waves", 1);
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: vector
  size: 12
  values: [4096, 4096, 4096]
- at: "0x00f0"
  kind: vector
  size: 12
  values: [0, 0, 0]
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.camera.t = {.x = 0, .y = 0, .z = 10000};
  view.projection = 512;
  runner.setView(view);
  const std::vector<EffectScreenQuad> quads = firstFrame(runner, renderingScript(render, Data));
  REQUIRE(quads.size() == 6 * 27);
  CHECK(quads[0].u == 8);
  CHECK(quads[0].texelWidth == 0xF);
  CHECK(quads[1].u == 0);
  CHECK(quads[1].v == 0x10);
  CHECK(quads[26].u == 8);
  for (const EffectScreenQuad& quad : quads) {
    CHECK(quad.blend == EffectBlend::Add);
    CHECK(quad.clutRow == 1);
  }
  // The first ray is turned 0x2aa round y (to the right and behind), its quads 1/5 of their
  // radius wide.
  CHECK(quads[0].corners[0].x > 0);
}

TEST_CASE("battle_effect.c BTL_disableMapLayer hides the map until the next tick; "
          "BTL_drawTMDScreenSpace places a model in front of the camera") {
  const std::string render = renderOf([](std::int32_t& at) {
    return zeroInstance(at) + subOf(at, "disable_map_layer", 0) + pushes(at, {"0", "base+0x00a0"}) +
           subOf(at, "draw_tmd_on_screen", 2);
  });
  constexpr std::string_view Data = R"(- at: "0x00a0"
  kind: vector
  size: 12
  values: [4096, 4096, 4096]
- at: "0x00f0"
  kind: vector
  size: 12
  values: [0, 0, 0]
)";
  NoSounds sounds;
  EffectRunner runner{sounds};
  dwe::game::battle::EffectView view;
  view.camera.t = {.x = 0, .y = 0, .z = 1000};
  view.centre = {.x = 10, .y = 0};
  runner.setView(view);
  EffectScript script = renderingScript(render, Data);
  script.modelObjects = 1;
  firstFrame(runner, script);
  CHECK(runner.mapHidden());
  REQUIRE(runner.draws().size() == 1);
  const auto* model = std::get_if<dwe::game::battle::EffectModel>(&runner.draws()[0].shape);
  REQUIRE(model != nullptr);
  // The instance's place (0, 0, 0) in view space, 10 left for the drawing offset: in the world,
  // 1000 toward the camera.
  CHECK(model->transform.t == dwe::game::battle::EffectVector{.x = -10, .y = 0, .z = -1000});
  CHECK(model->transform.m[0][0] == 4096);
  runner.tick();
  CHECK_FALSE(runner.mapHidden());
}
