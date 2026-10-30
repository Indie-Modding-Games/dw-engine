#include <dwe/assets/effect_script.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/effect_runner.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
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
  at: "0x0150"
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
  runner.setScreenSize({.x = 427, .y = 240});
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
