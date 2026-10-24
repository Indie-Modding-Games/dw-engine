#include <dwe/animation/animation_player.hpp>
#include <dwe/animation/clip_sampling.hpp>
#include <dwe/game/field/field.hpp>

#include <algorithm>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

// anim.c: startAnimation switches clips at once.
constexpr float ScriptedClipFadeSeconds = 0.0F;

} // namespace

bool FieldScene::animateScripted(Actor& actor, double seconds) {
  const auto* scripted = world_.registry().try_get<game::field::ScriptedActor>(actor.entity);
  if (scripted == nullptr || !scripted->clip) {
    if (actor.scriptedClip) {
      // The script let go: the motion's clip starts again.
      actor.scriptedClip.reset();
      actor.motion.reset();
    }
    return false;
  }
  if (actor.scriptedClip != scripted->clip) {
    actor.scriptedClip = scripted->clip;
    actor.motion.reset();
    actor.player->play(*scripted->clip, ScriptedClipFadeSeconds);
  }
  // The script moves the character at the clip's own ground speed, so the clip plays at its speed;
  // a frozen clip stays on its frame.
  actor.player->setSpeed(1.0F);
  static_cast<void>(actor.player->advance(scripted->frozen ? 0.0F : static_cast<float>(seconds)));
  return true;
}

float FieldScene::clipGroundSpeed(scene::Entity entity, std::uint32_t clip) const {
  const Actor* actor = nullptr;
  if (entity == playerActor_->entity) {
    actor = playerActor_.get();
  } else if (entity == partnerActor_->entity) {
    actor = partnerActor_.get();
  } else if (const auto npc = std::ranges::find(npcActors_, entity, &Actor::entity);
             npc != npcActors_.end()) {
    // A map's NPC walks for the script too (script_interp.c: scriptSetEntityWalking), as the wild
    // one of the forest's first fight leaves; without its speed the script would wait forever.
    actor = npc->get();
  }
  if (actor == nullptr) {
    return 0.0F;
  }
  const assets::AnimationClip* found = actor->model->findClip(clip);
  return found == nullptr ? 0.0F : animation::repeatGroundSpeed(*actor->model, *found);
}

} // namespace dwe::app
