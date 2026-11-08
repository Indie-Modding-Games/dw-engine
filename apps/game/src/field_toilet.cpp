#include "field_toilet.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/scene/components.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace dwe::app {

namespace {

constexpr std::string_view OpenMenuCommand = "open_menu";

const session::ToiletWalkRules* toiletWalkOf(const session::FieldItemsAdapter& fieldItems) {
  const auto& rules = fieldItems.rules();
  if (rules.has_value() && rules->toiletWalk.has_value()) {
    return &*rules->toiletWalk;
  }
  return nullptr;
}

// The map's toilet points (MAP_ENTRIES[map].toiletId, TOILET_DATA), if it has any.
const assets::MapToilet* toiletOf(const assets::MapTable* table, std::string_view mapId) {
  const assets::MapEntry* entry = table != nullptr ? table->find(mapId) : nullptr;
  if (entry == nullptr || !entry->toilet.has_value()) {
    return nullptr;
  }
  const std::int32_t index = *entry->toilet;
  const auto found = std::ranges::find(table->toilets, index, &assets::MapToilet::index);
  return found == table->toilets.end() ? nullptr : &*found;
}

} // namespace

FieldToilet::FieldToilet(session::GameSession& session,
                         const session::FieldItemsAdapter& fieldItems, FieldScene& scene)
    : session_{&session}, fieldItems_{&fieldItems}, scene_{&scene} {}

bool FieldToilet::handle(const ui::UiCommand& command) const {
  return command.name == OpenMenuCommand && active();
}

float FieldToilet::frames(std::int32_t count) const {
  return static_cast<float>(count) * scene_->field().rules().transition.frameSeconds;
}

bool FieldToilet::checkCell() {
  game::field::Field& field = scene_->field();
  const assets::CollisionCell cell = assets::collisionCellAt(field.player().position);
  if (active() ||
      std::cmp_not_equal(field.collision().trigger(cell).value_or(0),
                         field.rules().triggers.toilet) ||
      !session_->partner().goesToToilet()) {
    return false;
  }
  const session::ToiletWalkRules* rules = toiletWalkOf(*fieldItems_);
  // An extra toilet's cell (an engine improvement) goes to that toilet, any other to the map's.
  const game::field::ExtraToilet* extra = field.extraToiletAt(cell);
  extraPoints_ = extra != nullptr ? std::optional{extra->points()} : std::nullopt;
  const assets::MapToilet* toilet =
      extraPoints_ ? &*extraPoints_ : toiletOf(scene_->mapTable(), field.mapId());
  if (rules == nullptr || toilet == nullptr) {
    core::logDebug("[port:toilet] checkMapInteraction (tamer.c:689): toilet cell ({}, {}): no "
                   "toilet walk {}, the partner goes where it stands",
                   cell.column, cell.row, rules == nullptr ? "in the ruleset" : "for the map");
    goNow();
    return true;
  }
  // partner.c: tickPartnerToilet sub state 0: the points at the partner's height, the camera to
  // the second one, and a run to the first. TOILET_DATA is on the original's axes: its z is the
  // world's the other way (core::fromOriginalAxes).
  const float height = field.partner().position.y;
  first_ = toilet->firstPoint(height);
  second_ = toilet->secondPoint(height);
  rules_ = rules;
  field.holdForCare(true);
  field.panCameraByScript(second_, frames(rules->cameraFrames));
  field.walkPartnerTo(first_, scene::Motion::Run);
  step_ = Step::ToFirst;
  core::logDebug("[port:toilet] checkMapInteraction (tamer.c:689): toilet cell ({}, {}): section "
                 "0x4e2, call_partner_routine(0): tickPartnerToilet to toilet {}",
                 cell.column, cell.row, toilet->index);
  return false;
}

void FieldToilet::goNow() {
  session_->useToilet();
  const auto& rules = fieldItems_->rules();
  if (rules && session_->sound() != nullptr) {
    session_->sound()->playSound(rules->toiletSound.slot, rules->toiletSound.sound);
  }
}

void FieldToilet::finish() {
  scene_->playPartnerClip(std::nullopt);
  scene_->field().holdForCare(false);
  step_ = Step::None;
  rules_ = nullptr;
}

bool FieldToilet::tick(float seconds) {
  if (!active() || rules_ == nullptr) {
    return false;
  }
  scene_->tick({}, seconds);
  session_->tick(seconds);
  scene_->finishTick();
  game::field::Field& field = scene_->field();
  switch (step_) {
  case Step::ToFirst:
    if (!field.partnerWalking()) {
      field.walkPartnerTo(second_, scene::Motion::Run);
      step_ = Step::ToSecond;
    }
    return false;
  case Step::ToSecond:
    if (!field.partnerWalking()) {
      scene_->playPartnerClip(rules_->partnerClip);
      step_ = Step::Going;
    }
    return false;
  case Step::Going:
    // Sub state 3: handleToilet once the clip ends; the camera goes back to the player.
    if (scene_->partnerClipFinished()) {
      session_->useToilet();
      scene_->playPartnerClip(std::nullopt);
      field.panCameraByScript(field.player().position, frames(rules_->cameraFrames));
      field.walkPartnerTo(first_, scene::Motion::Walk);
      step_ = Step::Back;
      core::logDebug("[port:toilet] tickPartnerToilet (partner.c:577): handleToilet");
      return true;
    }
    return false;
  case Step::Back:
    // Sub state 4: back on the first point the script goes on and plays its sound.
    if (!field.partnerWalking()) {
      const auto& rules = fieldItems_->rules();
      if (rules && session_->sound() != nullptr) {
        session_->sound()->playSound(rules->toiletSound.slot, rules->toiletSound.sound);
      }
      finish();
    }
    return false;
  case Step::None:
    break;
  }
  return false;
}

} // namespace dwe::app
