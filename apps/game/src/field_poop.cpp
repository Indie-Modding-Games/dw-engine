#include "field_poop.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/partner/poop_piles.hpp>

#include <utility>
#include <vector>

namespace dwe::app {

FieldPoop::FieldPoop(session::GameSession& session, session::PoopAdapter& poop, FieldScene& scene)
    : session_{&session}, poop_{&poop}, scene_{&scene} {}

void FieldPoop::placePending() {
  const game::field::Field& field = scene_->field();
  if (field.mapId().empty()) {
    return;
  }
  const game::field::CharacterState partner = field.partner();
  const assets::CollisionCell cell = assets::collisionCellAt(partner.position);
  while (poop_->nextPending()) {
    poop_->placePending(*session_, field.mapId(), {.x = cell.column, .y = cell.row},
                        assets::yawToOriginal(partner.yaw));
  }
}

void FieldPoop::show() {
  const auto& rules = poop_->rules();
  if (!rules) {
    return;
  }
  const game::field::Field& field = scene_->field();
  // renderPoop: each pile at its tile's centre, at the partner's height, scaled by its size.
  const float height = field.partner().position.y;
  std::vector<glm::mat4> places;
  for (const game::partner::PoopPile& pile : poop_->onMap(field.mapId())) {
    glm::vec3 centre = assets::collisionCellCenter({.column = pile.tile.x, .row = pile.tile.y});
    centre.y = height;
    const float scale = static_cast<float>(pile.size) / static_cast<float>(rules->look.modelSize);
    places.push_back(glm::scale(glm::translate(glm::mat4{1.0F}, centre), glm::vec3{scale}));
  }
  scene_->setProps(rules->look.model, std::move(places));
}

} // namespace dwe::app
