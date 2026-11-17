#include "coop_screen.hpp"

#include <dwe/game/versus/network.hpp>
#include <dwe/net/rtc_peer_link.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/versus_match.hpp>

#include <chrono>
#include <utility>

namespace dwe::app {
namespace {
CoopConnectionDesc connectionDesc(const assets::VirtualFileSystem& files,
                                  const Options& options) {
  game::coop::Identity identity{.gameVersion = DWE_GAME_VERSION, .contentHash = {}};
  std::string configurationError;
  if (auto rules = ruleset::loadRuleset(files, options.ruleset); rules) {
    if (auto versus = session::versusIdentity(DWE_GAME_VERSION, *rules, files); versus) {
      identity.contentHash = std::move(versus->contentHash);
    } else {
      configurationError = "content_identity_unavailable";
    }
  } else {
    configurationError = "ruleset_unavailable";
  }
  net::RtcSettings settings;
  if (auto config = game::versus::loadNetworkConfig(files); config) {
    settings.signalServer = std::move(config->signalServer);
    settings.iceServers = std::move(config->iceServers);
  }
  if (!options.versusServer.empty()) settings.signalServer = options.versusServer;
  return {.identity = std::move(identity),
          .configurationError = std::move(configurationError),
          .host = [settings] { return net::RtcPeerLink::host(settings, net::openWebSocket); },
          .join = [settings](std::string_view code) {
            return net::RtcPeerLink::join(settings, code, net::openWebSocket);
          }};
}
} // namespace

CoopScreen::CoopScreen(const assets::VirtualFileSystem& files, const Options& options, ui::Ui& ui)
    : ui_{&ui}, connection_{connectionDesc(files, options)} {}

bool CoopScreen::handle(const ui::UiCommand& command) {
  if (command.name == CoopOpenCommand) {
    connection_.open();
    active_ = true;
    publish();
    return true;
  }
  if (!active_) return false;
  if (command.name == "coop_host") {
    isHost_ = true;
    (void)connection_.host();
    publish();
    return true;
  }
  if (command.name == "coop_join") {
    isHost_ = false;
    connection_.beginJoin();
    publish();
    return true;
  }
  if (command.name == "coop_close") {
    // Keep a ready peer alive while the player leaves the lobby to load their save. An unfinished
    // attempt is still cancelled when the page closes.
    if (!connection_.connected()) connection_.close();
    active_ = false;
    publish();
    return true;
  }
  return false;
}

void CoopScreen::type(std::string_view text, bool enter) {
  if (!takesText()) return;
  connection_.type(text, enter);
  publish();
}

void CoopScreen::tick(double nowSeconds) {
  if (connection_.link() == nullptr || connection_.view() == CoopConnection::View::Closed) return;
  constexpr double MillisPerSecond = 1000.0;
  connection_.tick(net::Millis{static_cast<std::int64_t>(nowSeconds * MillisPerSecond)});
  if (active_) publish();
}

void CoopScreen::publish() {
  ui_->setSetting("coop.view", std::string{coopConnectionViewName(connection_.view())});
  ui_->setSetting("coop.code", connection_.code());
  ui_->setSetting("coop.typed", connection_.typed());
  ui_->setSetting("coop.error", std::string{connection_.error()});
}

} // namespace dwe::app
