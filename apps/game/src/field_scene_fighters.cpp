#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/content_id.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/grid.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/scene/components.hpp>

#include <algorithm>
#include <cstddef>
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
  const std::optional<scene::Entity> entity =
      *npc >= 0 ? field_->npcs().entityAt(static_cast<std::size_t>(*npc)) : std::nullopt;
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

void FieldScene::settleActors(double seconds) {
  field_->settle();
  tickSeconds_ = seconds;
  const auto glide = [seconds](Actor& actor) {
    if (actor.fighter) {
      actor.fighterGlide.advance(seconds);
    }
  };
  glide(*partnerActor_);
  for (const auto& actor : npcActors_) {
    glide(*actor);
  }
  for (const auto& staged : stageFighters_) {
    glide(*staged.second);
  }
  effectsSeconds_ = std::min(effectsSeconds_ + seconds, BattleFrameSeconds);
}

void FieldScene::setEffects(std::vector<SceneEffect> effects, std::uint64_t frame) {
  if (frame != effectsFrame_) {
    // Only the frame right after the last glides from it (not a new battle's first).
    const bool next = frame == effectsFrame_ + 1;
    effectsFrame_ = frame;
    effectsFrom_ = std::exchange(effectsTo_, std::move(effects));
    if (!next) {
      effectsFrom_.clear();
    }
    effectsSeconds_ = 0.0;
  } else {
    effectsTo_ = std::move(effects);
  }
}

scene::Pose FieldScene::heldPlace(const Actor& actor) const {
  if (drawAhead_ && actor.fighter && actor.fighterGlide.started() && field_->smoothMotion()) {
    return actor.fighterGlide.drawn(*drawAhead_);
  }
  const auto& transform = world_.registry().get<scene::Transform>(actor.entity);
  return {.position = transform.position, .yaw = transform.yaw};
}

scene::Pose FieldScene::drawnPose(scene::Entity entity, float alpha) const {
  if (drawAhead_ && field_->smoothMotion()) {
    const auto held = [entity](const Actor& actor) {
      return actor.entity == entity && actor.fighter && actor.fighterGlide.started();
    };
    if (held(*partnerActor_)) {
      return heldPlace(*partnerActor_);
    }
    for (const auto& actor : npcActors_) {
      if (held(*actor)) {
        return heldPlace(*actor);
      }
    }
    for (const auto& staged : stageFighters_) {
      if (held(*staged.second)) {
        return heldPlace(*staged.second);
      }
    }
  }
  return field_->drawnPose(entity, alpha);
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
  // Smooth motion draws it gliding to its frame's place (battle_glide.hpp); the first hold puts it
  // there.
  actor->fighterGlide.moveTo({.position = transform.position, .yaw = transform.yaw});
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
                                                         std::uint32_t node,
                                                         float rightward) const {
  const Actor* actor = fighterActor(npc);
  if (actor == nullptr) {
    return std::nullopt;
  }
  const std::optional<glm::vec3> point = nodePoint(*actor, node, glm::vec3{0.0F});
  if (!point) {
    return std::nullopt;
  }
  // The view's first row is the camera's right in the world.
  const glm::mat4& view = mapRenderer_->camera().view();
  const glm::vec3 right{view[0][0], view[1][0], view[2][0]};
  return worldToWindowFraction(*point + (right * rightward));
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
  actor.player->setSpeed(actor.fighter->pace);
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
  // A fight can end with a fighter on ground the field will not let an NPC stand on: inside a
  // wall it crossed thin, behind a counter, past the map's edge. NPCs never move on their own, so
  // there the player cannot reach them again (the off-map fighters of #163): put the NPC back at
  // its record's spot, where the map loaded it (an engine improvement,
  // docs/modulos/game/battle.md).
  const auto standOnFreeGround = [this](std::size_t slot, scene::Entity entity) {
    const assets::MapNpc* record = field_->npcs().record(slot);
    if (record == nullptr || !record->digimon || !speciesRadius_) {
      return;
    }
    auto& transform = world_.registry().get<scene::Transform>(entity);
    if (game::field::fitsOnGrid(field_->collision(), transform.position,
                                speciesRadius_(*record->digimon))) {
      return;
    }
    const glm::vec3 home = assets::toWorldPosition(record->position);
    core::logWarning(
        "[port:battle] the fight left the NPC in slot {} at ({:.0f}, {:.0f}), not free ground; it stands at "
        "its record's spot again ({:.0f}, {:.0f})",
        slot, transform.position.x, transform.position.z, home.x, home.z);
    transform.position = home;
  };
  const auto release = [this, &standOnFreeGround](Actor& actor) {
    if (!actor.fighter) {
      return;
    }
    actor.fighter.reset();
    actor.fighterGlide = {};
    actor.motion.reset();
    scene::beginTick(world_.registry().get<scene::Transform>(actor.entity));
    // The battle moved it: an NPC's path starts over from where it stands (the engine's, as a
    // hold does), so it does not head back to a waypoint target it had before the fight.
    if (const auto slot = field_->npcs().slotOf(actor.entity)) {
      standOnFreeGround(*slot, actor.entity);
      static_cast<void>(field_->npcs().restartPathAt(*slot));
    }
  };
  release(*partnerActor_);
  // The NPCs that ran off left the map at the intro's end (battle_setup.c:316) while the field
  // did not tick: the battle's hold on them went with them, so there is nothing to give back.
  forgetGoneNpcs();
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
