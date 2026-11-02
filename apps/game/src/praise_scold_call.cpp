#include "praise_scold_call.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/partner/partner.hpp>

#include "common.hpp"

namespace dwe::app {

PraiseScoldCall::PraiseScoldCall(session::GameSession& session, ui::Ui& ui)
    : session_{&session}, ui_{&ui} {}

bool PraiseScoldCall::handle(const ui::UiCommand& command) {
  if (command.name == "open_menu") {
    return active();
  }
  const bool praise = command.name == FieldPraiseCommand;
  if (!praise && command.name != FieldScoldCommand) {
    return false;
  }
  if (fieldIdle_ && !active()) {
    ui_->hide(MenuDocument);
    scold_ = !praise;
    walkUp_.start();
    step_ = Step::Walk;
    core::logDebug("[port:partner_call] tickTriangleMenu (game_menu.c:2190-2210): {}: partner "
                   "state {}",
                   praise ? "praise" : "scold", praise ? 15 : 4);
  }
  return true;
}

void PraiseScoldCall::update(bool fieldIdle) {
  fieldIdle_ = fieldIdle;
}

void PraiseScoldCall::gesture(FieldScene& scene, const session::FieldItemsRules* rules) {
  if (rules != nullptr && session_->sound() != nullptr) {
    // partner.c:389 plays the praise's sound, tamer.c:1646 the scold's.
    const session::SoundCue& cue = scold_ ? rules->scoldSound : rules->praiseSound;
    session_->sound()->playSound(cue.slot, cue.sound);
  }
  const session::PraiseScoldRules* clips =
      rules != nullptr && rules->praiseScold ? &*rules->praiseScold : nullptr;
  if (clips == nullptr) {
    // Without the ruleset's clips it counts at once. The scene ends here, so the care hold the
    // walk-up took has to go too (finish() is not reached on this path).
    apply();
    scene.field().holdForCare(false);
    step_ = Step::None;
    return;
  }
  // tamer.c: tamerTickPraiseScold sub state 0: the player's gesture and the partner's answer; a
  // scold right after a refusal or a pile on the ground (ITEM_SCOLD_FLAG 1) gets another answer.
  const game::partner::Partner& partner = session_->partner();
  if (scold_) {
    const bool boosted = partner.state().mood.scoldBoost == game::partner::ScoldBoost::Ready;
    scene.setPlayerClip(clips->scoldPlayerClip);
    scene.playPartnerClip(boosted ? clips->boostedScoldPartnerClip : clips->scoldPartnerClip);
  } else {
    scene.setPlayerClip(clips->praisePlayerClipFor(
        static_cast<std::int32_t>(partner.species().level), partner.species().id));
    scene.playPartnerClip(clips->praisePartnerClip);
  }
  scene.field().holdForCare(true);
  step_ = Step::Gesture;
  core::logDebug("[port:partner_call] tamerTickPraiseScold (tamer.c:1629): the {} gesture",
                 scold_ ? "scold" : "praise");
}

void PraiseScoldCall::apply() {
  core::logDebug("[port:partner_call] partnerTickPraiseScold (partner.c:398): handlePraiseScold "
                 "({})",
                 scold_ ? "scold" : "praise");
  if (scold_) {
    session_->scold();
  } else {
    session_->praise();
  }
}

void PraiseScoldCall::finish(FieldScene& scene) {
  scene.setPlayerClip(std::nullopt);
  scene.playPartnerClip(std::nullopt);
  scene.field().holdForCare(false);
  step_ = Step::None;
  core::logDebug("[port:partner_call] partnerTickPraiseScold (partner.c:412): the partner's clip "
                 "ended");
}

bool PraiseScoldCall::tick(FieldScene& scene, float seconds,
                           const session::FieldItemsAdapter& fieldItems) {
  switch (step_) {
  case Step::None:
    return false;
  case Step::Walk:
    if (!walkUp_.tick(scene, seconds, session_)) {
      return false;
    }
    gesture(scene, fieldItems.rules() ? &*fieldItems.rules() : nullptr);
    return step_ == Step::None;
  case Step::Gesture:
  case Step::Answer:
    break;
  }
  scene.tick({}, seconds);
  session_->tick(seconds);
  scene.finishTick();
  if (step_ == Step::Gesture) {
    // tamer.c: tamerTickPraiseScold sub state 1: past the end of the gesture the player stands
    // (tamer state 6, the idle clip), and partner.c: partnerTickPraiseScold sub state 2 counts it.
    if (!scene.playerClipPlayedThrough()) {
      return false;
    }
    scene.setPlayerClip(std::nullopt);
    apply();
    step_ = Step::Answer;
    return true;
  }
  // Sub state 3: the partner's answer ends after its first pass, one that repeats too (loopCount
  // 255 set to 1); then the partner follows again and the camera the player.
  if (scene.partnerClipFinished()) {
    finish(scene);
  }
  return false;
}

} // namespace dwe::app
