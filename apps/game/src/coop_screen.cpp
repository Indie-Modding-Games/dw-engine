#include "coop_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/versus/network.hpp>
#include <dwe/net/rtc_peer_link.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/versus_match.hpp>

#include <chrono>
#include <utility>

#include "coop_guest_profile.hpp"

namespace dwe::app {
namespace {
CoopConnectionDesc connectionDesc(const assets::VirtualFileSystem& files, const Options& options,
                                  const std::shared_ptr<net::RtcSettings>& settings) {
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
  if (auto config = game::versus::loadNetworkConfig(files); config) {
    settings->iceServers = std::move(config->iceServers);
  }
  settings->signalServer = coopSignalServer(files, options);
  std::string compat = net::compatKey("coop", identity.gameVersion, identity.contentHash);
  // The settings are shared: the online profile's «relay only» changes them for the next room.
  return {.identity = std::move(identity),
          .configurationError = std::move(configurationError),
          .host =
              [settings](const std::optional<net::PublicHost>& listing) {
                return net::RtcPeerLink::host(*settings, net::openWebSocket, listing);
              },
          .join =
              [settings](std::string_view code) {
                return net::RtcPeerLink::join(*settings, code, net::openWebSocket);
              },
          .compat = std::move(compat),
          .signalServer = settings->signalServer,
          .open = net::openWebSocket};
}
} // namespace

std::string coopSignalServer(const assets::VirtualFileSystem& files, const Options& options) {
  if (!options.versusServer.empty()) {
    return options.versusServer;
  }
  if (auto config = game::versus::loadNetworkConfig(files); config) {
    return std::move(config->signalServer);
  }
  return {};
}

CoopScreen::CoopScreen(const assets::VirtualFileSystem& files, const Options& options, ui::Ui& ui)
    : ui_{&ui}, rtc_{std::make_shared<net::RtcSettings>()},
      connection_{connectionDesc(files, options, rtc_)} {}

void CoopScreen::setRelayOnly(bool relayOnly) {
  rtc_->relayOnly = relayOnly;
}

bool CoopScreen::handle(const ui::UiCommand& command) {
  if (command.name == CoopOpenCommand) {
    connection_.open();
    active_ = true;
    isHost_ = false;
    localReady_ = false;
    remoteReady_ = false;
    startRequested_ = false;
    guestProfileRequest_ = false;
    selectingGuestProfile_ = false;
    selectingHostStart_ = false;
    guestTransferId_.reset();
    hostTransferId_.reset();
    guestSaveText_.reset();
    hostSaveText_.reset();
    remotePartnerSpecies_.clear();
    remoteJoinState_.reset();
    remoteStartText_.reset();
    guestAssembler_.reset();
    hostAssembler_.reset();
    localError_.clear();
    publish();
    return true;
  }
  if (command.name == "coop_host") {
    // The title script sends coop_open when it reveals this view, but input can reach the
    // command queue before that lifecycle command is handled (for example after restoring the
    // title view). A host/join intent is sufficient to activate the lobby itself.
    if (!active_) {
      connection_.open();
      active_ = true;
    }
    isHost_ = true;
    localReady_ = true;
    remoteReady_ = false;
    localError_.clear();
    selectingHostStart_ = false;
    // The rooms screens send whether the room is public (listed); a plain coop_host is private.
    const bool* listed = std::get_if<bool>(&command.value);
    (void)connection_.host(listed != nullptr && *listed);
    publish();
    return true;
  }
  if (command.name == "coop_join") {
    if (!active_) {
      connection_.open();
      active_ = true;
    }
    isHost_ = false;
    localReady_ = false;
    remoteReady_ = false;
    localError_.clear();
    selectingGuestProfile_ = false;
    connection_.beginJoin();
    publish();
    return true;
  }
  if (command.name == "coop_connect") {
    if (!isHost_ && active_ &&
        (connection_.view() == CoopConnection::View::JoinEntry ||
         connection_.view() == CoopConnection::View::Error)) {
      (void)connection_.submitJoin();
      publish();
    }
    return true;
  }
  if (command.name == "coop_ready") {
    if (!isHost_ && connection_.connected() && connection_.link() != nullptr) {
      if (localReady_) {
        if (const auto sent =
                connection_.link()->send(game::coop::Message{game::coop::Ready{false}});
            sent) {
          localReady_ = false;
          connection_.markRoomReady(false);
          localError_.clear();
        } else {
          localError_ = sent.error().message;
        }
      } else {
        selectingGuestProfile_ = true;
        guestProfileRequest_ = true;
      }
      publish();
    }
    return true;
  }
  if (command.name == "coop_start") {
    if (isHost_ && connection_.connected() && remoteReady_ && guestSaveText_) {
      startRequested_ = true;
      selectingHostStart_ = true;
    }
    return true;
  }
  if (command.name == "coop_close") {
    // Keep a ready peer alive while the player leaves the lobby to load their save. An unfinished
    // attempt is still cancelled when the page closes.
    if (!connection_.connected())
      connection_.close();
    active_ = false;
    localReady_ = false;
    remoteReady_ = false;
    startRequested_ = false;
    guestProfileRequest_ = false;
    selectingGuestProfile_ = false;
    selectingHostStart_ = false;
    publish();
    return true;
  }
  if (!active_)
    return false;
  return false;
}

void CoopScreen::type(std::string_view text, bool enter) {
  if (!takesText())
    return;
  connection_.type(text, enter);
  publish();
}

void CoopScreen::tick(double nowSeconds) {
  if (connection_.link() == nullptr || connection_.view() == CoopConnection::View::Closed) {
    // A room that just closed may still be telling the directory.
    connection_.pollRoom();
    return;
  }
  constexpr double MillisPerSecond = 1000.0;
  connection_.tick(net::Millis{static_cast<std::int64_t>(nowSeconds * MillisPerSecond)});
  if (!connection_.linkInPlay())
    receiveMessages();
  if (active_)
    publish();
}

void CoopScreen::receiveMessages() {
  if (connection_.link() == nullptr)
    return;
  while (auto message = connection_.link()->receive()) {
    if (const auto* ready = std::get_if<game::coop::Ready>(&*message); ready && isHost_) {
      remoteReady_ = ready->ready && guestSaveText_.has_value();
      connection_.markRoomReady(remoteReady_);
    } else if (const auto* chunk = std::get_if<game::coop::ProfileChunk>(&*message)) {
      const bool guestProfile = isHost_ && chunk->kind == game::coop::ProfilePayloadKind::GuestSave;
      const bool hostSave = !isHost_ && chunk->kind == game::coop::ProfilePayloadKind::HostSave;
      if (!guestProfile && !hostSave)
        continue;
      auto& assembler = guestProfile ? guestAssembler_ : hostAssembler_;
      auto& transferId = guestProfile ? guestTransferId_ : hostTransferId_;
      if (!transferId || *transferId != chunk->transferId) {
        if (const auto begun = assembler.begin(chunk->transferId, chunk->count, chunk->kind);
            !begun) {
          transferId.reset();
          continue;
        }
        transferId = chunk->transferId;
      }
      const auto assembled = assembler.push(*chunk);
      if (!assembled) {
        assembler.reset();
        transferId.reset();
      } else if (*assembled) {
        std::string text{reinterpret_cast<const char*>((**assembled).data()), (**assembled).size()};
        // The other player's partner, drawn beside them in play.
        if (auto profile = extractCoopGuestPartnerProfile(text); profile) {
          remotePartnerSpecies_ = profile->species;
        } else {
          remotePartnerSpecies_.clear();
          core::logWarning("coop: the other player's partner cannot be shown: {}",
                           profile.error().message);
        }
        if (guestProfile) {
          guestSaveText_ = std::move(text);
        } else {
          hostSaveText_ = std::move(text);
        }
      }
    } else if (const auto* state = std::get_if<game::coop::JoinState>(&*message);
               state && !isHost_) {
      remoteJoinState_ = *state;
    }
  }
  completeRemoteStart();
}

void CoopScreen::completeRemoteStart() {
  if (isHost_ || !remoteJoinState_ || !hostSaveText_ || !guestSaveText_ || !hostTransferId_ ||
      *hostTransferId_ != remoteJoinState_->saveTransferId) {
    return;
  }
  auto combined = hostWorldWithGuestPartner(*hostSaveText_, *guestSaveText_);
  if (!combined) {
    localError_ = combined.error().message;
    return;
  }
  const auto parsed = game::save::parseSaveFile(*combined);
  if (!parsed || parsed->header.summary.map != remoteJoinState_->mapId) {
    localError_ = parsed ? "host_save_map_mismatch" : parsed.error().message;
    return;
  }
  remoteStartText_ = std::move(*combined);
  connection_.markRoomPlaying();
}

std::optional<std::string> CoopScreen::takeRemoteStartText() {
  return std::exchange(remoteStartText_, std::nullopt);
}

core::Result<void> CoopScreen::sendProfile(game::coop::ProfilePayloadKind kind,
                                           std::uint32_t transferId, std::string_view saveText) {
  if (connection_.link() == nullptr || saveText.empty() ||
      saveText.size() > game::coop::MaxProfileBytes) {
    return core::fail("cooperative save profile is empty, too large, or disconnected");
  }
  const auto* begin = reinterpret_cast<const std::uint8_t*>(saveText.data());
  const std::size_t count =
      (saveText.size() + game::coop::MaxProfileChunkBytes - 1) / game::coop::MaxProfileChunkBytes;
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t offset = index * game::coop::MaxProfileChunkBytes;
    const std::size_t size = std::min(game::coop::MaxProfileChunkBytes, saveText.size() - offset);
    game::coop::ProfileChunk chunk{.transferId = transferId,
                                   .index = static_cast<std::uint16_t>(index),
                                   .count = static_cast<std::uint16_t>(count),
                                   .kind = kind,
                                   .bytes = net::Bytes{begin + offset, begin + offset + size}};
    if (auto sent = connection_.link()->send(game::coop::Message{std::move(chunk)}); !sent) {
      return sent;
    }
  }
  return {};
}

core::Result<void> CoopScreen::provideGuestSave(std::string_view saveText) {
  if (!selectingGuestProfile_ || isHost_ || connection_.link() == nullptr) {
    return core::fail("the guest profile was not requested by this room");
  }
  if (const auto profile = extractCoopGuestPartnerProfile(saveText); !profile) {
    return std::unexpected{profile.error()};
  }
  std::uint32_t transferId = nextTransferId_++;
  if (transferId == 0)
    transferId = nextTransferId_++;
  if (auto sent = sendProfile(game::coop::ProfilePayloadKind::GuestSave, transferId, saveText);
      !sent) {
    return sent;
  }
  guestSaveText_ = std::string{saveText};
  guestTransferId_ = transferId;
  if (auto sent = connection_.link()->send(game::coop::Message{game::coop::Ready{true}}); !sent) {
    return sent;
  }
  localReady_ = true;
  connection_.markRoomReady(true);
  selectingGuestProfile_ = false;
  guestProfileRequest_ = false;
  localError_.clear();
  // Back to the room the player readied in (ui/online_room).
  ui_->show("online_room", true);
  publish();
  return {};
}

core::Result<void> CoopScreen::sendHostStart(std::string_view saveText) {
  if (!selectingHostStart_ || !isHost_ || !remoteReady_ || !guestSaveText_ ||
      connection_.link() == nullptr) {
    return core::fail("the host cannot start this room yet");
  }
  const auto save = game::save::parseSaveFile(saveText);
  if (!save)
    return std::unexpected{save.error()};
  if (save->header.summary.map.empty())
    return core::fail("the host save has no map");
  std::uint32_t transferId = nextTransferId_++;
  if (transferId == 0)
    transferId = nextTransferId_++;
  const std::size_t count =
      (saveText.size() + game::coop::MaxProfileChunkBytes - 1) / game::coop::MaxProfileChunkBytes;
  if (count == 0 || count > game::coop::MaxProfileChunks ||
      saveText.size() > game::coop::MaxProfileBytes) {
    return core::fail("the host save exceeds the cooperative transfer limit");
  }
  game::coop::JoinState state{.mapId = save->header.summary.map,
                              .saveTransferId = transferId,
                              .saveChunkCount = static_cast<std::uint16_t>(count),
                              .entities = {}};
  if (auto sent = sendProfile(game::coop::ProfilePayloadKind::HostSave, transferId, saveText);
      !sent) {
    return sent;
  }
  if (auto sent = connection_.link()->send(game::coop::Message{std::move(state)}); !sent) {
    return sent;
  }
  selectingHostStart_ = false;
  startRequested_ = false;
  connection_.markRoomPlaying();
  return {};
}

void CoopScreen::joinCode(std::string_view code) {
  if (!active_) {
    connection_.open();
    active_ = true;
  }
  isHost_ = false;
  localReady_ = false;
  remoteReady_ = false;
  localError_.clear();
  selectingGuestProfile_ = false;
  connection_.beginJoin();
  (void)connection_.join(code);
  publish();
}

void CoopScreen::leave() {
  connection_.close();
  active_ = false;
  isHost_ = false;
  localReady_ = false;
  remoteReady_ = false;
  startRequested_ = false;
  guestProfileRequest_ = false;
  selectingGuestProfile_ = false;
  selectingHostStart_ = false;
  localError_.clear();
  guestSaveText_.reset();
  hostSaveText_.reset();
  publish();
}

std::string CoopScreen::error() const {
  return localError_.empty() ? std::string{connection_.error()} : localError_;
}

void CoopScreen::reportError(std::string message) {
  localError_ = std::move(message);
  publish();
}

void CoopScreen::publish() {
  ui_->setSetting("coop.view", std::string{coopConnectionViewName(connection_.view())});
  ui_->setSetting("coop.code", connection_.code());
  ui_->setSetting("coop.typed", connection_.typed());
  ui_->setSetting("coop.error",
                  localError_.empty() ? std::string{connection_.error()} : localError_);
  ui_->setSetting("coop.role", std::string{isHost_ ? "host" : "guest"});
  ui_->setSetting("coop.connected", connection_.connected());
  ui_->setSetting("coop.local_ready", localReady_);
  ui_->setSetting("coop.remote_ready", remoteReady_);
  ui_->setSetting("coop.can_start", remoteReady_ && guestSaveText_.has_value());
}

} // namespace dwe::app
