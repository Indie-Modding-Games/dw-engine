#include "evolution_effects.hpp"

#include <dwe/core/original_math.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace dwe::app {
namespace {

// evl.c: EVL_D_80063EFC: from frame 0x70 to 0x9a, a bone breaks on the frames whose entry is not
// negative; from 0x9b to 0x11b one breaks on every frame, until none is left.
constexpr std::int32_t BreakTableFrom = 0x70;
constexpr std::array<std::int8_t, 44> BreakTable{
    0,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, 2,  -1, -1, -1, -1, -1, -1, -1, 3,  -1, -1, -1, 4, -1, 5,  -1, 6,  0};
constexpr std::int32_t BreakEveryFrameFrom = 0x9b;
// The new form shows (EVL_applyEvolution); the rays end and the wireframe starts over.
constexpr std::int32_t SwapFrame = 0x11c;

// EVL_D_80063F28: a square spawns on the frames whose entry (frame % 18) is not 0, its green from
// 0x32 to 0xE6 as the entry goes from 1 to 5, red and blue 0x0A (MAIN_D_801349E8).
constexpr std::array<std::int8_t, 18> RingPattern{1, 2, 3, 4, 5, 4, 3, 2, 1,
                                                  0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::int32_t RingRedBlue = 0x0A;
constexpr std::int32_t RingGreenFrom = 0x32;
constexpr std::int32_t RingGreenTo = 0xE6;
constexpr std::int32_t RingPatternMax = 5;
// EVL_tickParticle and EVL_renderParticle: a square lives 0x56 frames, its half side growing from 8
// to 0x9C4 and its colour fading to black; at most 100.
constexpr std::int32_t RingLife = 0x56;
constexpr std::int32_t RingSizeFrom = 8;
constexpr std::int32_t RingSizeTo = 0x9C4;
constexpr std::size_t MaxRings = 100;

// EVL_brightenDigimonClut from frame 0x70 to 0xCC, at full strength from 0x9A.
constexpr std::int32_t BrightenFrom = 0x70;
constexpr std::int32_t BrightenFull = 0x9A;
constexpr std::int32_t BrightenUntil = 0xCD;
constexpr std::int32_t PercentRoll = 100;

// EVL_tickShardSet and EVL_renderShardSet: a broken bone's faces fly 0x1F frames, moved by their
// normal times the timer over 60, their shade from 130 down to 54 (0x80 is 1); at most 30 sets.
constexpr std::int32_t ShardLife = 0x1F;
constexpr std::int32_t ShardSpeed = 60;
constexpr std::int32_t ShardShadeSpan = 74;
constexpr std::int32_t ShardShadeSteps = 30;
constexpr std::int32_t ShardShadeMin = 54;
constexpr std::size_t MaxShardSets = 30;
// The original's normals are 4096 long.
constexpr float NormalLength = 4096.0F;

// EVL_spawnSpark: at most 16 rays; EVL_renderSparkStreak: the ray reaches length^2 / distance past
// the middle, length 400 to 500, its tip moved up to 80 each way, grey 10 to 137, from DEFEFF
// (palette 1) at (95, 32), (95, 39), (48, 32), added.
constexpr std::size_t MaxSparks = 16;
constexpr std::int32_t SparkLengthFrom = 400;
constexpr std::int32_t SparkLengthTo = 500;
constexpr std::int32_t SparkJitter = 0x50;
constexpr std::int32_t SparkGreyRoll = 128;
constexpr std::int32_t SparkGreyMin = 10;
constexpr glm::vec2 SparkImageSize{256.0F, 48.0F};
constexpr std::array<glm::vec2, 3> SparkTexels{glm::vec2{95.0F, 32.0F}, glm::vec2{95.0F, 39.0F},
                                               glm::vec2{48.0F, 32.0F}};
// The ray goes from the body's middle: bone 1.
constexpr std::uint32_t MiddleBone = 1;

// A breaking bone (evl.c:442): sound 2 to 4 of slot 8 and a flash on the bone: half 1 of the glow,
// sorted by its depth, 10 frames, scale 0 to 0x333, grey 0x96 to 0x14.
constexpr std::int32_t BreakSoundFirst = 2;
constexpr std::int32_t BreakSoundCount = 3;
constexpr EvolutionFlash BreakFlash{.mode = 1,
                                    .fixedDepth = -1,
                                    .length = 0xA,
                                    .scaleFrom = 0,
                                    .scaleTo = 0x333,
                                    .colorFrom = glm::ivec3{0x96},
                                    .colorTo = glm::ivec3{0x14}};
// The flashes over the body (evl.c:507): every other frame from 0xD7 to 0x11B on bone 1, half 0 of
// the glow, in front of everything when (frame & lerp(5, 1)) is 0 and behind it otherwise, scale 0
// to 0x333..0x4800 and grey 0x32..0x78 plus up to 99 a channel, fading to 0x14.
constexpr std::int32_t ClimaxBase = 0xD6;
constexpr std::int32_t ClimaxFrom = 0xD7;
constexpr std::int32_t ClimaxFront = 0x21;
constexpr std::int32_t ClimaxBack = 0xFA0;
constexpr std::int32_t ClimaxMaskFrom = 5;
constexpr std::int32_t ClimaxMaskTo = 1;
constexpr std::int32_t ClimaxScaleFrom = 0x333;
constexpr std::int32_t ClimaxScaleTo = 0x4800;
constexpr std::int32_t ClimaxGreyFrom = 0x32;
constexpr std::int32_t ClimaxGreyTo = 0x78;
constexpr std::int32_t ClimaxGreyEnd = 0x14;
constexpr std::int32_t FlashLength = 0xA;
constexpr std::size_t MaxFlashes = 12;

// The new form fills in from wireframe (PARTNER_WIREFRAME_TOTAL from 0 to 16, 0x126 to 0x14E).
constexpr std::int32_t FillFrom = 0x126;
constexpr std::int32_t FillTo = 0x14E;
constexpr std::int32_t FillUntil = 0x14F;
constexpr std::int32_t FullShare = 16;

// model.c: renderWireframed: a face is drawn textured when WIREFRAME_RNG_TABLE[index & 15] is below
// the share; otherwise as lines of one grey a bone, 0x37 + rand() % 0xC8.
constexpr std::array<std::int8_t, 16> WireTable{6,  3, 11, 8,  10, 13, 1, 4,
                                                14, 7, 12, 15, 5,  2,  9, 0};

// efe.c: renderEFEFlash and renderParticleFlash: the glow is a 64 x 64 quarter of SPOT256 at
// v 64 (u 0x40 for half 0, 0 for half 1), mirrored into four; its colour fades along math.c's sin
// (512 a turn) of 0x80 to 0x14, its size is the projection distance times the scale times 10 over
// the depth, plus a bump of up to 300 along sin of 0 to 0x1EB over 0x17 frames.
constexpr glm::vec2 GlowImageSize{128.0F, 128.0F};
constexpr std::array<float, 2> GlowHalfU{64.0F, 0.0F};
constexpr float GlowV = 64.0F;
constexpr float GlowSide = 64.0F;
constexpr std::int32_t GlowTexels = 63;
constexpr std::int32_t FlashShadeFrom = 0x80;
constexpr std::int32_t FlashShadeTo = 0x14;
constexpr std::int32_t FlashBumpAngle = 0x1EB;
constexpr std::int32_t FlashBumpFrames = 0x17;
constexpr std::int32_t FlashBump = 300;
constexpr std::int32_t FlashScaleTimes = 10;
constexpr std::int32_t FlashScaleLimit = 0x8000;
constexpr std::int32_t FixedShift = 12;
constexpr std::int32_t SizeShift = 10;

// math.c _sin: 512 a turn (0x80 is a right angle), 4096 for 1; libgte rsin is 4096 a turn.
constexpr std::int32_t TurnMask512 = 0x1FF;
constexpr std::int32_t Turn512To4096 = 8;
[[nodiscard]] std::int32_t sin512(std::int32_t angle) {
  return core::original::rsin((angle & TurnMask512) * Turn512To4096);
}

// Colours of 0x80 are 1 on a textured primitive; a line's are 0 to 255.
constexpr float TexturedOne = 128.0F;
constexpr float LineOne = 255.0F;

// The original's ordering table holds depths over 16 between 0x20 and 0x1000; the squares go to
// entry 0xFA1, behind the models.
constexpr float MinDepth = 0x20 * 16.0F;
constexpr float MaxDepth = 0x1000 * 16.0F;
constexpr float BehindAll = 1.0e9F;
constexpr float InFrontOfAll = -1.0e9F;

glm::vec3 eyeOf(const glm::mat4& view) {
  return glm::vec3{glm::inverse(view)[3]};
}

// The TMD's corners go clockwise seen from the front (RotNclip3 keeps those).
bool facesCamera(const BodyFace& face, glm::vec3 eye) {
  const glm::vec3 back =
      glm::cross(face.corners[1] - face.corners[0], face.corners[2] - face.corners[0]);
  return glm::dot(back, eye - face.corners[0]) < 0.0F;
}

float meanDepth(const ScenePrimFrame& frame, const BodyFace& face) {
  float sum = 0.0F;
  for (std::size_t i = 0; i < face.count; ++i) {
    sum += frame.depth(face.corners.at(i));
  }
  return sum / static_cast<float>(face.count);
}

ScenePrim faceTriangles(const ScenePrimFrame& frame, const BodyFace& face, glm::vec3 offset,
                        glm::vec4 color) {
  ScenePrim prim{.shape = face.count == 4 ? ScenePrim::Shape::Quad : ScenePrim::Shape::Triangle,
                 .color = color,
                 .texture = face.texture,
                 .blend = ScenePrimBlend::Alpha};
  for (std::size_t i = 0; i < face.count; ++i) {
    prim.corners.at(i) = {.clip = frame.clip(face.corners.at(i) + offset), .uv = face.uvs.at(i)};
  }
  return prim;
}

void addLine(std::vector<SortedPrim>& out, const ScenePrimFrame& frame, glm::vec4 from,
             glm::vec4 to, float depth, glm::vec4 color, rhi::TextureId white) {
  ScenePrim line{.shape = ScenePrim::Shape::Line, .color = color, .texture = white};
  line.corners[0].clip = from;
  line.corners[1].clip = to;
  static_cast<void>(frame);
  out.push_back(SortedPrim{.depth = depth, .prim = line});
}

} // namespace

std::int32_t originalLerp(std::int32_t start, std::int32_t end, std::int32_t t0, std::int32_t t1,
                          std::int32_t t) {
  return core::original::lerp(start, end, t0, t1, t);
}

void EvolutionEffects::start(std::uint32_t boneCount, glm::vec3 feet, std::uint32_t seed) {
  clear();
  random_ = ruleset::Random::original(seed);
  boneCount_ = boneCount;
  feet_ = feet;
  wired_.assign(boneCount, false);
  // EVL_initEvoSequence (evl.c:1312): a key of rand() & 0xFFF for each bone from 1, then the bones
  // by increasing key (the first of equal keys first).
  constexpr std::int32_t KeyMask = 0xFFF;
  std::vector<std::pair<std::int32_t, std::uint32_t>> keys;
  for (std::uint32_t bone = 1; bone < boneCount; ++bone) {
    keys.emplace_back(random() & KeyMask, bone);
  }
  std::ranges::stable_sort(keys, {}, &std::pair<std::int32_t, std::uint32_t>::first);
  order_.clear();
  for (const auto& [key, bone] : keys) {
    order_.push_back(bone);
  }
}

void EvolutionEffects::clear() {
  rings_.clear();
  shards_.clear();
  sparks_.clear();
  flashes_.clear();
  sounds_.clear();
  order_.clear();
  broken_ = 0;
  std::ranges::fill(wired_, false);
  bodyShare_ = FullShare;
  lift_ = glm::vec3{0.0F};
}

std::int32_t EvolutionEffects::random() {
  return static_cast<std::int32_t>(random_.next());
}

void EvolutionEffects::burst(std::int32_t frame, const EvolutionBody& body) {
  breakNext(frame, body);
}

void EvolutionEffects::breakNext(std::int32_t frame, const EvolutionBody& body) {
  // evl.c:433: the next bone of the order; a bone without an object passes to the one after.
  while (broken_ < order_.size()) {
    const std::uint32_t bone = order_[broken_++];
    std::vector<BodyFace> faces = body.boneFaces(bone);
    if (faces.empty()) {
      continue;
    }
    sounds_.push_back((random() % BreakSoundCount) + BreakSoundFirst);
    if (shards_.size() < MaxShardSets) {
      shards_.push_back({.faces = std::move(faces), .timer = 0});
    }
    if (bone < wired_.size()) {
      wired_[bone] = true;
    }
    if (sparks_.size() < MaxSparks) {
      sparks_.push_back({.bone = bone, .timer = SwapFrame - frame});
    }
    if (const std::optional<glm::vec3> point = body.bonePoint(bone)) {
      EvolutionFlash flash = BreakFlash;
      flash.position = *point;
      addFlash(flash);
    }
    return;
  }
}

void EvolutionEffects::addFlash(const EvolutionFlash& flash) {
  if (flashes_.size() < MaxFlashes) {
    flashes_.push_back(flash);
  }
}

void EvolutionEffects::step(std::int32_t frame, const EvolutionBody& body) {
  // evl.c:421: a square on the ground.
  const std::int32_t pattern = RingPattern.at(
      static_cast<std::size_t>(frame % static_cast<std::int32_t>(RingPattern.size())));
  if (pattern != 0 && rings_.size() < MaxRings) {
    rings_.push_back(
        {.color = {RingRedBlue,
                   originalLerp(RingGreenFrom, RingGreenTo, 1, RingPatternMax, pattern),
                   RingRedBlue},
         .timer = 0});
  }
  // evl.c:426: the palette moves toward white by a fresh roll a channel, more as the frames pass.
  if (frame >= BrightenFrom && frame < BrightenUntil) {
    const std::int32_t t = std::min(frame, BrightenFull);
    const float progress =
        static_cast<float>(t - BrightenFrom) / static_cast<float>(BrightenFull - BrightenFrom);
    const auto roll = [&] {
      return static_cast<float>(random() % PercentRoll) / static_cast<float>(PercentRoll);
    };
    const float red = roll();
    const float green = roll();
    const float blue = roll();
    lift_ = glm::vec3{red, green, blue} * progress;
  }
  // evl.c:429: the bones break.
  if (frame >= BreakTableFrom && frame < BreakEveryFrameFrom) {
    if (BreakTable.at(static_cast<std::size_t>(frame - BreakTableFrom)) >= 0) {
      breakNext(frame, body);
    }
  } else if (frame >= BreakEveryFrameFrom && frame < SwapFrame) {
    breakNext(frame, body);
  }
  // evl.c:507: the flashes over the body.
  if (frame >= ClimaxFrom && frame < SwapFrame && (frame & 1) == 0) {
    if (const std::optional<glm::vec3> middle = body.bonePoint(MiddleBone)) {
      const std::int32_t mask =
          originalLerp(ClimaxMaskFrom, ClimaxMaskTo, ClimaxBase, SwapFrame, frame);
      const std::int32_t grey =
          originalLerp(ClimaxGreyFrom, ClimaxGreyTo, ClimaxBase, SwapFrame, frame);
      const std::int32_t red = grey + (random() % PercentRoll);
      const std::int32_t green = grey + (random() % PercentRoll);
      const std::int32_t blue = grey + (random() % PercentRoll);
      addFlash(
          {.position = *middle,
           .mode = 0,
           .fixedDepth = (frame & mask) == 0 ? ClimaxFront : ClimaxBack,
           .length = FlashLength,
           .scaleFrom = 0,
           .scaleTo = originalLerp(ClimaxScaleFrom, ClimaxScaleTo, ClimaxBase, SwapFrame, frame),
           .colorFrom = {red, green, blue},
           .colorTo = glm::ivec3{ClimaxGreyEnd}});
    }
  }
  // evl.c:530: the new form, all wireframe, with its own palette.
  if (frame == SwapFrame) {
    bodyShare_ = 0;
    std::ranges::fill(wired_, false);
    lift_ = glm::vec3{0.0F};
  }
  if (frame >= FillFrom && frame < FillUntil) {
    bodyShare_ = originalLerp(0, FullShare, FillFrom, FillTo, frame);
  }
}

void EvolutionEffects::tickObjects() {
  std::erase_if(
      rings_, [](EvolutionRing& ring) { return ring.timer >= RingLife || (++ring.timer, false); });
  std::erase_if(shards_, [](EvolutionShardSet& set) {
    return set.timer >= ShardLife || (++set.timer, false);
  });
  std::erase_if(sparks_,
                [](EvolutionSpark& spark) { return spark.timer < 0 || (--spark.timer, false); });
  std::erase_if(flashes_, [](EvolutionFlash& flash) { return ++flash.progress >= flash.length; });
}

std::vector<std::int32_t> EvolutionEffects::takeSounds() {
  return std::exchange(sounds_, {});
}

bool EvolutionEffects::boneWired(std::uint32_t bone) const {
  return bodyShare_ != FullShare || (bone < wired_.size() && wired_[bone]);
}

std::vector<ScenePrim> EvolutionEffects::draw(const EvolutionBody& body,
                                              const EvolutionView& view) {
  const ScenePrimFrame& frame = view.frame;
  const rhi::TextureId white = view.images.white;
  std::vector<SortedPrim> out;

  // EVL_renderParticle: four lines on the ground, behind everything.
  for (const EvolutionRing& ring : rings_) {
    const float size =
        static_cast<float>(originalLerp(RingSizeFrom, RingSizeTo, 0, RingLife, ring.timer));
    const std::array<glm::vec3, 4> corners{
        feet_ + glm::vec3{size, 0.0F, size}, feet_ + glm::vec3{size, 0.0F, -size},
        feet_ + glm::vec3{-size, 0.0F, -size}, feet_ + glm::vec3{-size, 0.0F, size}};
    const glm::vec4 color{
        static_cast<float>(originalLerp(ring.color.r, 0, 0, RingLife, ring.timer)) / LineOne,
        static_cast<float>(originalLerp(ring.color.g, 0, 0, RingLife, ring.timer)) / LineOne,
        static_cast<float>(originalLerp(ring.color.b, 0, 0, RingLife, ring.timer)) / LineOne, 1.0F};
    for (std::size_t i = 0; i < corners.size(); ++i) {
      const glm::vec3 a = corners.at(i);
      const glm::vec3 b = corners.at((i + 1) % corners.size());
      const auto inTable = [&](glm::vec3 p) {
        const float depth = frame.depth(p);
        return depth > MinDepth && depth < MaxDepth;
      };
      if (inTable(a) && inTable(b)) {
        addLine(out, frame, clipBehind(frame.clip(a)), clipBehind(frame.clip(b)), BehindAll, color,
                white);
      }
    }
  }

  // EVL_renderShardSet: the broken bones' faces fly along their normals, darkening.
  for (const EvolutionShardSet& set : shards_) {
    const float shade =
        static_cast<float>((((ShardLife - set.timer) * ShardShadeSpan) / ShardShadeSteps) +
                           ShardShadeMin) /
        TexturedOne;
    for (const BodyFace& face : set.faces) {
      const glm::vec3 offset = face.normal * NormalLength * static_cast<float>(set.timer) /
                               static_cast<float>(ShardSpeed);
      ScenePrim prim = faceTriangles(frame, face, offset, glm::vec4{glm::vec3{shade}, 1.0F});
      BodyFace moved = face;
      for (glm::vec3& corner : moved.corners) {
        corner += offset;
      }
      out.push_back(SortedPrim{.depth = meanDepth(frame, moved), .prim = prim});
    }
  }

  // EVL_renderSparkStreak: a ray from bone 1 past each broken bone.
  if (view.images.spark) {
    const std::optional<glm::vec3> middle = body.bonePoint(MiddleBone);
    for (const EvolutionSpark& spark : sparks_) {
      const std::optional<glm::vec3> point = body.bonePoint(spark.bone);
      if (!middle || !point) {
        continue;
      }
      const glm::vec3 along = *point - *middle;
      const float lengthSquared = glm::dot(along, along);
      const float reach =
          static_cast<float>(SparkLengthFrom + (random() % (SparkLengthTo - SparkLengthFrom)));
      const glm::vec3 tip =
          lengthSquared > 0.0F ? *middle + (along * reach * reach / lengthSquared) : *middle;
      const auto jitter = [&] {
        return static_cast<float>((random() % (2 * SparkJitter)) - SparkJitter);
      };
      const float jx = jitter();
      const float jy = jitter();
      const float jz = jitter();
      const glm::vec3 side = tip + glm::vec3{jx, jy, jz};
      const float grey =
          static_cast<float>((random() % SparkGreyRoll) + SparkGreyMin) / TexturedOne;
      ScenePrim ray{.shape = ScenePrim::Shape::Triangle,
                    .color = glm::vec4{glm::vec3{grey}, 1.0F},
                    .texture = *view.images.spark,
                    .blend = ScenePrimBlend::Additive};
      const std::array<glm::vec3, 3> points{side, *middle, tip};
      float depth = 0.0F;
      for (std::size_t i = 0; i < points.size(); ++i) {
        ray.corners.at(i) = {.clip = frame.clip(points.at(i)),
                             .uv = SparkTexels.at(i) / SparkImageSize};
        depth += frame.depth(points.at(i)) / static_cast<float>(points.size());
      }
      out.push_back(SortedPrim{.depth = depth, .prim = ray});
    }
  }

  // renderEFEFlash: the glow, mirrored into four around its point.
  if (view.images.glow) {
    for (const EvolutionFlash& flash : flashes_) {
      addFlashPrims(flash, frame, view.focalLength, *view.images.glow, out);
    }
  }

  // renderWireframed: the wired bones, or the whole body while it fills in.
  for (std::uint32_t bone = 0; bone < boneCount_; ++bone) {
    if (!boneWired(bone)) {
      continue;
    }
    const std::int32_t share = bodyShare_ != FullShare ? bodyShare_ : 0;
    const std::int32_t grey = WireGreyMin + (random() % (WireGreyMax - WireGreyMin));
    addWiredFaces(body.boneFaces(bone), share, grey, frame, white, out);
  }

  return sortedPrims(std::move(out));
}

std::vector<ScenePrim> sortedPrims(std::vector<SortedPrim> prims) {
  std::ranges::stable_sort(
      prims, [](const SortedPrim& a, const SortedPrim& b) { return a.depth > b.depth; });
  std::vector<ScenePrim> out;
  out.reserve(prims.size());
  for (SortedPrim& sorted : prims) {
    out.push_back(std::move(sorted.prim));
  }
  return out;
}

void addFlashPrims(const EvolutionFlash& flash, const ScenePrimFrame& frame, float focalLength,
                   rhi::TextureId image, std::vector<SortedPrim>& out) {
  const float depth = frame.depth(flash.position);
  if (depth <= 0.0F) {
    return;
  }
  const auto at = [&](std::int32_t from, std::int32_t to) {
    return originalLerp(from, to, 0, flash.length, flash.progress);
  };
  const std::int32_t fade = sin512(at(FlashShadeFrom, FlashShadeTo));
  const glm::vec3 color =
      glm::vec3{static_cast<float>((at(flash.colorFrom.r, flash.colorTo.r) * fade) >> FixedShift),
                static_cast<float>((at(flash.colorFrom.g, flash.colorTo.g) * fade) >> FixedShift),
                static_cast<float>((at(flash.colorFrom.b, flash.colorTo.b) * fade) >> FixedShift)} /
      TexturedOne;
  const auto scaled =
      static_cast<std::int64_t>(focalLength) * at(flash.scaleFrom, flash.scaleTo) * FlashScaleTimes;
  std::int64_t scale = scaled / static_cast<std::int64_t>(depth);
  const std::int32_t bump = originalLerp(0, FlashBumpAngle, 0, FlashBumpFrames, flash.progress);
  scale += ((static_cast<std::int64_t>(sin512(bump)) * FlashBump) >> FixedShift) + 1;
  if (scale >= FlashScaleLimit || scale <= 0) {
    return;
  }
  const std::int64_t reach = (scale * GlowTexels) >> SizeShift;
  if (reach <= 0) {
    return;
  }
  const float half = static_cast<float>(reach + 1);
  float sortDepth = depth;
  glm::vec4 centre = frame.clip(flash.position);
  if (flash.fixedDepth == ClimaxFront) {
    centre = clipInFront(centre);
    sortDepth = InFrontOfAll;
  } else if (flash.fixedDepth > 0) {
    centre = clipBehind(centre);
    sortDepth = BehindAll;
  } else if (depth <= MinDepth || depth >= MaxDepth) {
    return;
  }
  const SceneGlow glow{.centre = centre,
                       .half = half,
                       .color = color,
                       .texture = image,
                       .texel = {GlowHalfU.at(static_cast<std::size_t>(flash.mode & 1)), GlowV},
                       .side = glm::vec2{GlowSide},
                       .imageSize = GlowImageSize};
  for (const ScenePrim& quarter : glowQuarters(glow)) {
    out.push_back(SortedPrim{.depth = sortDepth, .prim = quarter});
  }
}

void addWiredFaces(std::span<const BodyFace> faces, std::int32_t share, std::int32_t grey,
                   const ScenePrimFrame& frame, rhi::TextureId white, std::vector<SortedPrim>& out,
                   const std::function<glm::vec4(const BodyFace&)>& shade) {
  const glm::vec3 eye = eyeOf(frame.view);
  const glm::vec4 color{glm::vec3{static_cast<float>(grey) / LineOne}, 1.0F};
  for (const BodyFace& face : faces) {
    if (!facesCamera(face, eye)) {
      continue;
    }
    const float depth = meanDepth(frame, face);
    if (WireTable.at(face.index & 0xF) < share) {
      out.push_back(SortedPrim{.depth = depth,
                               .prim = faceTriangles(frame, face, glm::vec3{0.0F},
                                                     shade ? shade(face) : glm::vec4{1.0F})});
      continue;
    }
    // LINE_F4: a triangle closed, a quad through 0 1 3 2.
    const std::array<std::size_t, 4> path = face.count == 4
                                                ? std::array<std::size_t, 4>{0, 1, 3, 2}
                                                : std::array<std::size_t, 4>{0, 1, 2, 0};
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
      addLine(out, frame, frame.clip(face.corners.at(path.at(i))),
              frame.clip(face.corners.at(path.at(i + 1))), depth, color, white);
    }
  }
}

std::int32_t wireTableEntry(std::uint32_t index) {
  return WireTable.at(index & 0xF);
}

} // namespace dwe::app
