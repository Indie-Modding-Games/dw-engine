#include "coop_hud.hpp"

#include <dwe/screens/screen_settings.hpp>

#include <variant>

namespace dwe::app {
namespace {

constexpr double Percent = 100.0;

void publishLabel(ui::Ui& ui, const CoopPeerLabel& peer, std::string_view name,
                  std::string_view key, std::string_view number) {
  ui.setSetting(name, peer.name);
  ui.setSetting(key, peer.key);
  ui.setSetting(number, static_cast<double>(peer.number));
}

// Counts a revision setting up, so the document redraws once for what changed with it.
void countUp(ui::Ui& ui, std::string_view revision) {
  const ui::UiValue current = ui.setting(revision);
  const double* count = std::get_if<double>(&current);
  ui.setSetting(revision, (count == nullptr ? 0.0 : *count) + 1.0);
}

} // namespace

CoopPeerLabel coopPeerLabel(bool localHosts, std::string_view remoteName) {
  if (!remoteName.empty()) {
    return {.name = std::string{remoteName}, .key = {}, .number = 0};
  }
  if (!localHosts) {
    return {.name = {}, .key = std::string{HostPlayerText}, .number = 0};
  }
  return {.name = {}, .key = std::string{GuestPlayerText}, .number = 1};
}

CoopTagView coopTagViewOf(const std::optional<CoopPeerLabel>& peer,
                          std::optional<screens::WindowFraction> head, bool fieldRunning) {
  if (!peer || !head || !fieldRunning || head->x < 0.0 || head->x > 1.0 || head->y < 0.0 ||
      head->y > 1.0) {
    // A hidden tag keeps no place or name, so it publishes nothing while it stays hidden.
    return {};
  }
  return {.visible = true, .x = head->x * Percent, .y = head->y * Percent, .peer = *peer};
}

CoopLinkLoss coopLinkLoss(bool localHosts, bool playing) {
  if (!playing) {
    return CoopLinkLoss::Quiet;
  }
  return localHosts ? CoopLinkLoss::PeerLeft : CoopLinkLoss::HostLeft;
}

void CoopHud::showTag(const CoopTagView& view) {
  if (published_ == view) {
    return;
  }
  screens::publish(
      *ui_,
      {{.name = std::string{CoopTagVisibleSetting}, .value = view.visible},
       {.name = std::string{CoopTagXSetting}, .value = view.x},
       {.name = std::string{CoopTagYSetting}, .value = view.y},
       {.name = std::string{CoopTagNameSetting}, .value = view.peer.name},
       {.name = std::string{CoopTagNameKeySetting}, .value = view.peer.key},
       {.name = std::string{CoopTagNumberSetting}, .value = static_cast<double>(view.peer.number)}},
      CoopTagRevisionSetting);
  published_ = view;
  showDocument();
}

void CoopHud::notice(std::string_view textKey, const CoopPeerLabel& peer) {
  publishLabel(*ui_, peer, CoopNoticeNameSetting, CoopNoticeNameKeySetting,
               CoopNoticeNumberSetting);
  ui_->setSetting(CoopNoticeTextSetting, std::string{textKey});
  ui_->setSetting(CoopNoticeShownSetting, true);
  countUp(*ui_, CoopNoticeRevisionSetting);
  noticeLeft_ = NoticeSeconds;
  noticePublished_ = true;
  showDocument();
}

void CoopHud::advance(double seconds) {
  if (noticeLeft_ > 0.0 && seconds > 0.0) {
    noticeLeft_ -= seconds;
  }
  if (noticeLeft_ <= 0.0 && noticePublished_) {
    noticeLeft_ = 0.0;
    noticePublished_ = false;
    ui_->setSetting(CoopNoticeShownSetting, false);
    countUp(*ui_, CoopNoticeRevisionSetting);
  }
  showDocument();
}

void CoopHud::showDocument() {
  // A UI change (another addon, a reload) may hide it; it comes back while it has something.
  const bool wanted = noticePublished_ || (published_ && published_->visible);
  if (wanted != ui_->isVisible(CoopHudDocument)) {
    if (wanted) {
      ui_->show(CoopHudDocument);
    } else {
      ui_->hide(CoopHudDocument);
    }
  }
}

} // namespace dwe::app
