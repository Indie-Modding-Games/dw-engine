#include "award_screen.hpp"

#include <dwe/core/log.hpp>

#include <string>
#include <utility>

namespace dwe::app {

namespace {

constexpr std::string_view NoticeAnswer = "award_notice_ok";
constexpr std::string_view LinesSetting = "award_notice.lines";
constexpr std::string_view ColorSetting = "award_notice.color";
constexpr std::string_view NameSetting = "award_notice.name";
// tamer.c: tamerTickAwardSomething draws "Congratulations!" and the two lines of the medal in
// TEXT_COLORS 7, or one line for the tamer level: up in 7, down in 3 (tamer_messages).
constexpr std::string_view MedalLines =
    "main.tamer_messages.0005,main.tamer_messages.0006,main.tamer_messages.0007";
constexpr std::string_view LevelUpLine = "main.tamer_messages.0003";
constexpr std::string_view LevelDownLine = "main.tamer_messages.0004";
constexpr double AwardColor = 7.0;
constexpr double LevelDownColor = 3.0;
// partner_impl.c: tickSicknessMechanics draws the partner's name in TEXT_COLORS 10 and the line
// after it in 1.
constexpr std::string_view SickLine = "main.partner_impl_is_sick.0000";
constexpr double SickColor = 1.0;

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
  // with it (tamerTickAwardSomething clears both). Either clears the text area the sickness line
  // was drawn in, so a sickness due with them shows no line of its own.
  const bool medal = medals_ != nullptr && medals_->awardPending();
  const auto& level = session_->tamerLevelAward();
  if (medal || level) {
    sickPartner_.reset();
    open(medal, level.value_or(session::TamerLevelChange::Up));
  } else if (sickPartner_) {
    openSickness();
  }
}

void AwardScreen::noteSickness(std::string partnerName) {
  sickPartner_ = std::move(partnerName);
}

void AwardScreen::open(bool medal, session::TamerLevelChange change) {
  core::logDebug("[port:medals] tamerTickAwardSomething (tamer.c:1751): {} notice",
                 medal ? "medal" : "tamer level");
  const bool down = !medal && change == session::TamerLevelChange::Down;
  show(medal ? MedalLines : (down ? LevelDownLine : LevelUpLine),
       down ? LevelDownColor : AwardColor, "");
}

void AwardScreen::openSickness() {
  core::logDebug("[port:partner] tickSicknessMechanics (partner_impl.c:1898): sickness notice "
                 "for {}",
                 *sickPartner_);
  show(SickLine, SickColor, *sickPartner_);
  sickPartner_.reset();
}

void AwardScreen::show(std::string_view lines, double color, std::string_view name) {
  active_ = true;
  ui_->setSetting(ColorSetting, color);
  ui_->setSetting(NameSetting, std::string{name});
  ui_->setSetting(LinesSetting, std::string{lines});
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
