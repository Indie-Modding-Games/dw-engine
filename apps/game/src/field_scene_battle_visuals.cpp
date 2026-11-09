#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/core/log.hpp>

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "evolution_effects.hpp"
#include "field_scene.hpp"

// The battle's world objects over the fighters besides their statuses (battle_effect.c), as the
// original draws them: models of EX and DEFEFF, the common glow and sparks of the DEFEFF sheet.
namespace dwe::app {

namespace {

using game::battle::StatusVisuals;

constexpr float One = 4096.0F;
constexpr float Turn = 4096.0F;
constexpr float SineTurn = 512.0F;
constexpr float ColorOne = 128.0F;
constexpr float SubUnit = 16.0F;

// The console's MATRIX of RotMatrix (Rx · Ry · Rz, 4096 a turn) and ScaleMatrix (columns times the
// scale), as a world matrix: F M F with F = diag(1, -1, -1).
glm::mat4 consoleTransform(glm::vec3 angles, glm::vec3 scale) {
  const auto about = [](float angle, int axis) {
    const float radians = angle * 2.0F * std::numbers::pi_v<float> / Turn;
    const float s = std::sin(radians);
    const float c = std::cos(radians);
    std::array<std::array<float, 3>, 3> m{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    if (axis == 0) {
      m = {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
    } else if (axis == 1) {
      m = {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
    } else {
      m = {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
    }
    return m;
  };
  const auto product = [](const std::array<std::array<float, 3>, 3>& a,
                          const std::array<std::array<float, 3>, 3>& b) {
    std::array<std::array<float, 3>, 3> out{};
    for (std::size_t row = 0; row < 3; ++row) {
      for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t k = 0; k < 3; ++k) {
          out.at(row).at(column) += a.at(row).at(k) * b.at(k).at(column);
        }
      }
    }
    return out;
  };
  const auto m = product(product(about(angles.x, 0), about(angles.y, 1)), about(angles.z, 2));
  constexpr std::array<float, 3> Flip{1.0F, -1.0F, -1.0F};
  const std::array<float, 3> by{scale.x, scale.y, scale.z};
  glm::mat4 out{1.0F};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      out[static_cast<glm::length_t>(column)][static_cast<glm::length_t>(row)] =
          Flip.at(row) * Flip.at(column) * m.at(row).at(column) * by.at(column);
    }
  }
  return out;
}

glm::vec3 worldOf(glm::vec3 original) {
  return {original.x, -original.y, -original.z};
}

glm::vec3 vectorOf(const StatusVisuals::Vec3& v) {
  return {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
}

// math.c: _sin and _cos, 512 a turn.
float sine512(std::int32_t angle) {
  return std::sin(static_cast<float>(angle) * 2.0F * std::numbers::pi_v<float> / SineTurn);
}
float cosine512(std::int32_t angle) {
  return std::cos(static_cast<float>(angle) * 2.0F * std::numbers::pi_v<float> / SineTurn);
}

// BTL_initializeFinisherAuraModel: model/etcdat/ex (ETCDAT/EX.TMD, the battle overlay's copy at
// 0x80053800); BTL_setTMDObjectColor gives objects 0 to 2 and 4 the grey 0x3C and object 3 0x5A
// (BTL_D_8007380C, BTL_D_8007381C) where the file has 0x80.
constexpr std::string_view AuraModel = "model/etcdat/ex";
constexpr float AuraGrey = 0x3C / ColorOne;
constexpr float AuraCoreGrey = 0x5A / ColorOne;
constexpr std::array<std::size_t, 3> AuraObjects{0, 1, 2};
constexpr std::size_t AuraCore = 3;
constexpr std::size_t AuraRing = 4;
constexpr float AuraRingWiden = 1.4F;
// BTL_renderFinisherAura: the width grows over 10 frames by 1 - cos, the height over 5; the
// height sways 15 % at a 0x200 of _sin each 0x10 frames; a radius of 150 and a height of 350 are
// the model's size.
constexpr std::int32_t AuraWidenFrames = 10;
constexpr std::int32_t AuraWidenAngle = 0x80;
constexpr float AuraWidenScale = 0xB34;
constexpr float AuraWidenFrom = 0x4CC;
constexpr std::int32_t AuraRiseFrames = 5;
constexpr std::int32_t AuraSwayTurn = 0x200;
constexpr std::int32_t AuraSwayFrames = 0x10;
constexpr float AuraSwayKept = 0.85F;
constexpr float AuraSway = 0.15F;
constexpr float AuraRadius = 150.0F;
constexpr float AuraHeight = 350.0F;
// BTL_FINISHER_AURA_COLOR, fading over the sparks' 7 frames.
constexpr glm::vec3 AuraSparkColor{0xCC, 0xA8, 0x28};

// model/etcdat/defeff (ETCDAT/DEFEFF.TMD, BUFF_MODEL): the disk is object 5, its palette row 491
// on even frames and 492 on odd ones (morphBuffModel: 0x7ACC, 0x7B0C); the rings are objects
// 3, 0, 1, 2, 4 (BTL_BUFF_RING_OBJECTS), each one tenth larger than the last and turned within it.
constexpr std::string_view BuffModel = "model/etcdat/defeff";
constexpr std::size_t DiskObject = 5;
constexpr std::uint32_t DiskRow = 491;
constexpr std::int32_t DiskOtherPalette = 4;
constexpr std::array<std::size_t, StatusVisuals::BuffRingCount> RingObjects{3, 0, 1, 2, 4};
constexpr float RingGrowth = 10.0F;
constexpr std::uint32_t RingNode = 1;

// The DEFEFF sheet with its palette 1 (row 489): the common glow (15 x 15 at 0, 32), the sparks'
// 7 x 7 at 48, 40 (BTL_renderFinisherAuraSpark, BTL_renderBuffRingsSpark) and the trails' 47 x 7
// at 48, 32 (BTL_renderBuffTrails), all added.
constexpr std::string_view SparkSheet = "texture/etcdat/defeff?palette=1";
constexpr std::string_view CommonSheet = "texture/etcdat/defeff";
constexpr std::int32_t GlowPalette = 1;
constexpr glm::vec2 SheetSize{256.0F, 48.0F};
constexpr glm::vec2 GlowTexel{0.0F, 32.0F};
constexpr float GlowTexels = 15.0F;
constexpr std::array<glm::vec2, 3> SparkTexels{{{0x30, 0x28}, {0x37, 0x28}, {0x30, 0x2F}}};
constexpr std::array<glm::vec2, 4> TrailTexels{
    {{0x5F, 0x20}, {0x5F, 0x27}, {0x30, 0x20}, {0x30, 0x27}}};
// BTL_renderBuffTrails: (200, 255, 180) at a full life of 8.
constexpr glm::vec3 TrailColor{200.0F, 255.0F, 180.0F};
constexpr float TrailLife = 8.0F;
// BTL_renderItemParticles: renderFXParticle(pos, 0x14, brightness >> 4).
constexpr float ItemGlowSize = 0x14;
// BTL_renderAuraProjectile: the shot's card (BTL_AURA_PROJECTILE_VERTEX_0..3: 50 each way) and its
// texels: 7 x 7 from (column + 0x60, 0xA0 - 0x80) of DEFEFF with its palette 4.
constexpr float ShotHalfSide = 50.0F;
constexpr float ShotColumnBase = 0x60;
constexpr float ShotTexelRow = 0x20;
constexpr float ShotTexels = 7.0F;
constexpr std::size_t ShotPalette = 4;

} // namespace

const FieldScene::EffectModelEntry* FieldScene::repaintedStatusModel(std::string_view model,
                                                                     std::uint32_t row,
                                                                     std::string_view image,
                                                                     std::int32_t palette) {
  const std::string key = std::format("{}#{}#{}", model, row, palette);
  if (const auto found = statusModels_.find(key); found != statusModels_.end()) {
    return found->second ? &*found->second : nullptr;
  }
  std::optional<EffectModelEntry> entry;
  auto id = assets::AssetId::parse(model);
  auto data = id ? assets::loadModel(*files_, *id)
                 : core::Result<assets::ModelData>{std::unexpected{id.error()}};
  if (data) {
    for (const assets::ModelMaterial& material : data->materials) {
      if (!material.image || !material.clut || material.clut->y != row ||
          *material.image >= data->images.size()) {
        continue;
      }
      auto repainted = effectSheetImage(*files_, std::string{image}, palette, nullptr);
      assets::Image& target = data->images[*material.image];
      if (!repainted || repainted->width != target.width || repainted->height != target.height) {
        continue;
      }
      // The model's own images keep a colour with STP opaque: its material mixes it.
      for (std::size_t alpha = 3; alpha < repainted->pixels.size(); alpha += 4) {
        if (repainted->pixels[alpha] == std::byte{assets::SemiTransparentAlpha}) {
          repainted->pixels[alpha] = std::byte{0xFF};
        }
      }
      target = std::move(*repainted);
    }
    if (auto handle = models_->add(*data)) {
      entry =
          EffectModelEntry{.handle = *handle, .joints = data->skin ? data->skin->joints.size() : 0};
    }
  }
  if (!entry) {
    core::logWarning("battle: no model {} with palette {} for the battle visuals", model, palette);
  }
  auto [at, inserted] = statusModels_.emplace(key, entry);
  return at->second ? &*at->second : nullptr;
}

void FieldScene::submitBattleVisualModels() {
  if (!statusVisuals_) {
    return;
  }
  std::vector<glm::mat4> joints;
  // One object of the model (its joint) placed by `transform` at `at`, the others hidden.
  const auto submit = [&](const EffectModelEntry& entry, glm::vec3 at,
                          const std::vector<std::pair<std::size_t, glm::mat4>>& objects,
                          float grey) {
    if (entry.joints == 0) {
      return;
    }
    joints.assign(entry.joints, glm::mat4{0.0F});
    for (const auto& [object, transform] : objects) {
      if (object < joints.size()) {
        joints.at(object) = transform;
      }
    }
    models_->submit({.model = entry.handle,
                     .world = glm::translate(glm::mat4{1.0F}, at),
                     .joints = joints,
                     .tint = glm::vec4{glm::vec3{grey}, 1.0F}});
  };

  // BTL_renderFinisherAura.
  if (const EffectModelEntry* model = statusModel(AuraModel)) {
    for (const auto& aura : statusVisuals_->finisherAuras()) {
      const std::optional<StatusAnchor> anchor = aura ? statusAnchor(aura->fighter) : std::nullopt;
      if (!anchor) {
        continue;
      }
      const std::int32_t frame = aura->frame;
      float width = One;
      if (frame < AuraWidenFrames) {
        const std::int32_t angle = originalLerp(0, AuraWidenAngle, 0, AuraWidenFrames - 1, frame);
        width = ((One - (cosine512(angle) * One)) * AuraWidenScale / One) + AuraWidenFrom;
      }
      width = width * static_cast<float>(anchor->body.radius) / AuraRadius / One;
      float height = frame < AuraRiseFrames ? static_cast<float>(frame) * One / 4.0F : One;
      height = height * static_cast<float>(anchor->body.height) / AuraHeight / One;
      const std::int32_t sway = originalLerp(0, AuraSwayTurn, 0, AuraSwayFrames, frame);
      height = (height * AuraSwayKept) + (height * AuraSway * sine512(sway));
      const glm::mat4 scale = consoleTransform({}, {width, height, width});
      const std::size_t shell = AuraObjects.at(static_cast<std::size_t>(frame / 2 % 3));
      const glm::mat4 ring =
          consoleTransform({0.0F, static_cast<float>(aura->spin), 0.0F},
                           {width * AuraRingWiden, height, width * AuraRingWiden});
      submit(*model, anchor->feet, {{shell, scale}, {AuraRing, ring}}, AuraGrey);
      submit(*model, anchor->feet, {{AuraCore, scale}}, AuraCoreGrey);
    }
  }

  // BTL_renderBuffDisk: on the fighter's feet as it stands now.
  if (const auto& disk = statusVisuals_->buffDisk()) {
    const bool odd = ((statusVisuals_->frames() - 1) & 1) != 0;
    const EffectModelEntry* model =
        odd ? repaintedStatusModel(BuffModel, DiskRow, CommonSheet, DiskOtherPalette)
            : statusModel(BuffModel);
    const std::optional<StatusAnchor> anchor = statusAnchor(disk->fighter);
    // Smooth motion: its size and turn between the last frame's and this one's
    // (battle_glide.hpp).
    const StatusVisuals* last = lastStatusFrame();
    const StatusVisuals::BuffDisk* before = last != nullptr && last->buffDisk() &&
                                                    last->buffDisk()->fighter == disk->fighter &&
                                                    last->buffDisk()->frame + 1 == disk->frame
                                                ? &*last->buffDisk()
                                                : nullptr;
    const auto mixed = [before, this](std::int32_t was, std::int32_t is) {
      return before != nullptr
                 ? between(static_cast<float>(was), static_cast<float>(is), visualsProgress_)
                 : static_cast<float>(is);
    };
    if (model != nullptr && anchor) {
      const float across = mixed(before ? before->scaleXZ : 0, disk->scaleXZ) / One;
      const float up = mixed(before ? before->scaleY : 0, disk->scaleY) / One;
      submit(*model, anchor->feet,
             {{DiskObject, consoleTransform(
                               {0.0F, mixed(before ? before->rotationY : 0, disk->rotationY), 0.0F},
                               {across, up, across})}},
             1.0F);
    }
  }

  // BTL_renderBuffRings: each ring within the last (GsCOORDINATE2 super), on the second node.
  if (const auto& rings = statusVisuals_->buffRings();
      rings && rings->frame < StatusVisuals::BuffRingsShownFrames) {
    const EffectModelEntry* model = statusModel(BuffModel);
    const std::optional<StatusAnchor> anchor = statusAnchor(rings->fighter);
    const Actor* actor = rings->fighter < statusBodies_.size()
                             ? fighterActor(statusBodies_[rings->fighter].npc)
                             : nullptr;
    const std::optional<glm::vec3> node =
        actor != nullptr ? nodePoint(*actor, RingNode, glm::vec3{0.0F}) : std::nullopt;
    if (model != nullptr && anchor && node) {
      std::vector<std::pair<std::size_t, glm::mat4>> objects;
      glm::mat4 within{1.0F};
      for (std::size_t j = 0; j < RingObjects.size(); ++j) {
        const float scale = static_cast<float>(rings->scale) *
                            (static_cast<float>(j) + RingGrowth) / RingGrowth / One;
        const auto& turn = rings->rotations.at(j);
        within = within *
                 consoleTransform({static_cast<float>(turn[0]), 0.0F, static_cast<float>(turn[1])},
                                  glm::vec3{scale});
        objects.emplace_back(RingObjects.at(j), within);
      }
      submit(*model, *node, objects, 1.0F);
    }
  }
}

void FieldScene::addBattleVisualBillboards(std::vector<render::MapBillboard>& billboards) {
  if (!statusVisuals_) {
    return;
  }
  const std::optional<std::size_t> glow = sheetTexture(std::string{CommonSheet}, GlowPalette);
  if (!glow) {
    return;
  }
  // BTL_renderItemParticles: every glow in the first one's brightness, where the item landed.
  // Smooth motion: each glow between its last frame's place and this one's (battle_glide.hpp).
  const StatusVisuals* last = lastStatusFrame();
  const auto& bursts = statusVisuals_->itemBursts();
  for (std::size_t index = 0; index < bursts.size(); ++index) {
    const auto& burst = bursts.at(index);
    if (!burst) {
      continue;
    }
    const StatusVisuals::ItemBurst* before = lastFramesOf(
        last ? &last->itemBursts() : nullptr, index, *burst, [](const auto& was, const auto& is) {
          return was.fighter == is.fighter && was.origin == is.origin && was.timer == is.timer + 1;
        });
    const std::optional<StatusAnchor> anchor = statusAnchor(burst->fighter);
    const float feet = anchor ? anchor->feet.y : 0.0F;
    const float grey = static_cast<float>(burst->particles.front().brightness >> 4) / ColorOne;
    for (std::size_t k = 0; k < burst->particles.size(); ++k) {
      const StatusVisuals::ItemParticle& particle = burst->particles.at(k);
      if (particle.life < 0) {
        continue;
      }
      glm::vec3 position = vectorOf(particle.position);
      if (before != nullptr && before->particles.at(k).life == particle.life + 1) {
        position = glm::mix(vectorOf(before->particles.at(k).position), position, visualsProgress_);
      }
      const glm::vec3 offset = worldOf(position / SubUnit);
      billboards.push_back({.centre = glm::vec3{static_cast<float>(burst->origin.x), feet,
                                                static_cast<float>(burst->origin.z)} +
                                      offset,
                            .size = glm::vec2{ItemGlowSize},
                            .flat = false,
                            .texture = *glow,
                            .texel = GlowTexel,
                            .texels = glm::vec2{GlowTexels},
                            .blend = assets::SpriteBlend::Add,
                            .color = glm::vec3{grey}});
    }
  }
}

void FieldScene::addBattleVisualPrims(std::vector<ScenePrim>& prims) {
  if (!statusVisuals_) {
    return;
  }
  const std::optional<rhi::TextureId> sheet = sceneImage(SparkSheet);
  if (!sheet) {
    return;
  }
  const render::MapCamera& camera = mapRenderer_->camera();
  const ScenePrimFrame frame{
      .view = camera.view(), .projection = camera.projection(), .pixelScale = camera.scale()};
  // A spark: the centre and its two corners scaled by `scale` (4096 is 1), added.
  const auto spark = [&](glm::vec3 centre, const StatusVisuals::SparkPair& pair, float scale,
                         glm::vec3 color) {
    ScenePrim prim{.shape = ScenePrim::Shape::Triangle,
                   .color = glm::vec4{color / ColorOne, 1.0F},
                   .texture = *sheet,
                   .blend = ScenePrimBlend::Additive};
    const std::array<glm::vec3, 3> points{centre,
                                          centre + worldOf(vectorOf(pair.first) * scale / One),
                                          centre + worldOf(vectorOf(pair.second) * scale / One)};
    for (std::size_t i = 0; i < points.size(); ++i) {
      prim.corners.at(i) = {.clip = frame.clip(points.at(i)), .uv = SparkTexels.at(i) / SheetSize};
    }
    prims.push_back(prim);
  };

  // BTL_renderFinisherAura: ten sparks from the feet over the first 7 frames.
  for (const auto& aura : statusVisuals_->finisherAuras()) {
    const std::optional<StatusAnchor> anchor = aura ? statusAnchor(aura->fighter) : std::nullopt;
    if (!anchor || aura->frame >= StatusVisuals::FinisherSparkFrames) {
      continue;
    }
    const auto sparkFrames = static_cast<float>(StatusVisuals::FinisherSparkFrames);
    const auto frameNow = static_cast<float>(aura->frame);
    const float scale = std::floor(frameNow * One / sparkFrames) *
                        static_cast<float>(anchor->body.radius) / AuraRadius;
    const glm::vec3 color = glm::floor(AuraSparkColor * (sparkFrames - frameNow) / sparkFrames);
    for (const StatusVisuals::SparkPair& pair : statusVisuals_->finisherSparks()) {
      spark(anchor->feet, pair, scale, color);
    }
  }

  // BTL_renderBuffRings: after the rings, 16 sparks from the second node that spread and fade.
  if (const auto& rings = statusVisuals_->buffRings();
      rings && rings->frame >= StatusVisuals::BuffRingsShownFrames && rings->target != 0) {
    const Actor* actor = rings->fighter < statusBodies_.size()
                             ? fighterActor(statusBodies_[rings->fighter].npc)
                             : nullptr;
    const std::optional<glm::vec3> node =
        actor != nullptr ? nodePoint(*actor, RingNode, glm::vec3{0.0F}) : std::nullopt;
    if (node) {
      const std::int32_t spread =
          originalLerp(0, rings->target, StatusVisuals::BuffRingsShownFrames,
                       StatusVisuals::BuffRingsFrames, rings->frame);
      const auto left = static_cast<float>(rings->target - spread);
      const glm::vec3 color = glm::floor(vectorOf(StatusVisuals::ringColor(rings->kind)) * left /
                                         static_cast<float>(rings->target));
      for (const StatusVisuals::SparkPair& pair : statusVisuals_->ringSparks()) {
        spark(*node, pair, static_cast<float>(spread), color);
      }
    }
  }

  // BTL_renderBuffTrails: the streaks still alive, fading with their life, over the feet.
  // Smooth motion: each rising between its last frame's corners and this one's
  // (battle_glide.hpp).
  if (const auto& trails = statusVisuals_->buffTrails()) {
    const std::optional<StatusAnchor> anchor = statusAnchor(trails->fighter);
    const float ground = anchor ? anchor->feet.y : 0.0F;
    const StatusVisuals* last = lastStatusFrame();
    const StatusVisuals::BuffTrails* before =
        last != nullptr && last->buffTrails() && last->buffTrails()->fighter == trails->fighter
            ? &*last->buffTrails()
            : nullptr;
    for (std::size_t k = 0; k < trails->trails.size(); ++k) {
      const StatusVisuals::BuffTrail& trail = trails->trails.at(k);
      if (trail.life <= 0) {
        continue;
      }
      const StatusVisuals::BuffTrail* was =
          before != nullptr && before->trails.at(k).life == trail.life + 1 ? &before->trails.at(k)
                                                                           : nullptr;
      const glm::vec3 color = glm::floor(TrailColor * static_cast<float>(trail.life) / TrailLife);
      ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                     .color = glm::vec4{color / ColorOne, 1.0F},
                     .texture = *sheet,
                     .blend = ScenePrimBlend::Additive};
      for (std::size_t i = 0; i < prim.corners.size(); ++i) {
        glm::vec3 corner = vectorOf(trail.corners.at(i));
        if (was != nullptr) {
          corner = glm::mix(vectorOf(was->corners.at(i)), corner, visualsProgress_);
        }
        prim.corners.at(i) = {.clip = frame.clip({corner.x, ground + corner.y, corner.z}),
                              .uv = TrailTexels.at(i) / SheetSize};
      }
      prims.push_back(prim);
    }
  }
}

// BTL_renderAuraProjectile: a 100 x 100 card standing along the way the shot flies, its texture's
// left edge ahead and its top up: DEFEFF's 7 x 7 texels at (column + 0x60, 32) with palette 4
// (getClut(192, 492)), colour 0x80. The console's semi-transparency (code |= 2, additive) only
// mixes the palette's STP colours; the shots' white and dark texels are drawn whole.
void FieldScene::addFlattenedShotPrims(std::vector<ScenePrim>& prims) {
  if (flattenedShots_.empty()) {
    return;
  }
  const std::optional<rhi::TextureId> sheet = stpSceneImage(CommonSheet, ShotPalette);
  if (!sheet) {
    return;
  }
  const render::MapCamera& camera = mapRenderer_->camera();
  const ScenePrimFrame frame{
      .view = camera.view(), .projection = camera.projection(), .pixelScale = camera.scale()};
  for (const ShotSprite& shot : flattenedShots_) {
    // The shot keeps the height it left at, over the ground its shooter stands on.
    const std::optional<StatusAnchor> anchor = statusAnchor(shot.attacker);
    const float ground = anchor ? anchor->feet.y : 0.0F;
    const glm::vec3 centre{shot.at.x, ground + shot.height, shot.at.y};
    const glm::vec3 ahead = glm::vec3{shot.facing.x, 0.0F, shot.facing.y} * ShotHalfSide;
    const glm::vec3 up{0.0F, ShotHalfSide, 0.0F};
    const glm::vec2 texel{static_cast<float>(shot.column) + ShotColumnBase, ShotTexelRow};
    const glm::vec2 from = texel / SheetSize;
    const glm::vec2 to = (texel + glm::vec2{ShotTexels}) / SheetSize;
    ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                   .texture = *sheet,
                   .blend = ScenePrimBlend::Additive,
                   .stpMask = true};
    prim.corners[0] = {.clip = frame.clip(centre + up + ahead), .uv = from};
    prim.corners[1] = {.clip = frame.clip(centre + up - ahead), .uv = {to.x, from.y}};
    prim.corners[2] = {.clip = frame.clip(centre - up + ahead), .uv = {from.x, to.y}};
    prim.corners[3] = {.clip = frame.clip(centre - up - ahead), .uv = to};
    prims.push_back(prim);
  }
}

} // namespace dwe::app
