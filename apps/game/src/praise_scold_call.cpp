#include "praise_scold_call.hpp"

#include <dwe/core/log.hpp>

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
    core::logDebug("[port:partner_call] tickTriangleMenu (game_menu.c:2190-2210): {}: partner "
                   "state {}",
                   praise ? "praise" : "scold", praise ? 15 : 4);
  }
  return true;
}

void PraiseScoldCall::update(bool fieldIdle) {
  fieldIdle_ = fieldIdle;
}

bool PraiseScoldCall::tick(FieldScene& scene, float seconds,
                           const session::FieldItemsAdapter& fieldItems) {
  if (!walkUp_.tick(scene, seconds, session_)) {
    return false;
  }
  const auto& rules = fieldItems.rules();
  if (rules && session_->sound() != nullptr) {
    const session::SoundCue& cue = scold_ ? rules->scoldSound : rules->praiseSound;
    session_->sound()->playSound(cue.slot, cue.sound);
  }
  core::logDebug("[port:partner_call] partnerTickPraiseScold (partner.c:398): handlePraiseScold "
                 "({})",
                 scold_ ? "scold" : "praise");
  if (scold_) {
    session_->scold();
  } else {
    session_->praise();
  }
  return true;
}

} // namespace dwe::app
