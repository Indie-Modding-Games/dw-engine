#include "sleep_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/partner/care_rules.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "common.hpp"
#include "messages.hpp"
#include "title_screen.hpp"

namespace dwe::app {
namespace {

// fade.c: fadeToBlack and fadeFromBlack step the shade by shadeMax / (frames + 1) per frame.
std::int32_t shadeStep(const game::field::TransitionRules& rules, std::int32_t frames) {
  return rules.shadeMax / (frames + 1);
}

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 7> Names{"awake",     "fading out", "menu",    "fading in",
                                             "fade tail", "walking up", "settling"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

} // namespace

SleepScreen::SleepScreen(session::GameSession& session, ui::Ui& ui,
                         const game::field::TransitionRules& transition)
    : session_(&session), ui_(&ui), transition_(transition),
      saves_(session.slots(), [&session](game::save::SlotId slot) { return session.save(slot); }),
      steps_(session.partnerRules().care.sleep.scene) {
  ui.setSetting("save.last_result", std::string{});
}

bool SleepScreen::active() const {
  return phase_ != Phase::Awake && phase_ != Phase::FadeTail;
}

bool SleepScreen::fieldRuns() const {
  return phase_ == Phase::WalkingUp || phase_ == Phase::Settling || phase_ == Phase::FadingOut ||
         phase_ == Phase::FadingIn;
}

std::int32_t SleepScreen::fadeFrame() const {
  // As the field's fades: the frame counter starts at 1, and a little slack lets ticks adding up to
  // a frame's length land on that frame despite rounding.
  constexpr double Slack = 1e-3;
  return 1 + static_cast<std::int32_t>(std::floor(
                 (fadeSeconds_ / static_cast<double>(transition_.frameSeconds)) + Slack));
}

float SleepScreen::fadeLevel() const {
  const game::partner::SleepFade& fade = session_->partnerRules().care.sleep.fade;
  switch (phase_) {
  case Phase::FadingOut:
  case Phase::Menu: {
    // fade.c: renderFadeOut adds its step to the shade the last fade in left, and goes on drawing
    // until the fade in replaces it.
    const std::int32_t start = game::field::fadeInShade(transition_, transition_.fadeInFrames);
    const std::int32_t shade = std::min(
        transition_.shadeMax, start + (shadeStep(transition_, fade.outFrames) * fadeFrame()));
    return game::field::shadeDarkness(transition_, shade);
  }
  case Phase::FadingIn:
  case Phase::FadeTail: {
    game::field::TransitionRules in = transition_;
    in.shadeStep = shadeStep(transition_, fade.inFrames);
    return game::field::shadeDarkness(in, game::field::fadeInShade(in, fadeFrame()));
  }
  case Phase::Awake:
  case Phase::WalkingUp:
  case Phase::Settling:
    break;
  }
  return 0.0F;
}

void SleepScreen::tick(double seconds) {
  // The pause menu does not open over a sleep.
  if (active() && ui_->isVisible(MenuDocument)) {
    ui_->hide(MenuDocument);
  }
  if (phase_ == Phase::Awake || phase_ == Phase::WalkingUp || phase_ == Phase::Settling) {
    return;
  }
  fadeSeconds_ += seconds;
  const game::partner::SleepFade& fade = session_->partnerRules().care.sleep.fade;
  const std::int32_t frame = fadeFrame();
  if (phase_ == Phase::FadingOut && frame >= fade.outFrames) {
    // partner.c: partnerTickSleep sleeps once FADE_OUT_CURRENT passes 39.
    fallAsleep();
  } else if (phase_ == Phase::FadingIn && frame >= fade.controlFrame) {
    // partner.c: partnerTickSleep gives control back once FADE_IN_CURRENT passes 34, and only then
    // runs the wake-up evolution and the game time.
    setPhase(Phase::FadeTail);
    releasePending_ = true;
    if (std::exchange(asleep_, false)) {
      core::logDebug("[port:save] partnerTickSleep (partner.c:354): awake at frame {}", frame);
      session_->wakeUp();
    }
  } else if (phase_ == Phase::FadeTail && frame > fade.inFrames) {
    setPhase(Phase::Awake);
  }
}

bool SleepScreen::handle(const ui::UiCommand& command) {
  const std::string& name = command.name;
  if (name == "sleep") {
    if (canSleep(fieldIdle_)) {
      ui_->hide(MenuDocument);
      setPhase(Phase::WalkingUp);
      holdPending_ = true;
    }
    return true;
  }
  if (name == "open_menu") {
    // fade.c: FADE_PROTECTION keeps the menu shut until the fade in ends.
    return phase_ != Phase::Awake;
  }
  if (phase_ != Phase::Menu) {
    return false;
  }
  if (name == "sleep_only") {
    ui_->hide(SleepMenuDocument);
    startFadeIn();
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

void SleepScreen::tickField(FieldScene& scene, float seconds,
                            const std::optional<session::SoundCue>& arrived) {
  if (std::exchange(wakePending_, false)) {
    apply(scene, steps_.wakeUp());
  }
  if (std::exchange(releasePending_, false)) {
    // partner.c:356-358: the partner's state 1 and the player's state 0: the two are the field's
    // again.
    scene.setPlayerClip(std::nullopt);
    scene.playPartnerClip(std::nullopt);
    scene.field().holdForCare(false);
  }
  switch (phase_) {
  case Phase::WalkingUp: {
    game::field::Field& field = scene.field();
    if (std::exchange(holdPending_, false)) {
      // partner.c:281-289: sub state 0 stops the game time, holds the player (its state 6) and,
      // unless the partner is beside the player already, walks it straight up to the player (clip
      // 2, looking at the player every frame) until getPartnerTamerCloseness gives 2.
      field.holdForCare(true, game::field::CareClock::Stops);
      if (!field.partnerBeside()) {
        const game::field::PartnerRules& rules = field.rules().partner;
        field.walkPartnerToRange(rules.walkDistance * rules.mover.radius);
      }
    }
    scene.tick({}, seconds);
    scene.finishTick();
    if (field.partnerWalking()) {
      return;
    }
    // partner.c:292-300: beside the player, the two look at each other, the sound plays and their
    // clips start.
    field.faceEachOther();
    core::logDebug("[port:sleep] partnerTickSleep (partner.c:296): the partner is beside the "
                   "player");
    if (arrived && session_->sound() != nullptr) {
      session_->sound()->playSound(arrived->slot, arrived->sound);
    }
    apply(scene, steps_.start());
    setPhase(Phase::Settling);
    return;
  }
  case Phase::Settling: {
    scene.tick({}, seconds);
    scene.finishTick();
    const SleepSteps::Order order =
        steps_.tick({.playerPlayedThrough = scene.playerClipPlayedThrough(),
                     .partnerFramesLeft = scene.partnerClipFramesLeft()});
    apply(scene, order);
    if (order.fadeOut) {
      setPhase(Phase::FadingOut);
      fadeSeconds_ = 0.0;
    }
    return;
  }
  case Phase::FadingOut:
  case Phase::FadingIn:
    // The field goes on under the fade, the two holding their clips.
    scene.tick({}, seconds);
    scene.finishTick();
    return;
  case Phase::Awake:
  case Phase::Menu:
  case Phase::FadeTail:
    return;
  }
}

void SleepScreen::apply(FieldScene& scene, const SleepSteps::Order& order) {
  if (order.playerClip) {
    scene.setPlayerClip(order.playerClip);
  }
  if (order.partnerClip) {
    scene.playPartnerClip(order.partnerClip, order.partnerMoves);
  }
}

bool SleepScreen::canSleep(bool fieldIdle) const {
  return fieldIdle && phase_ == Phase::Awake && session_->partner().canSleep();
}

void SleepScreen::update(bool fieldIdle) {
  // game_menu.c: setSleepDisabled greys out the menu's Sleep unless the partner is sleepy
  // (partner_impl.c:1037-1039, and on waking at 1209); here it follows canSleep each frame.
  fieldIdle_ = fieldIdle;
  const bool enabled = canSleep(fieldIdle);
  if (canSleep_ != enabled) {
    canSleep_ = enabled;
    ui_->setSetting("pause_menu.sleep_enabled", enabled);
  }
}

void SleepScreen::fallAsleep() {
  if (auto slept = session_->fallAsleep({.hasPillow = false, .area = session_->areaResponse()});
      !slept) {
    core::logWarning(messages::SleepFailed, slept.error().message);
    startFadeIn();
    return;
  }
  asleep_ = true;
  core::logDebug("[port:save] partnerTickSleep (partner.c:325): asleep at frame {}, home slot {}",
                 fadeFrame(), session_->homeSlot().has_value());
  // Without a slot the playthrough cannot be saved, so there is nothing to ask.
  if (session_->homeSlot() && session_->slots() != nullptr) {
    showMenu();
  } else {
    startFadeIn();
  }
}

void SleepScreen::showMenu() {
  setPhase(Phase::Menu);
  ui_->show(SleepMenuDocument, true);
}

void SleepScreen::follow(const SaveStep& step) {
  core::logDebug("[port:save] tickMainMenu (main_menu.c): sleep menu save: result {}, dialog {}",
                 step.result ? saveResultName(*step.result) : "pending",
                 step.dialog ? step.dialog->text : std::string{"none"});
  if (step.result) {
    ui_->setSetting("save.last_result", std::string{saveResultName(*step.result)});
  }
  if (step.dialog) {
    screens::publish(*ui_, dialogSettings(*step.dialog), "dialog.revision");
    ui_->show(DialogDocument, true);
    dialogShown_ = true;
  } else if (step.result == SaveResult::Saved) {
    startFadeIn();
  } else {
    showMenu();
  }
}

void SleepScreen::setPhase(Phase phase) {
  core::logDebug("[port:save] partnerTickSleep (partner.c:320-362): sleep {} -> {}",
                 phaseName(static_cast<std::uint8_t>(phase_)),
                 phaseName(static_cast<std::uint8_t>(phase)));
  phase_ = phase;
}

void SleepScreen::startFadeIn() {
  setPhase(Phase::FadingIn);
  fadeSeconds_ = 0.0;
  wakePending_ = true;
}

} // namespace dwe::app
