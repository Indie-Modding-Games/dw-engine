#include <dwe/core/log.hpp>
#include <dwe/game/field/npcs.hpp>

#include <algorithm>
#include <ranges>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

// An NPC is drawn as its species with models/partner's animations, whose clip ids every species
// shares (as FieldScene::showPartnerSpecies draws the partner); those its model lacks are left out.
constexpr std::string_view NpcCharacter = "models/partner";

} // namespace

void FieldScene::followNpcs() {
  const auto gone = [this](const std::unique_ptr<Actor>& actor) {
    return !world_.alive(actor->entity);
  };
  for (const std::unique_ptr<Actor>& actor : npcActors_ | std::views::filter(gone)) {
    models_->remove(actor->handle);
  }
  std::erase_if(npcActors_, gone);
  std::erase_if(npcsNotShown_, [this](scene::Entity entity) { return !world_.alive(entity); });

  const game::field::NpcRules& rules = field_->rules().npc;
  const auto view = world_.registry().view<const game::field::NpcTag, const scene::ModelRef>();
  for (const auto [entity, tag, modelRef] : view.each()) {
    if (std::ranges::contains(npcActors_, entity, &Actor::entity) ||
        std::ranges::contains(npcsNotShown_, entity)) {
      continue;
    }
    auto model = assets::AssetId::parse(modelRef.asset);
    auto actor = model ? loadActor(NpcCharacter, entity,
                                   {.model = modelRef.asset,
                                    .radius = 0.0F,
                                    .walkSpeed = rules.walkSpeed,
                                    .runSpeed = rules.runSpeed,
                                    .turnSpeed = rules.turnSpeed},
                                   *model, MissingClips::Drop)
                       : core::Result<std::unique_ptr<Actor>>{std::unexpected{model.error()}};
    if (!actor) {
      core::logWarning("field: the NPC with script id {} is not drawn: {}", tag.scriptId,
                       actor.error().message);
      npcsNotShown_.push_back(entity);
      continue;
    }
    npcActors_.push_back(std::move(*actor));
  }
}

void FieldScene::drawNpcs(double animationSeconds, float alpha) {
  for (const std::unique_ptr<Actor>& actor : npcActors_) {
    if (!world_.alive(actor->entity)) {
      continue;
    }
    animate(*actor, animationSeconds);
    playClipSounds(*actor);
    submit(*actor, alpha);
  }
}

bool FieldScene::place(const Placement& placement) {
  std::optional<scene::Entity> entity;
  switch (placement.who) {
  case Placement::Who::Player:
    entity = field_->playerEntity();
    break;
  case Placement::Who::Partner:
    entity = field_->partnerEntity();
    break;
  case Placement::Who::Npc:
    entity = field_->npcs().entityOf(placement.npc);
    break;
  }
  if (!entity) {
    return false;
  }
  const float height = world_.registry().get<scene::Transform>(*entity).position.y;
  field_->placeAt(*entity, {placement.ground.x, height, placement.ground.y}, placement.yaw);
  if (placement.clip) {
    field_->playPartnerFrom(placement.clip->id, static_cast<std::size_t>(placement.clip->frame),
                            {.x = placement.clip->fractionX, .z = placement.clip->fractionZ});
  }
  return true;
}

} // namespace dwe::app
