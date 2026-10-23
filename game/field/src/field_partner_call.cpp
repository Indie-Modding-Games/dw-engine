#include <dwe/assets/map_document.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/scene/components.hpp>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace dwe::game::field {

bool Field::partnerBeside() const {
  const glm::vec3 player = world_->registry().get<scene::Transform>(player_).position;
  const glm::vec3 partner = world_->registry().get<scene::Transform>(partner_).position;
  const float distance = glm::length(glm::vec2{player.x - partner.x, player.z - partner.z});
  return distance < rules_.partner.walkDistance * rules_.partner.mover.radius;
}

void Field::playerFacePartner() {
  auto& player = world_->registry().get<scene::Transform>(player_);
  const glm::vec3 partner = world_->registry().get<scene::Transform>(partner_).position;
  const glm::vec3 toPartner = partner - player.position;
  if (toPartner.x != 0.0F || toPartner.z != 0.0F) {
    player.yaw = assets::yawOfDirection(toPartner);
  }
}

} // namespace dwe::game::field
