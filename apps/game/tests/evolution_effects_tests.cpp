#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "evolution_effects.hpp"

using dwe::app::BodyFace;
using dwe::app::EvolutionBody;
using dwe::app::EvolutionEffects;
using dwe::app::EvolutionView;
using dwe::app::ScenePrim;

namespace {

// A made-up body of six bones a metre apart up the y axis, each with one triangle except bone 3,
// which has no object.
class MadeUpBody final : public EvolutionBody {
public:
  [[nodiscard]] std::optional<glm::vec3> bonePoint(std::uint32_t bone) const override {
    return glm::vec3{0.0F, 100.0F * static_cast<float>(bone), 0.0F};
  }
  [[nodiscard]] std::vector<BodyFace> boneFaces(std::uint32_t bone) const override {
    if (bone == 3 || bone >= Bones) {
      return {};
    }
    const float y = 100.0F * static_cast<float>(bone);
    BodyFace face{.bone = bone, .index = 0, .count = 3};
    face.corners = {glm::vec3{0.0F, y, 0.0F}, glm::vec3{0.0F, y + 50.0F, 0.0F},
                    glm::vec3{50.0F, y, 0.0F}, glm::vec3{0.0F}};
    face.normal = glm::vec3{0.0F, 0.0F, 1.0F};
    return {face};
  }
  [[nodiscard]] std::uint32_t boneCount() const override { return Bones; }

  static constexpr std::uint32_t Bones = 6;
};

constexpr std::int32_t BurstFrame = 0x6A;
constexpr std::int32_t SwapFrame = 0x11C;

// Runs the scene's state 2 from the burst to `last`, ticking the objects after each frame.
std::vector<std::int32_t> runTo(EvolutionEffects& effects, const EvolutionBody& body,
                                std::int32_t last) {
  std::vector<std::int32_t> breaks;
  effects.burst(BurstFrame, body);
  if (!effects.takeSounds().empty()) {
    breaks.push_back(BurstFrame);
  }
  effects.tickObjects();
  for (std::int32_t frame = BurstFrame + 1; frame <= last; ++frame) {
    effects.step(frame, body);
    if (!effects.takeSounds().empty()) {
      breaks.push_back(frame);
    }
    effects.tickObjects();
  }
  return breaks;
}

} // namespace

TEST_CASE("the evolution breaks every bone with an object once, in a random order") {
  MadeUpBody body;
  EvolutionEffects effects;
  effects.start(MadeUpBody::Bones, glm::vec3{0.0F}, 0x3039);
  std::vector<std::uint32_t> order = effects.order();
  std::ranges::sort(order);
  CHECK(order == std::vector<std::uint32_t>{1, 2, 3, 4, 5});

  // EVL_D_80063EFC: the burst (0x6A), then 0x70 and 0x80, 0x8A, 0x92...; bone 3 is passed over.
  const std::vector<std::int32_t> breaks = runTo(effects, body, 0x100);
  CHECK(breaks == std::vector<std::int32_t>{0x6A, 0x70, 0x80, 0x8A});
  for (const std::uint32_t bone : {1U, 2U, 4U, 5U}) {
    CHECK(effects.boneWired(bone));
  }
  CHECK_FALSE(effects.boneWired(3));
  CHECK_FALSE(effects.boneWired(0));
}

TEST_CASE("the evolution's objects live as long as the original's") {
  MadeUpBody body;
  EvolutionEffects effects;
  effects.start(MadeUpBody::Bones, glm::vec3{0.0F}, 1);
  effects.burst(BurstFrame, body);
  REQUIRE(effects.shards().size() == 1);
  REQUIRE(effects.sparks().size() == 1);
  REQUIRE(effects.flashes().size() == 1);
  // The ray lasts until the new form shows; the shards 0x1F frames; the flash 10.
  CHECK(effects.sparks().front().timer == SwapFrame - BurstFrame);
  for (int tick = 0; tick < 9; ++tick) {
    effects.tickObjects();
  }
  CHECK(effects.flashes().size() == 1);
  effects.tickObjects();
  CHECK(effects.flashes().empty());
  for (int tick = 10; tick < 0x1F; ++tick) {
    effects.tickObjects();
  }
  CHECK(effects.shards().size() == 1);
  CHECK(effects.shards().front().timer == 0x1F);
  effects.tickObjects();
  CHECK(effects.shards().empty());
}

TEST_CASE("a new evolution starts with none of the last one's objects and keeps the caps") {
  // A made-up body of 40 bones, each with one triangle: more than the 30 shard sets and 16 rays.
  class ManyBones final : public EvolutionBody {
  public:
    [[nodiscard]] std::optional<glm::vec3> bonePoint(std::uint32_t bone) const override {
      return glm::vec3{0.0F, static_cast<float>(bone), 0.0F};
    }
    [[nodiscard]] std::vector<BodyFace> boneFaces(std::uint32_t bone) const override {
      BodyFace face{.bone = bone, .index = 0, .count = 3};
      face.normal = glm::vec3{0.0F, 0.0F, 1.0F};
      return {face};
    }
    [[nodiscard]] std::uint32_t boneCount() const override { return 40; }
  };
  ManyBones body;
  EvolutionEffects effects;
  effects.start(body.boneCount(), glm::vec3{0.0F}, 1);
  // EVL_initShardSets and EVL_resetSparks: 30 shard sets and 16 rays at most, whatever breaks.
  for (std::uint32_t bone = 1; bone < body.boneCount(); ++bone) {
    effects.burst(BurstFrame, body);
  }
  CHECK(effects.shards().size() == 30);
  CHECK(effects.sparks().size() == 16);
  for (std::int32_t frame = 0x6C; frame < 0x6C + 18; ++frame) {
    effects.step(frame, body);
  }
  REQUIRE_FALSE(effects.rings().empty());
  REQUIRE_FALSE(effects.flashes().empty());

  // EVL_initEvoSequence: EVL_initShardSets, EVL_resetParticles and EVL_resetSparks free every
  // table, so the next scene draws nothing of this one.
  effects.start(body.boneCount(), glm::vec3{0.0F}, 2);
  CHECK(effects.shards().empty());
  CHECK(effects.sparks().empty());
  CHECK(effects.rings().empty());
  CHECK(effects.flashes().empty());
  CHECK_FALSE(effects.boneWired(1));
}

TEST_CASE("the evolution's squares spawn nine frames in eighteen and fade over 0x56") {
  MadeUpBody body;
  EvolutionEffects effects;
  effects.start(MadeUpBody::Bones, glm::vec3{0.0F}, 1);
  for (std::int32_t frame = 0x6C; frame < 0x6C + 18; ++frame) {
    effects.step(frame, body);
  }
  // 0x6C % 18 is 0: frames 0 to 8 of the pattern spawn, green from 0x32 up to 0xE6 and back.
  REQUIRE(effects.rings().size() == 9);
  CHECK(effects.rings()[0].color == glm::ivec3{0x0A, 0x32, 0x0A});
  CHECK(effects.rings()[4].color == glm::ivec3{0x0A, 0xE6, 0x0A});
  for (int tick = 0; tick < 0x56; ++tick) {
    effects.tickObjects();
  }
  CHECK(effects.rings().size() == 9);
  effects.tickObjects();
  CHECK(effects.rings().empty());
}

TEST_CASE("the new form fills in from wireframe after it shows") {
  MadeUpBody body;
  EvolutionEffects effects;
  effects.start(MadeUpBody::Bones, glm::vec3{0.0F}, 1);
  static_cast<void>(runTo(effects, body, SwapFrame - 1));
  CHECK(effects.bodyShare() == 16);
  CHECK(effects.lift() != glm::vec3{0.0F});
  effects.step(SwapFrame, body);
  CHECK(effects.bodyShare() == 0);
  CHECK(effects.lift() == glm::vec3{0.0F});
  CHECK(effects.boneWired(0));
  effects.step(0x126 + 20, body);
  CHECK(effects.bodyShare() == 8);
  effects.step(0x14E, body);
  CHECK(effects.bodyShare() == 16);
  CHECK_FALSE(effects.boneWired(1));
}

TEST_CASE("the evolution draws its squares behind and its flash in front of the body") {
  MadeUpBody body;
  EvolutionEffects effects;
  effects.start(MadeUpBody::Bones, glm::vec3{0.0F}, 1);
  effects.step(0x6C, body);
  // 0xD8 breaks a bone (its flash sorted by depth) and makes a flash over the body (0xD8 is even),
  // in front when 0xD8 & lerp(5, 1) is 0.
  effects.step(0xD8, body);
  REQUIRE(effects.flashes().size() == 2);
  CHECK(effects.flashes().front().fixedDepth == -1);
  CHECK(effects.flashes().back().fixedDepth == 0x21);
  // A flash at progress 0 has no size: the objects tick before the frame is drawn.
  effects.tickObjects();
  EvolutionView view;
  view.frame.view = glm::lookAt(glm::vec3{0.0F, 400.0F, 2000.0F}, glm::vec3{0.0F, 200.0F, 0.0F},
                                glm::vec3{0.0F, 1.0F, 0.0F});
  view.frame.projection = glm::perspective(0.8F, 4.0F / 3.0F, 10.0F, 100000.0F);
  view.images.glow = dwe::rhi::TextureId{.index = 7};
  const std::vector<ScenePrim> prims = effects.draw(body, view);
  // Two squares of four lines first, the climax flash's four quarters last.
  REQUIRE(prims.size() > 12);
  for (std::size_t i = 0; i < 8; ++i) {
    CHECK(prims[i].shape == ScenePrim::Shape::Line);
    CHECK(prims[i].corners[0].clip.z == doctest::Approx(0.9999F * prims[i].corners[0].clip.w));
  }
  for (std::size_t i = prims.size() - 4; i < prims.size(); ++i) {
    CHECK(prims[i].shape == ScenePrim::Shape::Quad);
    CHECK(prims[i].corners[0].clip.z == 0.0F);
  }
}
