#include "sleep_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <algorithm>
#include <string>
#include <variant>

#include "common.hpp"
#include "messages.hpp"
#include "title_screen.hpp"

namespace dwe::app {
namespace {

// The sleep's fades last as long as a map change's.
double fadeDuration() {
  const game::field::TransitionRules rules;
  return static_cast<double>(rules.fadeOutFrames) * static_cast<double>(rules.frameSeconds);
}

} // namespace

SleepScreen::SleepScreen(session::GameSession& session, ui::Ui& ui)
    : session_(&session), ui_(&ui),
      saves_(session.slots(), [&session](game::save::SlotId slot) { return session.save(slot); }) {}

float SleepScreen::fadeLevel() const {
  const double progress = std::clamp(fadeSeconds_ / fadeDuration(), 0.0, 1.0);
  switch (phase_) {
  case Phase::FadingOut:
    return static_cast<float>(progress);
  case Phase::Menu:
    return 1.0F;
  case Phase::FadingIn:
    return static_cast<float>(1.0 - progress);
  case Phase::Awake:
    break;
  }
  return 0.0F;
}

void SleepScreen::tick(double seconds) {
  // The pause menu does not open over a sleep.
  if (phase_ != Phase::Awake && ui_->isVisible(MenuDocument)) {
    ui_->hide(MenuDocument);
  }
  if (phase_ != Phase::FadingOut && phase_ != Phase::FadingIn) {
    return;
  }
  fadeSeconds_ += seconds;
  if (fadeSeconds_ < fadeDuration()) {
    return;
  }
  if (phase_ == Phase::FadingOut) {
    fallAsleep();
  } else {
    phase_ = Phase::Awake;
  }
}

bool SleepScreen::handle(const ui::UiCommand& command) {
  const std::string& name = command.name;
  if (name == "sleep") {
    if (phase_ == Phase::Awake && session_->partner().canSleep()) {
      ui_->hide(MenuDocument);
      phase_ = Phase::FadingOut;
      fadeSeconds_ = 0.0;
    }
    return true;
  }
  if (name == "open_menu") {
    return active();
  }
  if (phase_ != Phase::Menu) {
    return false;
  }
  if (name == "sleep_only") {
    ui_->hide(SleepMenuDocument);
    wakeUp();
  } else if (name == "sleep_and_save") {
    ui_->hide(SleepMenuDocument);
    follow(saves_.save({.home = session_->homeSlot(), .started = session_->playthroughStarted()}));
  } else if (name == "dialog_answer" && dialogShown_) {
    const bool* yes = std::get_if<bool>(&command.value);
    ui_->hide(DialogDocument);
    dialogShown_ = false;
    follow(saves_.answer(yes != nullptr && *yes));
  } else {
    return false;
  }
  return true;
}

void SleepScreen::update() {
  const bool canSleep = session_->partner().canSleep();
  if (canSleep_ != canSleep) {
    canSleep_ = canSleep;
    ui_->setSetting("pause_menu.sleep_enabled", canSleep);
  }
}

void SleepScreen::fallAsleep() {
  if (auto slept = session_->sleep({.hasPillow = false, .area = session_->areaResponse()});
      !slept) {
    core::logWarning(messages::SleepFailed, slept.error().message);
    wakeUp();
    return;
  }
  // Without a slot the playthrough cannot be saved, so there is nothing to ask.
  if (session_->homeSlot() && session_->slots() != nullptr) {
    showMenu();
  } else {
    wakeUp();
  }
}

void SleepScreen::showMenu() {
  phase_ = Phase::Menu;
  ui_->show(SleepMenuDocument, true);
}

void SleepScreen::follow(const SaveStep& step) {
  if (step.result) {
    ui_->setSetting("save.last_result", std::string{saveResultName(*step.result)});
  }
  if (step.dialog) {
    screens::publish(*ui_, dialogSettings(*step.dialog), "dialog.revision");
    ui_->show(DialogDocument, true);
    dialogShown_ = true;
  } else if (step.result == SaveResult::Saved) {
    wakeUp();
  } else {
    showMenu();
  }
}

void SleepScreen::wakeUp() {
  phase_ = Phase::FadingIn;
  fadeSeconds_ = 0.0;
}

} // namespace dwe::app
