#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/content_id.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/scene/components.hpp>

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "field_scene.hpp"
#include "messages.hpp"

namespace dwe::app {

namespace {

// anim.c: startAnimation switches clips at once.
constexpr float FighterClipFadeSeconds = 0.0F;
// A stage fighter is drawn as its species with models/partner's animations, as an NPC is
// (field_scene_npcs.cpp), from the species' folder (story_npcs.cpp: speciesModel).
constexpr std::string_view StageFighterCharacter = "models/partner";
constexpr std::string_view SpeciesModelLeaf = "model";

} // namespace

FieldScene::Actor* FieldScene::fighterActor(std::optional<std::int32_t> npc) const {
  if (!npc) {
    return partnerActor_.get();
  }
  if (const auto staged = std::ranges::find(
          stageFighters_, *npc, &std::pair<std::int32_t, std::unique_ptr<Actor>>::first);
      staged != stageFighters_.end()) {
    return staged->second.get();
  }
  const std::optional<scene::Entity> entity = field_->npcs().entityOf(*npc);
  if (!entity) {
    return nullptr;
  }
  const auto found = std::ranges::find(npcActors_, *entity, &Actor::entity);
  return found != npcActors_.end() ? found->get() : nullptr;
}

void FieldScene::setNpcSoundSlot(scene::Entity npc, std::int64_t slot) {
  const auto found = std::ranges::find(npcActors_, npc, &Actor::entity);
  if (found == npcActors_.end()) {
    return;
  }
  core::logDebug("[port:sound] entity {} vabId {}", static_cast<std::uint32_t>(npc), slot);
  (*found)->soundSlot = slot;
}

// The battle moves the fighter's entity itself (battle_main.c: the fighters' positions and
// rotations are their entities'), so the field finds it where the battle left it.
bool FieldScene::holdFighter(std::optional<std::int32_t> npc, const FighterFigure& figure) {
  Actor* actor = fighterActor(npc);
  if (actor == nullptr) {
    return false;
  }
  auto& transform = world_.registry().get<scene::Transform>(actor->entity);
  scene::beginTick(transform);
  transform.position.x = figure.x;
  transform.position.z = figure.z;
  transform.yaw = figure.yaw;
  const bool starts = !actor->fighter || actor->fighter->clip != figure.clip ||
                      actor->fighter->starts != figure.starts;
  if (!actor->fighter) {
    actor->shownWhenHeld = actor == playerActor_.get() || onScreen(*actor);
    core::logDebug("[port:battle] loadBattleData (btl.c:412): entity {} vabId {}, isOnScreen {}",
                   static_cast<std::uint32_t>(actor->entity), figure.soundSlot,
                   actor->shownWhenHeld ? 1 : 0);
  }
  // btl.c: loadBattleData sets the enemies' stats.current.vabId, which they keep after it.
  actor->soundSlot = figure.soundSlot;
  actor->fighter = figure;
  if (!starts) {
    return true;
  }
  // The field's motion picks its clip again once the battle lets go.
  actor->motion.reset();
  if (actor->player->play(figure.clip, FighterClipFadeSeconds)) {
    core::logDebug("[port:battle] startAnimation (anim.c:201): entity {} plays battle clip {:#x}{}",
                   static_cast<std::uint32_t>(actor->entity), figure.clip,
                   figure.frozen ? " on its first frame" : "");
  } else {
    core::logWarning("{}", core::formatMessage(messages::FighterClipMissing, figure.clip,
                                               actor->character.model.str()));
  }
  return true;
}

std::optional<glm::vec2> FieldScene::fighterFeetOnWindow(std::optional<std::int32_t> npc) const {
  const Actor* actor = fighterActor(npc);
  if (actor == nullptr) {
    return std::nullopt;
  }
  return worldToWindowFraction(world_.registry().get<scene::Transform>(actor->entity).position);
}

std::optional<glm::vec2> FieldScene::fighterNodeOnWindow(std::optional<std::int32_t> npc,
                                                         std::uint32_t node) const {
  const Actor* actor = fighterActor(npc);
  if (actor == nullptr) {
    return std::nullopt;
  }
  const std::optional<glm::vec3> point = nodePoint(*actor, node, glm::vec3{0.0F});
  if (!point) {
    return std::nullopt;
  }
  return worldToWindowFraction(*point);
}

std::optional<glm::vec2> FieldScene::playerNodeOnWindow(std::uint32_t node) const {
  const std::optional<glm::vec3> point = nodePoint(*playerActor_, node, glm::vec3{0.0F});
  if (!point) {
    return std::nullopt;
  }
  return worldToWindowFraction(*point);
}

bool FieldScene::animateFighter(Actor& actor) const {
  if (!actor.fighter) {
    return false;
  }
  actor.player->setSpeed(1.0F);
  // A frozen clip stands on its frame (anim.c: tickAnimation skips an entity without animFlag 1).
  static_cast<void>(
      actor.player->advance(actor.fighter->frozen ? 0.0F : static_cast<float>(fighterSeconds_)));
  return true;
}

bool FieldScene::fighterPlayedThrough(std::optional<std::int32_t> npc,
                                      const FighterFigure& figure) {
  const Actor* actor = fighterActor(npc);
  if (actor == nullptr || !actor->fighter) {
    return true;
  }
  return actor->fighter->clip == figure.clip && actor->fighter->starts == figure.starts &&
         actor->player->playedThrough();
}

const assets::ModelData* FieldScene::fighterModel(std::optional<std::int32_t> npc) const {
  const Actor* actor = fighterActor(npc);
  return actor != nullptr ? actor->model.get() : nullptr;
}

void FieldScene::releaseFighters() {
  const auto release = [this](Actor& actor) {
    if (!actor.fighter) {
      return;
    }
    actor.fighter.reset();
    actor.motion.reset();
    scene::beginTick(world_.registry().get<scene::Transform>(actor.entity));
    // The battle moved it: an NPC's path starts over from where it stands (the engine's, as a
    // hold does), so it does not head back to a waypoint target it had before the fight.
    if (const auto* tag = world_.registry().try_get<game::field::NpcTag>(actor.entity)) {
      static_cast<void>(field_->npcs().restartPath(tag->scriptId));
    }
  };
  release(*partnerActor_);
  for (const std::unique_ptr<Actor>& actor : npcActors_) {
    release(*actor);
  }
  for (const auto& [id, actor] : stageFighters_) {
    release(*actor);
  }
  // The partner stands again with a new wait before its gestures.
  partnerLook_ = {};
  fighterSecondsDue_ = 0.0;
  // The player is no obstacle in battle (as in the original), so the partner may end the fight on
  // the player: the release rule sets whoever stands in someone apart.
  field_->releaseHandoff("battle");
  core::logDebug("[port:battle] BTL_battleMain (battle_main.c:778): the field takes the fighters "
                 "back where they stand");
}

bool FieldScene::addStageFighter(std::int32_t id, std::string_view species, glm::vec2 ground,
                                 float yaw) {
  const std::string model = std::format("{}/{}/{}", assets::SpeciesFolder,
                                        assets::contentAssetPath(species), SpeciesModelLeaf);
  auto modelId = assets::AssetId::parse(model);
  if (!modelId) {
    core::logWarning("field: the stage fighter {} ({}) is not drawn: {}", id, species,
                     modelId.error().message);
    return false;
  }
  const scene::Entity entity = world_.create();
  entt::registry& registry = world_.registry();
  scene::place(registry.emplace<scene::Transform>(entity), {ground.x, 0.0F, ground.y}, yaw);
  registry.emplace<scene::ModelRef>(entity, model);
  registry.emplace<scene::AnimationIntent>(entity, scene::AnimationIntent{});
  const game::field::NpcRules& rules = field_->rules().npc;
  auto actor = loadActor(StageFighterCharacter, entity,
                         {.model = model,
                          .radius = 0.0F,
                          .walkSpeed = rules.walkSpeed,
                          .runSpeed = rules.runSpeed,
                          .turnSpeed = rules.turnSpeed},
                         *modelId, MissingClips::Drop);
  if (!actor) {
    core::logWarning("field: the stage fighter {} ({}) is not drawn: {}", id, species,
                     actor.error().message);
    world_.destroy(entity);
    return false;
  }
  stageFighters_.emplace_back(id, std::move(*actor));
  return true;
}

void FieldScene::clearStageFighters() {
  for (const auto& [id, actor] : stageFighters_) {
    models_->remove(actor->handle);
    world_.destroy(actor->entity);
  }
  stageFighters_.clear();
}

} // namespace dwe::app
