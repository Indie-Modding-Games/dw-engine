#include "story_naming.hpp"

#include <dwe/core/log.hpp>

#include <string>
#include <utility>

namespace dwe::app {

namespace {

// script_menu.c: initializeNamingBuffer: bit 0 names the partner, bit 1 is the scripts' renaming
// (without it the routine is the whole opening, which only main.c: newGameScene runs).
constexpr std::int64_t PartnerBit = 1;
constexpr std::int64_t RenameBit = 2;

} // namespace

bool StoryNaming::take(const session::StoryScreenRequest& request) {
  if (request.screen != session::StoryScreen::NewGameMenu) {
    return false;
  }
  if ((request.first & RenameBit) == 0 || box_ == nullptr || session_ == nullptr) {
    core::logDebug("[port:story] call_routine 32 (script_menu.c:1121): flags {}: no renaming, "
                   "over at once",
                   request.first);
    waiting_ = request.waits;
    done_ = true;
    return true;
  }
  target_ = (request.first & PartnerBit) != 0 ? NameTarget::Partner : NameTarget::Tamer;
  const session::PlayerNames& names = session_->names();
  core::logDebug("[port:story] initializeNamingBuffer (script_menu.c:1121): flags {}, the {} name",
                 request.first, target_ == NameTarget::Tamer ? "tamer's" : "partner's");
  box_->open(target_, target_ == NameTarget::Tamer ? names.tamer : names.partner);
  waiting_ = request.waits;
  done_ = false;
  return true;
}

bool StoryNaming::press(const input::InputState& input) {
  if (box_ == nullptr || !box_->isOpen() || done_) {
    return false;
  }
  if (box_->press(input)) {
    // newGameStateMachine 0x15: the name goes to DIGIMON_NAME(0) or PARTNER_ENTITY.name.
    session::PlayerNames names = session_->names();
    (target_ == NameTarget::Tamer ? names.tamer : names.partner) = box_->name();
    session_->setNames(std::move(names));
    box_->close();
    done_ = true;
  }
  return true;
}

void StoryNaming::update(session::StoryScenes& scenes) {
  if (!done_) {
    return;
  }
  done_ = false;
  // newGameStateMachine 0x16: the routine ends once the boxes are closed.
  if (std::exchange(waiting_, false)) {
    scenes.screenClosed();
  }
}

} // namespace dwe::app
