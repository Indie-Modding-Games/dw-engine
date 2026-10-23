#include "field_poop.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/partner/poop_piles.hpp>
#include <dwe/scene/components.hpp>

#include <cmath>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::app {

namespace {

constexpr std::string_view OpenMenuCommand = "open_menu";

glm::vec3 centreOf(game::partner::Tile tile) {
  return assets::collisionCellCenter({.column = tile.x, .row = tile.y});
}

const session::WildPoopRules* wildOf(const session::PoopAdapter& poop) {
  const auto& rules = poop.rules();
  return rules.has_value() && rules->wild.has_value() ? &*rules->wild : nullptr;
}

const session::EatPoopRules* eatOf(const session::PoopAdapter& poop) {
  const auto& rules = poop.rules();
  return rules.has_value() && rules->eat.has_value() ? &*rules->eat : nullptr;
}

} // namespace

FieldPoop::FieldPoop(session::GameSession& session, session::PoopAdapter& poop, FieldScene& scene)
    : session_{&session}, poop_{&poop}, scene_{&scene} {}

bool FieldPoop::handle(const ui::UiCommand& command) const {
  return command.name == OpenMenuCommand && active();
}

void FieldPoop::placeOne() {
  const game::field::Field& field = scene_->field();
  const game::field::CharacterState partner = field.partner();
  const assets::CollisionCell cell = assets::collisionCellAt(partner.position);
  poop_->placePending(*session_, field.mapId(), {.x = cell.column, .y = cell.row},
                      assets::yawToOriginal(partner.yaw));
}

void FieldPoop::update() {
  game::field::Field& field = scene_->field();
  if (active() || field.mapId().empty()) {
    return;
  }
  // A pile left off the field's scenes (handleSleeping, on waking) goes down at once.
  while (poop_->nextPending().has_value()) {
    placeOne();
  }
  if (session_->partner().wildPoopDue()) {
    if (wildOf(*poop_) == nullptr) {
      // Without the scene's rules the pile and its care effects come at once.
      session_->leaveWildPile();
      placeOne();
      session_->finishWildPoop();
      return;
    }
    // partner.c: tickWildPoop sub state 0: the player stops and looks at the partner, which runs
    // up to it.
    field.holdForCare(true);
    field.runPartnerUp();
    step_ = Step::RunUp;
    core::logDebug("[port:poop] tickWildPoop (partner.c:593): partner state 7");
    return;
  }
  checkEating();
}

void FieldPoop::leavePile() {
  // Sub state 2: createPoopPile and handleWildPoop, the care mistake, mood and weight with it.
  session_->leaveWildPile();
  placeOne();
  core::logDebug("[port:poop] tickWildPoop (partner.c:620): createPoopPile, handleWildPoop");
}

void FieldPoop::endScene() {
  // Sub state 3: the player is free again and the tamer level may drop.
  session_->finishWildPoop();
  finish();
  core::logDebug("[port:poop] tickWildPoop (partner.c): sub state 3, addTamerLevel");
}

void FieldPoop::checkEating() {
  const session::EatPoopRules* eat = eatOf(*poop_);
  if (eat == nullptr || !session_->partner().eatsPoop()) {
    return;
  }
  game::field::Field& field = scene_->field();
  const glm::vec3 player = field.player().position;
  // partner_impl.c: tickPoopDetection: the first pile of the map with the player in its box.
  for (const game::partner::PoopPile& pile : poop_->onMap(field.mapId())) {
    const glm::vec3 centre = centreOf(pile.tile);
    const auto reach = static_cast<float>(eat->reachOf(pile.size));
    if (std::abs(player.x - centre.x) >= reach || std::abs(player.z - centre.z) >= reach) {
      continue;
    }
    pileTile_ = pile.tile;
    field.holdForCare(true);
    field.walkPartnerTo({centre.x, field.partner().position.y, centre.z}, scene::Motion::Walk);
    step_ = Step::ToPile;
    core::logDebug("[port:poop] tickPoopDetection (partner_impl.c:1688): pile ({}, {}) size {}: "
                   "partner state 9, partnerTickEatShit",
                   pile.tile.x, pile.tile.y, pile.size);
    return;
  }
}

void FieldPoop::finish() {
  scene_->playPartnerClip(std::nullopt);
  scene_->field().holdForCare(false);
  step_ = Step::None;
}

bool FieldPoop::tick(float seconds) {
  if (!active()) {
    return false;
  }
  scene_->tick({}, seconds);
  session_->tick(seconds);
  scene_->finishTick();
  const game::field::Field& field = scene_->field();
  const session::WildPoopRules* wild = wildOf(*poop_);
  const session::EatPoopRules* eat = eatOf(*poop_);
  switch (step_) {
  case Step::RunUp:
    // Sub state 1: close enough, the partner squats.
    if (!field.partnerWalking()) {
      if (wild == nullptr) {
        leavePile();
        endScene();
        return false;
      }
      scene_->playPartnerClip(wild->squatClip);
      step_ = Step::Squat;
    }
    return false;
  case Step::Squat:
    // Sub state 2: at the clip's end, the pile and the after clip.
    if (scene_->partnerClipFinished()) {
      leavePile();
      if (wild == nullptr) {
        endScene();
        return false;
      }
      scene_->playPartnerClip(wild->afterClip);
      step_ = Step::After;
    }
    return false;
  case Step::After:
    if (scene_->partnerClipFinished()) {
      endScene();
    }
    return false;
  case Step::ToPile:
    // partnerTickEatShit sub state 1: on the pile, the partner eats.
    if (!field.partnerWalking()) {
      if (eat == nullptr) {
        finish();
        return false;
      }
      scene_->playPartnerClip(eat->eatClip);
      step_ = Step::Eat;
    }
    return false;
  case Step::Eat:
    // Sub state 3: handleEatingPoop at the clip's end.
    if (scene_->partnerClipFinished()) {
      const std::optional<std::int32_t> size = poop_->remove(field.mapId(), pileTile_);
      if (size) {
        session_->eatPoop(*size);
        core::logDebug("[port:poop] partnerTickEatShit (partner.c:721): handleEatingPoop, size {}",
                       *size);
      }
      finish();
      return size.has_value();
    }
    return false;
  case Step::None:
    break;
  }
  return false;
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
    glm::vec3 centre = centreOf(pile.tile);
    centre.y = height;
    const float scale = static_cast<float>(pile.size) / static_cast<float>(rules->look.modelSize);
    places.push_back(glm::scale(glm::translate(glm::mat4{1.0F}, centre), glm::vec3{scale}));
  }
  scene_->setProps(rules->look.model, std::move(places));
}

} // namespace dwe::app
