#include "award_screen.hpp"

#include <dwe/core/log.hpp>

#include <string>

namespace dwe::app {

namespace {

constexpr std::string_view NoticeAnswer = "award_notice_ok";
constexpr std::string_view LinesSetting = "award_notice.lines";
constexpr std::string_view ColorSetting = "award_notice.color";
// tamer.c: tamerTickAwardSomething draws "Congratulations!" and the two lines of the medal in
// TEXT_COLORS 7, or one line for the tamer level: up in 7, down in 3 (tamer_messages).
constexpr std::string_view MedalLines =
    "main.tamer_messages.0005,main.tamer_messages.0006,main.tamer_messages.0007";
constexpr std::string_view LevelUpLine = "main.tamer_messages.0003";
constexpr std::string_view LevelDownLine = "main.tamer_messages.0004";
constexpr double AwardColor = 7.0;
constexpr double LevelDownColor = 3.0;

} // namespace

AwardScreen::AwardScreen(session::MedalsAdapter* medals, session::GameSession& session, ui::Ui& ui,
                         game::story::SoundPort* sound)
    : medals_{medals}, session_{&session}, ui_{&ui}, sound_{sound} {}

void AwardScreen::tick() {
  if (active_ || !session_->playing()) {
    return;
  }
  if (medals_ != nullptr) {
    medals_->check(*session_);
  }
  // tamer.c: checkPendingAwards; the medal's notice goes first and the tamer level's is dropped
  // with it (tamerTickAwardSomething clears both).
  const bool medal = medals_ != nullptr && medals_->awardPending();
  const auto& level = session_->tamerLevelAward();
  if (medal || level) {
    open(medal, level.value_or(session::TamerLevelChange::Up));
  }
}

void AwardScreen::open(bool medal, session::TamerLevelChange change) {
  active_ = true;
  core::logDebug("[port:medals] tamerTickAwardSomething (tamer.c:1751): {} notice",
                 medal ? "medal" : "tamer level");
  const bool down = !medal && change == session::TamerLevelChange::Down;
  ui_->setSetting(ColorSetting, down ? LevelDownColor : AwardColor);
  ui_->setSetting(LinesSetting,
                  std::string{medal ? MedalLines : (down ? LevelDownLine : LevelUpLine)});
  ui_->show(AwardNoticeDocument, true);
  if (sound_ != nullptr && medals_ != nullptr && medals_->rules()) {
    const session::AwardSound& sound = medals_->rules()->awardSound;
    sound_->playSound(sound.slot, sound.sound);
  }
}

bool AwardScreen::handle(const ui::UiCommand& command) {
  if (command.name != NoticeAnswer || !active_) {
    return false;
  }
  active_ = false;
  if (medals_ != nullptr) {
    medals_->clearAward();
  }
  session_->clearTamerLevelAward();
  ui_->hide(AwardNoticeDocument);
  return true;
}

} // namespace dwe::app
