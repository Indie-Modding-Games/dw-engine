#include "versus_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/game/versus/network.hpp>
#include <dwe/net/rtc_peer_link.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/slot_names.hpp>
#include <dwe/session/versus_match.hpp>

#include <random>
#include <utility>
#include <vector>

#include "session_start.hpp"

#ifndef DWE_GAME_VERSION
#define DWE_GAME_VERSION "0.0.0"
#endif

namespace dwe::app {

namespace {

constexpr std::string_view Revision = "versus.revision";

// The saves of the saves folder with their registered partners, in the slots' order; a save that
// cannot be read is left out.
std::vector<VersusSave> savesIn(const assets::VirtualFileSystem& files, const Options& options,
                                bool keepsSaves) {
  std::vector<VersusSave> saves;
  const std::unique_ptr<game::save::DiskSaveStorage> storage = saveStorageFor(options, keepsSaves);
  const auto settings = game::save::loadSaveSettings(files);
  if (!storage || !settings) {
    return saves;
  }
  game::save::SaveSlots slots{*storage, *settings};
  slots.setSummaryReader(session::summaryFromSections);
  for (const game::save::SlotInfo& info : slots.list()) {
    if (info.state != game::save::SlotInfo::State::Saved) {
      continue;
    }
    auto file = slots.load(info.slot);
    if (!file) {
      core::logWarning("versus: cannot read a save: {}", file.error().message);
      continue;
    }
    auto partners = session::registeredPartnersIn(*file);
    if (!partners) {
      core::logWarning("versus: cannot read a save's partners: {}", partners.error().message);
      continue;
    }
    saves.push_back({.player = file->header.summary.tamer,
                     .nickname = file->header.summary.nickname,
                     .partners = std::move(*partners)});
  }
  return saves;
}

} // namespace

VersusScreen::VersusScreen(const assets::VirtualFileSystem& files, const Options& options,
                           bool keepsSaves, ui::Ui& ui)
    : files_{&files}, options_{&options}, keepsSaves_{keepsSaves}, ui_{&ui} {}

VersusScreen::~VersusScreen() = default;

bool VersusScreen::open() {
  if (!flow_) {
    auto rules = ruleset::loadRuleset(*files_, options_->ruleset);
    if (!rules) {
      core::logError("versus: {}", rules.error().message);
      return false;
    }
    auto battles = std::make_unique<session::BattleAdapter>(
        session::BattleAdapterDesc{files_, {}, {}, nullptr});
    if (auto read = battles->readRules(*rules); !read) {
      core::logError("versus: {}", read.error().message);
      return false;
    }
    auto identity = session::versusIdentity(DWE_GAME_VERSION, *rules, *files_);
    if (!identity) {
      core::logError("versus: {}", identity.error().message);
      return false;
    }
    battles_ = std::move(battles);
    // The servers: the game pack's, and the command line's signaling server over it. Without a
    // signaling server the links fail at once and the screen says that none is set up.
    net::RtcSettings settings;
    if (auto network = game::versus::loadNetworkConfig(*files_); network) {
      settings.signalServer = std::move(network->signalServer);
      settings.iceServers = std::move(network->iceServers);
    } else {
      core::logWarning("versus: {}", network.error().message);
    }
    if (!options_->versusServer.empty()) {
      settings.signalServer = options_->versusServer;
    }
    const assets::VirtualFileSystem* files = files_;
    const Options* options = options_;
    const bool keepsSaves = keepsSaves_;
    flow_.emplace(VersusFlowDesc{
        .battles = battles_.get(),
        .identity = std::move(*identity),
        .saves = [files, options, keepsSaves] { return savesIn(*files, *options, keepsSaves); },
        .host = [settings] { return net::RtcPeerLink::host(settings, net::openWebSocket); },
        .join =
            [settings](std::string_view code) {
              return net::RtcPeerLink::join(settings, code, net::openWebSocket);
            },
        .seed =
            [] {
              std::random_device device;
              return (static_cast<std::uint64_t>(device()) << 32U) | device();
            }});
  }
  flow_->open();
  active_ = true;
  closed_ = false;
  screens::publish(*ui_, flow_->settings(), Revision);
  ui_->show(VersusDocument, true);
  return true;
}

void VersusScreen::close() {
  active_ = false;
  closed_ = true;
  ui_->hide(VersusDocument);
}

bool VersusScreen::handle(const ui::UiCommand& command) {
  if (!active_) {
    return false;
  }
  if (command.name != VersusActionCommand) {
    return command.name.starts_with("versus_");
  }
  if (const auto* action = std::get_if<std::string>(&command.value)) {
    flow_->act(*action);
  }
  return true;
}

void VersusScreen::type(std::string_view text, bool enter) {
  if (active_) {
    flow_->type(text, enter);
  }
}

void VersusScreen::tick(double nowSeconds) {
  if (!active_) {
    return;
  }
  constexpr double MillisPerSecond = 1000.0;
  flow_->tick(net::Millis{static_cast<std::int64_t>(nowSeconds * MillisPerSecond)});
  if (flow_->closed()) {
    close();
    return;
  }
  screens::publish(*ui_, flow_->settings(), Revision);
}

bool VersusScreen::takeClosed() {
  return std::exchange(closed_, false);
}

} // namespace dwe::app
