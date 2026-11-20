#include "coop_hud.hpp"

#include <dwe/screens/screen_settings.hpp>

#include <algorithm>
#include <format>
#include <utility>
#include <variant>
#include <vector>

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

CoopPeerLabel coopPlayerLabel(std::uint8_t id, std::string_view name) {
  if (!name.empty()) {
    return {.name = std::string{name}, .key = {}, .number = 0};
  }
  if (id == 0) {
    return {.name = {}, .key = std::string{HostPlayerText}, .number = 0};
  }
  return {.name = {}, .key = std::string{GuestPlayerText}, .number = id};
}

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
                          std::optional<screens::WindowFraction> head, bool fieldRunning,
                          bool trade, bool training) {
  if (!peer || !head || !fieldRunning || head->x < 0.0 || head->x > 1.0 || head->y < 0.0 ||
      head->y > 1.0) {
    // A hidden tag keeps no place or name, so it publishes nothing while it stays hidden.
    return {};
  }
  return {.visible = true,
          .x = head->x * Percent,
          .y = head->y * Percent,
          .peer = *peer,
          .trade = trade,
          .training = training};
}

CoopLinkLoss coopLinkLoss(bool localHosts, bool playing) {
  if (!playing) {
    return CoopLinkLoss::Quiet;
  }
  return localHosts ? CoopLinkLoss::PeerLeft : CoopLinkLoss::HostLeft;
}

void CoopHud::showTags(std::span<const CoopTagView> views) {
  std::array<CoopTagView, MaxCoopTags> shown{};
  std::copy_n(views.begin(), std::min(views.size(), shown.size()), shown.begin());
  if (published_ == shown) {
    return;
  }
  screens::ScreenSettings settings;
  for (std::size_t index = 0; index < shown.size(); ++index) {
    const CoopTagView& view = shown[index];
    // The first tag keeps its names (coop_hud.tag.*); the others are coop_hud.tag.<n>.*.
    const std::string prefix =
        index == 0 ? std::string{"coop_hud.tag."} : std::format("coop_hud.tag.{}.", index + 1);
    settings.push_back({.name = prefix + "visible", .value = view.visible});
    settings.push_back({.name = prefix + "x", .value = view.x});
    settings.push_back({.name = prefix + "y", .value = view.y});
    settings.push_back({.name = prefix + "name", .value = view.peer.name});
    settings.push_back({.name = prefix + "name_key", .value = view.peer.key});
    settings.push_back({.name = prefix + "number", .value = static_cast<double>(view.peer.number)});
    settings.push_back({.name = prefix + "trade", .value = view.trade});
    settings.push_back({.name = prefix + "training", .value = view.training});
  }
  screens::publish(*ui_, settings, CoopTagRevisionSetting);
  published_ = shown;
  showDocument();
}

void CoopHud::notice(std::string_view textKey, const CoopPeerLabel& peer, double seconds) {
  publishLabel(*ui_, peer, CoopNoticeNameSetting, CoopNoticeNameKeySetting,
               CoopNoticeNumberSetting);
  ui_->setSetting(CoopNoticeTextSetting, std::string{textKey});
  ui_->setSetting(CoopNoticeShownSetting, true);
  countUp(*ui_, CoopNoticeRevisionSetting);
  noticeLeft_ = seconds;
  noticePublished_ = true;
  showDocument();
}

void CoopHud::noticeNext(std::string_view textKey, const CoopPeerLabel& peer, double seconds) {
  next_ = Pending{.textKey = std::string{textKey}, .peer = peer, .seconds = seconds};
}

void CoopHud::advance(double seconds) {
  if (noticeLeft_ > 0.0 && seconds > 0.0) {
    noticeLeft_ -= seconds;
  }
  if (noticeLeft_ <= 0.0 && next_) {
    const Pending next = *std::exchange(next_, std::nullopt);
    notice(next.textKey, next.peer, next.seconds);
    return;
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
  const bool wanted =
      noticePublished_ || (published_ && std::ranges::any_of(*published_, &CoopTagView::visible));
  if (wanted != ui_->isVisible(CoopHudDocument)) {
    if (wanted) {
      ui_->show(CoopHudDocument);
    } else {
      ui_->hide(CoopHudDocument);
    }
  }
}

bool CoopHostWait::due(std::chrono::milliseconds quietFor, std::chrono::milliseconds now) {
  if (quietFor < QuietAfter) {
    shownAt_.reset();
    return false;
  }
  if (shownAt_ && now - *shownAt_ < Every) {
    return false;
  }
  shownAt_ = now;
  return true;
}

} // namespace dwe::app
