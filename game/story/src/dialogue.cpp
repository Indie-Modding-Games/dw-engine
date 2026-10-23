#include <dwe/core/log.hpp>
#include <dwe/game/story/dialogue.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "dialogue_messages.hpp"

namespace dwe::game::story {

namespace msg = dialogue_messages;

namespace {

// The point a box grows from, for the log.
std::string pointText(const std::optional<WorldPoint>& point) {
  return point ? std::format("({}, {}, {})", point->x, point->y, point->z) : std::string{"none"};
}

std::string_view kindText(BoxKind kind) {
  return kind == BoxKind::Speech ? "speech" : "sized";
}

} // namespace

Speaker speakerOf(std::int64_t owner) {
  if (owner == NoSpeaker) {
    return {};
  }
  if (owner == PlayerSpeaker) {
    return {.kind = SpeakerKind::Player};
  }
  if (owner == PartnerSpeaker) {
    return {.kind = SpeakerKind::Partner};
  }
  if (owner >= FirstSpecialSpeaker) {
    return {.kind = SpeakerKind::Special, .number = owner - FirstSpecialSpeaker};
  }
  return {.kind = SpeakerKind::Character, .number = owner};
}

Dialogue::Dialogue(StoryStatePort& state, DialogueScenePort& scene, DialogueRules rules)
    : state_{&state}, scene_{&scene}, rules_{rules} {}

void Dialogue::setOwner(std::int64_t owner) {
  core::logDebug("{}", msg::formatMessage(msg::PortSetOwner, owner, owner_, boxOpen()));
  namesHidden_ = false;
  // script_interp.c:775: a box that is not open yet is always set up again, even for the same
  // speaker.
  if (!boxOpen()) {
    owner_ = owner - 1;
  }
  if (owner_ == owner) {
    core::logDebug("{}", msg::formatMessage(msg::PortOwnerKept, owner));
    return;
  }
  owner_ = owner;
  scene_->freeze(owner);
  openSpeechBox(owner);
}

Wait Dialogue::showText(std::string text) {
  const std::int64_t speaker = namesHidden_ ? NoSpeaker : owner_;
  core::logDebug("{}", msg::formatMessage(msg::PortShowText, text, speaker));
  if (!ensureBox(text)) {
    core::logDebug("{}", msg::formatMessage(msg::PortSkipPage, text));
    result_ = ResumeValue{};
    return SourceWait{.source = this};
  }
  startPage(std::move(text), speaker, Waiting::Page);
  return SourceWait{.source = this};
}

core::Result<Wait> Dialogue::showSelection(std::string text, std::int64_t count) {
  if (count < 1) {
    return core::fail(msg::formatMessage(msg::NoOptions, count));
  }
  core::logDebug("{}", msg::formatMessage(msg::PortShowSelection, text, count, owner_));
  // script_interp.c:706: a selection names its speaker even after set_textbox_size.
  namesHidden_ = false;
  optionCount_ = count;
  cursor_ = 0;
  if (!ensureBox(text)) {
    core::logDebug("{}", msg::formatMessage(msg::PortSkipPage, text));
    result_ = ResumeValue{count};
    return SourceWait{.source = this};
  }
  startPage(std::move(text), owner_, Waiting::Selection);
  return SourceWait{.source = this};
}

core::Result<void> Dialogue::setMode(std::int64_t mode, std::int64_t frames) {
  core::logDebug("{}", msg::formatMessage(msg::PortSetMode, mode, frames));
  if (mode < std::to_underlying(TextboxMode::Confirm) ||
      mode > std::to_underlying(TextboxMode::Timed)) {
    return core::fail(msg::formatMessage(msg::NoSuchMode, mode));
  }
  mode_ = static_cast<TextboxMode>(mode);
  // The frames count only for the timed mode, from this order on (script_interp.c:1967).
  if (mode_ == TextboxMode::Timed) {
    timedFrames_ = std::max<std::int64_t>(frames, 0);
  }
  publish();
  return {};
}

core::Result<void> Dialogue::setSizedBox(std::int64_t origin, std::int64_t columns,
                                         std::int64_t rows) {
  core::logDebug("{}", msg::formatMessage(msg::PortSizedBox, origin, columns, rows));
  if (columns < 1 || rows < 1) {
    return core::fail(msg::formatMessage(msg::NoRoom, columns, rows));
  }
  scene_->freeze(NoSpeaker);
  namesHidden_ = true;
  Box box;
  box.kind = BoxKind::Sized;
  box.columns = columns;
  box.rows = rows;
  if (origin != NoSpeaker) {
    if (nextOrigin_) {
      box.originPoint = std::exchange(nextOrigin_, std::nullopt);
    } else if (scene_->onMap(origin)) {
      box.originCharacter = origin;
    }
  }
  openBox(box);
  return {};
}

void Dialogue::setOrigin(WorldPoint point) {
  core::logDebug("{}", msg::formatMessage(msg::PortSetOrigin, point.x, point.y, point.z));
  nextOrigin_ = point;
}

FrameWait Dialogue::closeBox(std::int64_t box) {
  core::logDebug("{}", msg::formatMessage(msg::PortCloseBox, box, box_.has_value()));
  if (box != 0) {
    // The other boxes belong to the systems' menus, which close their own.
    core::logDebug("{}", msg::formatMessage(msg::PortNoStoryBox, box));
  } else {
    box_.reset();
    closeAfterPage_ = false;
    publish();
  }
  // script_interp.c:840: the order yields until the next frame.
  return FrameWait{.frames = 1, .result = {}};
}

void Dialogue::press(DialogueKeys pressed, bool confirmHeld) {
  latched_.confirm = latched_.confirm || pressed.confirm;
  latched_.cancel = latched_.cancel || pressed.cancel;
  latched_.up = latched_.up || pressed.up;
  latched_.down = latched_.down || pressed.down;
  confirmHeld_ = confirmHeld;
}

std::optional<ResumeValue> Dialogue::poll() {
  return std::exchange(result_, std::nullopt);
}

void Dialogue::scriptStarted() {
  core::logDebug("{}", msg::PortScriptStarted);
  owner_ = PlayerSpeaker;
}

void Dialogue::frameBegan() {
  const DialogueKeys keys = std::exchange(latched_, DialogueKeys{});
  if (std::exchange(closeAfterPage_, false)) {
    core::logDebug("{}", msg::PortSizedClosed);
    box_.reset();
    publish();
  }
  // script_textbox.c: tickTextboxHandling counts the timed mode down before the boxes tick.
  if (timedFrames_ > 0) {
    --timedFrames_;
  }
  if (!box_) {
    return;
  }
  if (boxOpen()) {
    tickBox(keys);
    return;
  }
  ++box_->openingFrame;
  if (boxOpen()) {
    core::logDebug("{}", msg::formatMessage(msg::PortBoxOpen, rules_.openFrames));
  }
  publish();
}

void Dialogue::scriptStopped() {
  closeAll();
}

void Dialogue::openBox(Box box) {
  const bool grows = box.originCharacter.has_value() || box.originPoint.has_value();
  // ui.c: createAnimatedUIBox plays the opening sound; a box with nothing to grow from appears at
  // once and silently (createStaticUIBox).
  box.openingFrame = grows ? 0 : rules_.openFrames;
  core::logDebug("{}", msg::formatMessage(msg::PortOpenBox, kindText(box.kind), box.columns,
                                          box.rows, box.originCharacter.value_or(-1),
                                          pointText(box.originPoint), box.openingFrame));
  if (grows) {
    play(rules_.openSound);
  }
  box_ = box;
  skipPages_ = false;
  closeAfterPage_ = false;
  // script_textbox.c: registerTextbox clears the box's text.
  shownText_.clear();
  shownSpeaker_ = {};
  pageOnShow_ = false;
  publish();
}

void Dialogue::openSpeechBox(std::int64_t owner) {
  Box box;
  box.kind = BoxKind::Speech;
  box.columns = rules_.speechColumns;
  box.rows = rules_.speechRows;
  // script_textbox.c: setupBoxOrigin: a set point first, used once, else the speaker on the map.
  if (owner != NoSpeaker) {
    if (nextOrigin_) {
      box.originPoint = std::exchange(nextOrigin_, std::nullopt);
    } else if (scene_->onMap(owner)) {
      box.originCharacter = owner;
    }
  }
  openBox(box);
}

void Dialogue::closeAll() {
  core::logDebug("{}", msg::formatMessage(msg::PortCloseAll, box_.has_value()));
  box_.reset();
  waiting_ = Waiting::None;
  result_.reset();
  pageOnShow_ = false;
  closeAfterPage_ = false;
  skipPages_ = false;
  shownText_.clear();
  shownSpeaker_ = {};
  publish();
}

bool Dialogue::ensureBox(std::string_view text) {
  if (box_) {
    return true;
  }
  if (skipPages_) {
    return false;
  }
  core::logDebug("{}", msg::formatMessage(msg::PortNoBox, text, owner_));
  openSpeechBox(owner_);
  return true;
}

void Dialogue::startPage(std::string text, std::int64_t speaker, Waiting waiting) {
  // Another page follows the sized box's confirmed one, in the same box.
  closeAfterPage_ = false;
  pendingText_ = std::move(text);
  pendingSpeaker_ = speakerOf(speaker);
  pageOnShow_ = false;
  waiting_ = waiting;
  result_.reset();
  publish();
}

bool Dialogue::boxOpen() const {
  return box_.has_value() && box_->openingFrame >= rules_.openFrames;
}

void Dialogue::tickBox(const DialogueKeys& keys) {
  if (waiting_ == Waiting::None) {
    return;
  }
  // script_textbox.c: flipIdleTextboxPage turns to the new page; that frame reads no input.
  if (!pageOnShow_) {
    pageOnShow_ = true;
    shownText_ = pendingText_;
    shownSpeaker_ = pendingSpeaker_;
    core::logDebug("{}", msg::formatMessage(msg::PortPageShown, shownText_));
    publish();
    return;
  }
  if (box_->freshGuard) {
    if (confirmHeld_) {
      return;
    }
    box_->freshGuard = false;
  }
  if (waiting_ == Waiting::Selection) {
    tickSelection(keys);
  } else if (box_->kind == BoxKind::Sized) {
    tickSizedPage(keys);
  } else {
    tickPage(keys);
  }
}

void Dialogue::tickPage(const DialogueKeys& keys) {
  switch (mode_) {
  case TextboxMode::Confirm:
    if (!keys.confirm) {
      return;
    }
    play(rules_.advanceSound);
    break;
  case TextboxMode::Timed:
    if (timedFrames_ != 0) {
      return;
    }
    break;
  case TextboxMode::Auto:
    break;
  }
  core::logDebug("{}", msg::formatMessage(msg::PortPageDone, std::to_underlying(mode_)));
  finish(ResumeValue{});
}

void Dialogue::tickSizedPage(const DialogueKeys& keys) {
  // script_textbox.c: tickCustomSizedTextbox reads the player whatever the mode.
  if (keys.confirm) {
    core::logDebug("{}", msg::formatMessage(msg::PortSizedPageDone, "confirmed"));
    play(rules_.advanceSound);
    finish(ResumeValue{});
    closeAfterPage_ = true;
    return;
  }
  if (keys.cancel) {
    // The original shows the pages that follow at once and closes the box.
    core::logDebug("{}", msg::formatMessage(msg::PortSizedPageDone, "skipped"));
    skipPages_ = true;
    box_.reset();
    play(rules_.cancelSound);
    finish(ResumeValue{});
  }
}

void Dialogue::tickSelection(const DialogueKeys& keys) {
  if (keys.confirm) {
    core::logDebug("{}", msg::formatMessage(msg::PortSelectionDone, cursor_, optionCount_));
    play(rules_.advanceSound);
    finish(ResumeValue{cursor_});
    return;
  }
  if (keys.cancel) {
    const auto locked = state_->trigger(rules_.noCancelTrigger);
    if (!locked) {
      core::logError("{}", msg::formatMessage(msg::PortTriggerFailed, rules_.noCancelTrigger,
                                              locked.error().message));
    }
    if (locked.value_or(false)) {
      core::logDebug("{}", msg::formatMessage(msg::PortSelectionRefused, rules_.noCancelTrigger));
      play(rules_.refusedSound);
      return;
    }
    // The script goes past its options, as Lua's last branch (count) does.
    core::logDebug("{}", msg::formatMessage(msg::PortSelectionDone, optionCount_, optionCount_));
    play(rules_.cancelSound);
    finish(ResumeValue{optionCount_});
    return;
  }
  if (keys.up) {
    cursor_ = cursor_ == 0 ? optionCount_ - 1 : cursor_ - 1;
  } else if (keys.down) {
    cursor_ = cursor_ + 1 == optionCount_ ? 0 : cursor_ + 1;
  } else {
    return;
  }
  core::logDebug("{}", msg::formatMessage(msg::PortCursor, cursor_, optionCount_));
  play(rules_.cursorSound);
  publish();
}

void Dialogue::finish(ResumeValue value) {
  result_ = value;
  waiting_ = Waiting::None;
  publish();
}

void Dialogue::play(const DialogueSound& sound) {
  if (sound_ != nullptr) {
    sound_->playSound(sound.slot, sound.sound);
  }
}

void Dialogue::publish() {
  DialogueView view;
  view.revision = view_.revision;
  if (box_) {
    view.open = true;
    view.kind = box_->kind;
    view.columns = box_->columns;
    view.rows = box_->rows;
    view.originCharacter = box_->originCharacter;
    view.originPoint = box_->originPoint;
    view.openingFrame = std::min(box_->openingFrame, rules_.openFrames);
    view.text = shownText_;
    view.speaker = shownSpeaker_;
    view.waitingForConfirm = pageOnShow_ && waiting_ == Waiting::Page &&
                             box_->kind == BoxKind::Speech && mode_ == TextboxMode::Confirm;
    if (pageOnShow_ && waiting_ == Waiting::Selection) {
      view.cursor = cursor_;
    }
  }
  if (view == view_) {
    return;
  }
  ++view.revision;
  view_ = std::move(view);
}

} // namespace dwe::game::story
