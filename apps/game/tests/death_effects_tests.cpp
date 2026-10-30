#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "death_effects.hpp"

using dwe::app::BodyFace;
using dwe::app::DeathEffects;
using dwe::app::DeathPartner;
using dwe::app::DeathShard;
using dwe::app::EvolutionBody;
using dwe::app::ScenePrim;
using dwe::app::ScenePrimBlend;

namespace {

// A made-up body of five bones a metre apart up the y axis, each with sixteen small triangles, one
// for each place in the wireframe table.
class MadeUpBody final : public EvolutionBody {
public:
  [[nodiscard]] std::optional<glm::vec3> bonePoint(std::uint32_t bone) const override {
    return glm::vec3{0.0F, 100.0F * static_cast<float>(bone), 0.0F};
  }
  [[nodiscard]] std::vector<BodyFace> boneFaces(std::uint32_t bone) const override {
    std::vector<BodyFace> faces;
    const float y = 100.0F * static_cast<float>(bone);
    for (std::uint32_t index = 0; index < FacesPerBone; ++index) {
      const float x = 20.0F + (10.0F * static_cast<float>(index));
      BodyFace face{.bone = bone, .index = index, .count = 3};
      face.corners = {glm::vec3{x, y, 0.0F}, glm::vec3{x, y + 10.0F, 0.0F},
                      glm::vec3{x + 10.0F, y, 0.0F}, glm::vec3{0.0F}};
      face.normal = glm::vec3{0.0F, 0.0F, 1.0F};
      faces.push_back(face);
    }
    return faces;
  }
  [[nodiscard]] std::uint32_t boneCount() const override { return Bones; }

  static constexpr std::uint32_t Bones = 5;
  static constexpr std::uint32_t FacesPerBone = 16;
};

constexpr DeathPartner Partner{.feet = glm::vec3{0.0F}, .height = 300.0F};

// Runs DOOA_tickDissolve's effects from frame 1 to `last`, ticking the objects after each frame.
void dissolveTo(DeathEffects& effects, const EvolutionBody& body, std::int32_t last) {
  for (std::int32_t frame = 1; frame <= last; ++frame) {
    effects.dissolveStep(frame, body, Partner);
    effects.tickObjects();
  }
}

} // namespace

TEST_CASE("the iris closes from 200 to 0 by its end and stays closed") {
  CHECK(dwe::app::irisRadius(0, 0x23, 0) == (200 << 12) / 128);
  CHECK(dwe::app::irisRadius(0, 0x23, 0x23) == 0);
  CHECK(dwe::app::irisRadius(0, 0x21, 0x23) == 0);
  CHECK(dwe::app::irisRadius(0, 0x23, 0x10) < dwe::app::irisRadius(0, 0x23, 0x0F));
  // The partner is drawn over it once a sixteenth of its radius fits in the partner's box.
  CHECK(dwe::app::irisWithinPartner(6400, 400.0F));
  CHECK_FALSE(dwe::app::irisWithinPartner(6400, 399.0F));
}

TEST_CASE("the iris is a ring and four rectangles subtracted, only the side ones when closed") {
  const dwe::rhi::TextureId spot{7};
  const std::vector<ScenePrim> open = dwe::app::irisPrims(glm::vec4{0.0F, 0.0F, 0.5F, 1.0F},
                                                          dwe::app::irisRadius(0, 0x23, 0), spot);
  CHECK(open.size() == 8);
  CHECK(std::ranges::all_of(open, [](const ScenePrim& prim) {
    return prim.blend == ScenePrimBlend::Subtractive && prim.texture == dwe::rhi::TextureId{7};
  }));
  // The ring's half side is (radius * 63 >> 10) + 1 original pixels.
  CHECK(open.front().corners[0].offset.x == doctest::Approx(-((6400 * 63 >> 10) + 1)));
  const std::vector<ScenePrim> closed =
      dwe::app::irisPrims(glm::vec4{0.0F, 0.0F, 0.5F, 1.0F}, 0, spot);
  CHECK(closed.size() == 2);
}

TEST_CASE("the body turns to wireframe in waves from 0x69 to 0x8C and is whole again at 0xD1") {
  const MadeUpBody body;
  DeathEffects effects;
  effects.start(0x3039);
  dissolveTo(effects, body, 0x68);
  CHECK(effects.bodyShare() == 16);
  CHECK(effects.shards().empty());
  effects.dissolveStep(0x69, body, Partner);
  CHECK(effects.bodyShare() == 15);
  effects.tickObjects();
  for (std::int32_t frame = 0x6A; frame <= 0x8C; ++frame) {
    effects.dissolveStep(frame, body, Partner);
    effects.tickObjects();
  }
  CHECK(effects.bodyShare() == 0);
  // Bones 0 and 1 do not break; of the others' 48 faces, rand() & 3 keeps about three in four.
  const std::size_t broken = effects.shards().size();
  CHECK(broken > 24);
  CHECK(broken < 48);
  // The shards fell to the ground.
  CHECK(std::ranges::all_of(effects.shards(),
                            [](const DeathShard& shard) { return shard.centre.y <= 0; }));
  for (std::int32_t frame = 0x8D; frame <= 0xD1; ++frame) {
    effects.dissolveStep(frame, body, Partner);
    effects.tickObjects();
  }
  CHECK(effects.bodyShare() == 16);
}

TEST_CASE("a flash a frame comes down from 0xD2 to 0x135, then the shards flicker") {
  const MadeUpBody body;
  DeathEffects effects;
  effects.start(0x3039);
  dissolveTo(effects, body, 0xD1);
  CHECK(effects.flashes().empty());
  effects.dissolveStep(0xD2, body, Partner);
  REQUIRE(effects.flashes().size() == 1);
  // 100 over the partner's 300 high head.
  CHECK(effects.flashes().front().position.y == doctest::Approx(400.0F).epsilon(0.02));
  effects.tickObjects();
  for (std::int32_t frame = 0xD3; frame <= 0x134; ++frame) {
    effects.dissolveStep(frame, body, Partner);
    effects.tickObjects();
  }
  // Each lives 8 frames.
  CHECK(effects.flashes().size() == 7);
  CHECK_FALSE(effects.flicker());
  effects.dissolveStep(0x135, body, Partner);
  CHECK(effects.flicker());
  CHECK(effects.flashes().back().position.y == doctest::Approx(0.0F));
  for (int tick = 0; tick < 8; ++tick) {
    effects.tickObjects();
  }
  CHECK(effects.flashes().empty());
}

TEST_CASE("the rebirth spirals the shards up, closes them in and fills the egg in") {
  const MadeUpBody body;
  DeathEffects effects;
  effects.start(0x3039);
  dissolveTo(effects, body, 0x1B7);
  REQUIRE_FALSE(effects.shards().empty());
  for (std::int32_t frame = 1; frame <= 99; ++frame) {
    effects.rebirthStep(frame, 0, nullptr);
    effects.tickObjects();
  }
  CHECK(effects.eggShown());
  CHECK(effects.shardState() == 1);
  CHECK(effects.eggShare() == 0);
  for (const DeathShard& shard : effects.shards()) {
    // Shards far from the axis may have a target under 4096 and stay as they are.
    CHECK(shard.radius >= 4096);
    CHECK(shard.spin > 0);
    CHECK(shard.centre.y <= 0);
  }
  for (std::int32_t frame = 100; frame <= 131; ++frame) {
    effects.rebirthStep(frame, 0, nullptr);
    effects.tickObjects();
  }
  CHECK(effects.shardState() == 3);
  CHECK(effects.eggShare() == 16);
  // Every shard has shrunk to the egg.
  CHECK(std::ranges::all_of(effects.shards(),
                            [](const DeathShard& shard) { return shard.radius == 0; }));
  effects.rebirthStep(132, 0, nullptr);
  CHECK(effects.shards().empty());
  effects.rebirthStep(133, 0, nullptr);
  CHECK(effects.streaks() > 0);
  CHECK(effects.flashes().size() == 1);
  effects.rebirthStep(147, 0, nullptr);
  CHECK(effects.streaks() == 0);
}

TEST_CASE("the egg breaks a piece on frames 207, 217, 227, 237, 247 and 248") {
  // An egg of four slots, six pieces each, one triangle a piece.
  std::vector<std::vector<BodyFace>> egg;
  for (std::uint32_t object = 0; object < 24; ++object) {
    BodyFace face{.bone = object, .index = 0, .count = 3};
    face.corners = {glm::vec3{0.0F, 100.0F, 0.0F}, glm::vec3{10.0F, 100.0F, 0.0F},
                    glm::vec3{0.0F, 110.0F, 0.0F}, glm::vec3{0.0F}};
    face.normal = glm::vec3{0.0F, 0.0F, 1.0F};
    egg.push_back({face});
  }
  DeathEffects effects;
  effects.start(0x3039);
  std::vector<std::int32_t> breaks;
  for (std::int32_t frame = 1; frame <= 290; ++frame) {
    effects.rebirthStep(frame, 2, &egg);
    if (effects.takePieceBreaks() > 0) {
      breaks.push_back(frame);
    }
    if (frame == 248) {
      // The first piece's 41 frames ended on frame 247.
      CHECK(effects.eggShards().size() == 5);
    }
    effects.tickObjects();
  }
  CHECK(breaks == std::vector<std::int32_t>{207, 217, 227, 237, 247, 248});
  // A broken piece lives 41 frames.
  CHECK(effects.eggShards().empty());
}
