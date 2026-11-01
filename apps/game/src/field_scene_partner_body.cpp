#include <dwe/core/log.hpp>
#include <dwe/ui/image_reference.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

constexpr std::size_t Channels = 4;
// No image: the renderer's white texture.
constexpr std::uint32_t NoImage = 0xFFFFFFFFU;

rhi::Extent extentOf(const assets::Image& image) {
  return {.width = image.width, .height = image.height};
}

// The faces of one object of a model (its joint), placed in the world by `place`.
std::vector<BodyFace>
objectFaces(const assets::ModelData& model, std::uint32_t object, const glm::mat4& place,
            const std::function<rhi::TextureId(std::uint32_t)>& imageTexture) {
  std::vector<BodyFace> faces;
  const glm::mat3 turn{place};
  std::uint32_t index = 0;
  for (const assets::ModelPrimitive& primitive : model.primitives) {
    const assets::ModelMaterial* material = primitive.material < model.materials.size()
                                                ? &model.materials[primitive.material]
                                                : nullptr;
    const std::optional<std::uint32_t> image =
        material != nullptr ? material->image : std::optional<std::uint32_t>{};
    const rhi::TextureId texture = imageTexture(image.value_or(NoImage));
    const auto& indices = primitive.indices;
    // The extracted triangle of a TMD face is (f, f + 2, f + 1), a quad's second (f + 1, f + 2,
    // f + 3): its corners f to f + 3 come back in the TMD's order (formats/src/model/gltf.cpp).
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
      const std::uint32_t first = indices[t];
      if (first >= primitive.vertices.size() || primitive.vertices[first].joints[0] != object) {
        continue;
      }
      const bool quad = t + 5 < indices.size() && indices[t + 3] == indices[t + 2] &&
                        indices[t + 4] == indices[t + 1] && indices[t + 5] == indices[t + 1] + 1;
      BodyFace face{.bone = object,
                    .index = index++,
                    .count = quad ? std::uint8_t{4} : std::uint8_t{3},
                    .texture = texture};
      const std::array<std::uint32_t, 4> corners{first, indices[t + 2], indices[t + 1],
                                                 quad ? indices[t + 5] : first};
      glm::vec3 normal{0.0F};
      for (std::size_t i = 0; i < face.count; ++i) {
        const assets::ModelVertex& vertex = primitive.vertices.at(corners.at(i));
        face.corners.at(i) = glm::vec3{place * glm::vec4{vertex.position, 1.0F}};
        face.uvs.at(i) = vertex.uv;
        if (i < 3) {
          normal += turn * vertex.normal;
        }
      }
      face.normal = normal / 3.0F;
      faces.push_back(face);
      if (quad) {
        t += 3;
      }
    }
  }
  return faces;
}

} // namespace

glm::mat4 FieldScene::PartnerBody::world() const {
  const Actor& actor = *owner_->partnerActor_;
  const auto& transform = owner_->world_.registry().get<scene::Transform>(actor.entity);
  return glm::scale(glm::rotate(glm::translate(glm::mat4{1.0F}, transform.position), transform.yaw,
                                glm::vec3{0.0F, 1.0F, 0.0F}),
                    owner_->stage_.partnerScale * owner_->partnerSpeciesScale_);
}

std::optional<glm::vec3> FieldScene::PartnerBody::bonePoint(std::uint32_t bone) const {
  const Actor& actor = *owner_->partnerActor_;
  if (!actor.model->skin) {
    return std::nullopt;
  }
  const auto joints = actor.player->jointMatrices();
  const auto& inverseBind = actor.model->skin->inverseBindMatrices;
  if (bone >= joints.size() || bone >= inverseBind.size()) {
    return std::nullopt;
  }
  // A skin matrix is the joint's model-space transform times its inverse bind matrix.
  return glm::vec3{world() * joints[bone] * glm::inverse(inverseBind[bone]) *
                   glm::vec4{0.0F, 0.0F, 0.0F, 1.0F}};
}

std::uint32_t FieldScene::PartnerBody::boneCount() const {
  return static_cast<std::uint32_t>(owner_->partnerActor_->player->jointMatrices().size());
}

std::vector<BodyFace> FieldScene::PartnerBody::boneFaces(std::uint32_t bone) const {
  Actor& actor = *owner_->partnerActor_;
  const auto joints = actor.player->jointMatrices();
  if (bone >= joints.size()) {
    return {};
  }
  return objectFaces(*actor.model, bone, world() * joints[bone],
                     [&](std::uint32_t image) { return owner_->partnerImage(image); });
}

rhi::TextureId FieldScene::partnerImage(std::uint32_t image) {
  const assets::ModelData* model = partnerActor_->model.get();
  if (partnerTexturesOf_ != model) {
    for (const std::optional<rhi::TextureId>& texture : partnerTextures_) {
      if (texture && texture->index != renderer_->whiteTexture().index) {
        renderer_->destroyTexture(*texture);
      }
    }
    partnerTextures_.assign(model->images.size(), std::nullopt);
    partnerTexturesOf_ = model;
  }
  if (image >= partnerTextures_.size()) {
    return renderer_->whiteTexture();
  }
  std::optional<rhi::TextureId>& slot = partnerTextures_[image];
  if (!slot) {
    const assets::Image& pixels = model->images[image];
    if (auto made = renderer_->queueTexture(extentOf(pixels), pixels.pixels)) {
      slot = *made;
    } else {
      core::logWarning("field: no texture for the partner's image {}: {}", image,
                       made.error().message);
      slot = renderer_->whiteTexture();
    }
  }
  return *slot;
}

std::optional<rhi::TextureId> FieldScene::sceneImage(std::string_view source) {
  if (const auto found = sceneImages_.find(source); found != sceneImages_.end()) {
    return found->second;
  }
  std::optional<rhi::TextureId> texture;
  auto reference = ui::parseImageReference(source);
  auto image = reference ? ui::loadImage(*files_, *reference)
                         : core::Result<assets::Image>{std::unexpected{reference.error()}};
  if (image) {
    // The scenes add these images: every texel the original draws counts whole.
    for (std::size_t alpha = Channels - 1; alpha < image->pixels.size(); alpha += Channels) {
      if (image->pixels[alpha] != std::byte{0}) {
        image->pixels[alpha] = std::byte{0xFF};
      }
    }
    if (auto made = renderer_->queueTexture(extentOf(*image), image->pixels)) {
      texture = *made;
    } else {
      core::logWarning("field: no texture for {}: {}", source, made.error().message);
    }
  } else {
    core::logWarning("field: no image {}: {}", source, image.error().message);
  }
  sceneImages_.emplace(std::string{source}, texture);
  return texture;
}

const std::vector<std::vector<BodyFace>>* FieldScene::sceneModelFaces(std::string_view asset) {
  if (const auto found = sceneModels_.find(asset); found != sceneModels_.end()) {
    return found->second ? &*found->second : nullptr;
  }
  std::optional<std::vector<std::vector<BodyFace>>> objects;
  auto id = assets::AssetId::parse(asset);
  auto model = id ? assets::loadModel(*files_, *id)
                  : core::Result<assets::ModelData>{std::unexpected{id.error()}};
  if (model) {
    std::vector<rhi::TextureId> textures;
    for (const assets::Image& image : model->images) {
      if (auto made = renderer_->queueTexture(extentOf(image), image.pixels)) {
        textures.push_back(*made);
      } else {
        core::logWarning("field: no texture for {}: {}", asset, made.error().message);
        textures.push_back(renderer_->whiteTexture());
      }
    }
    const std::size_t count = model->skin ? model->skin->joints.size() : 1;
    objects.emplace();
    for (std::uint32_t object = 0; object < count; ++object) {
      objects->push_back(objectFaces(*model, object, glm::mat4{1.0F}, [&](std::uint32_t image) {
        return image < textures.size() ? textures[image] : renderer_->whiteTexture();
      }));
    }
  } else {
    core::logWarning("field: no model {}: {}", asset, model.error().message);
  }
  const auto placed = sceneModels_.emplace(std::string{asset}, std::move(objects)).first;
  return placed->second ? &*placed->second : nullptr;
}

} // namespace dwe::app
