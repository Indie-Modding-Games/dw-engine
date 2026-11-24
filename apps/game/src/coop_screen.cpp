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
  // A co-op room seats its host and up to three guests (#352).
  settings->seats = static_cast<int>(game::coop::MaxPlayers);
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
          .seat =
              [settings](std::string_view code, std::string_view token, int taken) {
                return net::RtcPeerLink::hostSeat(*settings, code, token, taken,
                                                  net::openWebSocket);
              },
          .seats = settings->seats,
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
    hostAssembler_.reset();
    lobby_.reset();
    guests_.clear();
    lobbySent_.clear();
    localError_.clear();
    publish();
    return true;
  }
  if (command.name == CoopHostCommand) {
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
    guests_.clear();
    lobbySent_.clear();
    localError_.clear();
    selectingHostStart_ = false;
    // The rooms screens send whether the room is public (listed); a plain coop_host is private.
    const bool* listed = std::get_if<bool>(&command.value);
    (void)connection_.host(listed != nullptr && *listed);
    connection_.setOpenToNewPlayers(openToJoin_);
    publish();
    return true;
  }
  if (command.name == CoopJoinCommand) {
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
  if (command.name == CoopConnectCommand) {
    if (!isHost_ && active_ &&
        (connection_.view() == CoopConnection::View::JoinEntry ||
         connection_.view() == CoopConnection::View::Error)) {
      (void)connection_.submitJoin();
      publish();
    }
    return true;
  }
  if (command.name == CoopReadyCommand) {
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
  if (command.name == CoopOpenToJoinCommand) {
    // The host's choice: whether its game, once under way, takes new players (#356).
    if (const bool* open = std::get_if<bool>(&command.value); open != nullptr && isHost_) {
      openToJoin_ = *open;
      openToJoinChange_ = *open;
      connection_.setOpenToNewPlayers(*open);
      publish();
    }
    return true;
  }
  if (command.name == CoopStartCommand) {
    if (isHost_ && connection_.connected() && guestReady()) {
      startRequested_ = true;
      selectingHostStart_ = true;
    }
    return true;
  }
  if (command.name == CoopCloseCommand) {
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
  if (isHost_) {
    followGuests();
  }
  receiveMessages();
  // Back in the room it dropped out of, while its game is under way: the save it brought takes it
  // back in without asking again (#356).
  if (!isHost_ && !localReady_ && connection_.connected() && connection_.returning() && brought_ &&
      brought_->code == connection_.code() && lobby_ && lobby_->playing) {
    const std::string text = brought_->text;
    if (auto sent = sendGuestSave(text); !sent) {
      localError_ = sent.error().message;
    } else {
      core::logInfo("coop: back in the room, rejoining its game with the save brought before");
    }
  }
  if (active_)
    publish();
}

void CoopScreen::followGuests() {
  const auto in = connection_.guests();
  // A guest gone (left, dropped) takes its lobby state with it; a new link for an id is a new
  // guest.
  std::erase_if(guests_, [&](const auto& entry) {
    return std::ranges::none_of(in, [&](const CoopConnection::Guest& guest) {
      return guest.id == entry.first && guest.generation == entry.second.generation;
    });
  });
  for (const CoopConnection::Guest& guest : in) {
    if (!guests_.contains(guest.id)) {
      guests_[guest.id].generation = guest.generation;
    }
  }
  std::erase_if(lobbySent_, [&](const auto& entry) {
    return std::ranges::none_of(
        in, [&](const CoopConnection::Guest& guest) { return guest.generation == entry.first; });
  });
  const bool anyReady = guestReady();
  if (anyReady != remoteReady_) {
    remoteReady_ = anyReady;
    connection_.markRoomReady(anyReady);
  }
  // Every guest still in the lobby hears who is in the room whenever that changes.
  const game::coop::Lobby lobby = lobbyMessage();
  for (const CoopConnection::Guest& guest : in) {
    if (connection_.linkInPlay(guest.generation)) {
      continue;
    }
    const auto sent = lobbySent_.find(guest.generation);
    if (sent != lobbySent_.end() && sent->second == lobby) {
      continue;
    }
    if (guest.link->send(game::coop::Message{lobby})) {
      lobbySent_[guest.generation] = lobby;
    }
  }
}

game::coop::Lobby CoopScreen::lobbyMessage() const {
  game::coop::Lobby lobby{.capacity = static_cast<std::uint8_t>(connection_.seats()), .seats = {}};
  for (const CoopLobbyMember& member : lobbyMembers()) {
    if (member.arriving) {
      continue;
    }
    lobby.seats.push_back(
        {.id = member.id,
         // The host's own row names it as its profile does (the screen shows "you" without it).
         .name = (member.host ? connection_.playerName() : member.name).substr(0, 64),
         .state = member.ready ? game::coop::SeatState::Ready : game::coop::SeatState::Connected});
  }
  lobby.playing = connection_.playing();
  lobby.open = connection_.openToNewPlayers();
  lobby.difficulty = difficulty_;
  return lobby;
}

void CoopScreen::setOpenToJoin(bool open) {
  openToJoin_ = open;
  if (isHost_) {
    connection_.setOpenToNewPlayers(open);
    publish();
  }
}

std::optional<game::coop::CoopDifficulty> CoopScreen::coopDifficulty() const {
  if (isHost_) {
    return difficulty_;
  }
  return lobby_.has_value() ? std::optional{lobby_->difficulty} : std::nullopt;
}

std::vector<CoopLobbyMember> CoopScreen::lobbyMembers() const {
  std::vector<CoopLobbyMember> members;
  if (isHost_) {
    members.push_back({.id = game::coop::HostPlayerId,
                       .name = {},
                       .host = true,
                       .ready = true,
                       .arriving = false,
                       .local = true});
    for (const CoopConnection::Guest& guest : connection_.guests()) {
      const auto lobby = guests_.find(guest.id);
      members.push_back({.id = guest.id,
                         .name = guest.link->remoteName(),
                         .host = false,
                         .ready = lobby != guests_.end() && lobby->second.ready,
                         .arriving = false,
                         .local = false});
    }
    for (const game::coop::PlayerId id : connection_.arriving()) {
      members.push_back(
          {.id = id, .name = {}, .host = false, .ready = false, .arriving = true, .local = false});
    }
    return members;
  }
  if (!connection_.connected()) {
    return members;
  }
  const game::coop::PlayerId self =
      connection_.link() != nullptr ? connection_.link()->localPlayer() : game::coop::GuestPlayerId;
  if (lobby_) {
    for (const game::coop::LobbySeat& seat : lobby_->seats) {
      const bool local = seat.id == self;
      members.push_back({.id = seat.id,
                         .name = seat.name,
                         .host = seat.id == game::coop::HostPlayerId,
                         .ready = local ? localReady_ : seat.state == game::coop::SeatState::Ready,
                         .arriving = false,
                         .local = local});
    }
    if (std::ranges::none_of(members, &CoopLobbyMember::local)) {
      members.push_back({.id = self,
                         .name = {},
                         .host = false,
                         .ready = localReady_,
                         .arriving = false,
                         .local = true});
    }
  } else {
    // An older host, or its lobby not here yet: the host and this guest.
    members.push_back({.id = game::coop::HostPlayerId,
                       .name = connection_.remoteName(),
                       .host = true,
                       .ready = false,
                       .arriving = false,
                       .local = false});
    members.push_back({.id = self,
                       .name = {},
                       .host = false,
                       .ready = localReady_,
                       .arriving = false,
                       .local = true});
  }
  std::ranges::sort(members, {}, &CoopLobbyMember::id);
  return members;
}

int CoopScreen::capacity() const {
  if (isHost_) {
    return connection_.seats();
  }
  return lobby_ ? lobby_->capacity : 2;
}

bool CoopScreen::guestReady() const {
  if (!isHost_) {
    return false;
  }
  const auto in = connection_.guests();
  return !in.empty() && std::ranges::all_of(in, [&](const CoopConnection::Guest& guest) {
    const auto lobby = guests_.find(guest.id);
    return lobby != guests_.end() && lobby->second.ready && lobby->second.saveText.has_value();
  });
}

std::string CoopScreen::guestName(game::coop::PlayerId id) const {
  for (const CoopConnection::Guest& guest : connection_.guests()) {
    if (guest.id == id) {
      return guest.link->remoteName();
    }
  }
  return {};
}

std::string_view CoopScreen::partnerSpeciesOf(game::coop::PlayerId id) const {
  const auto lobby = guests_.find(id);
  return lobby != guests_.end() ? std::string_view{lobby->second.partnerSpecies}
                                : std::string_view{};
}

std::string CoopScreen::saveModeOf(game::coop::PlayerId id) const {
  const auto lobby = guests_.find(id);
  return lobby != guests_.end() && lobby->second.saveText ? coopSaveMode(*lobby->second.saveText)
                                                          : std::string{};
}

std::string_view CoopScreen::remotePartnerSpecies() const {
  if (!isHost_) {
    return remotePartnerSpecies_;
  }
  const auto in = connection_.guests();
  return in.empty() ? std::string_view{} : partnerSpeciesOf(in.front().id);
}

void CoopScreen::receiveMessages() {
  if (isHost_) {
    for (const CoopConnection::Guest& guest : connection_.guests()) {
      if (!connection_.linkInPlay(guest.generation)) {
        receiveFrom(*guest.link, &guests_[guest.id]);
      }
    }
    return;
  }
  if (connection_.link() != nullptr && !connection_.linkInPlay()) {
    receiveFrom(*connection_.link(), nullptr);
  }
}

void CoopScreen::receiveFrom(game::coop::CoopLink& link, GuestLobby* guest) {
  while (auto message = link.receive()) {
    if (const auto* ready = std::get_if<game::coop::Ready>(&*message); ready && guest != nullptr) {
      guest->ready = ready->ready && guest->saveText.has_value();
    } else if (const auto* chunk = std::get_if<game::coop::ProfileChunk>(&*message)) {
      const bool guestProfile =
          guest != nullptr && chunk->kind == game::coop::ProfilePayloadKind::GuestSave;
      const bool hostSave =
          guest == nullptr && chunk->kind == game::coop::ProfilePayloadKind::HostSave;
      if (!guestProfile && !hostSave)
        continue;
      auto& assembler = guestProfile ? guest->assembler : hostAssembler_;
      auto& transferId = guestProfile ? guest->transferId : hostTransferId_;
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
        std::string species;
        if (auto profile = extractCoopGuestPartnerProfile(text); profile) {
          species = profile->species;
        } else {
          core::logWarning("coop: the other player's partner cannot be shown: {}",
                           profile.error().message);
        }
        if (guestProfile) {
          guest->partnerSpecies = std::move(species);
          guest->saveText = std::move(text);
        } else {
          remotePartnerSpecies_ = std::move(species);
          hostSaveText_ = std::move(text);
        }
      }
    } else if (const auto* state = std::get_if<game::coop::JoinState>(&*message);
               state && guest == nullptr) {
      remoteJoinState_ = *state;
    } else if (const auto* lobby = std::get_if<game::coop::Lobby>(&*message);
               lobby && guest == nullptr) {
      lobby_ = *lobby;
    }
  }
  if (guest == nullptr) {
    completeRemoteStart();
  }
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
  hostVariant_ = coopSaveMode(*hostSaveText_);
  ownVariant_ = coopSaveMode(*guestSaveText_);
  // One game per JoinState: once the link comes back from the game, the same state must not start
  // the host's world again (after the host left, that game would come back over the title).
  remoteJoinState_.reset();
  connection_.markRoomPlaying();
}

std::optional<std::string> CoopScreen::takeRemoteStartText() {
  return std::exchange(remoteStartText_, std::nullopt);
}

core::Result<void> CoopScreen::sendProfile(game::coop::CoopLink& link,
                                           game::coop::ProfilePayloadKind kind,
                                           std::uint32_t transferId, std::string_view saveText) {
  if (saveText.empty() || saveText.size() > game::coop::MaxProfileBytes) {
    return core::fail("cooperative save profile is empty, too large, or disconnected");
  }
  const auto* begin = reinterpret_cast<const std::uint8_t*>(saveText.data());
  const std::size_t count = game::coop::profileChunkCount(saveText.size());
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t offset = index * game::coop::MaxProfileChunkBytes;
    const std::size_t size = std::min(game::coop::MaxProfileChunkBytes, saveText.size() - offset);
    game::coop::ProfileChunk chunk{.transferId = transferId,
                                   .index = static_cast<std::uint16_t>(index),
                                   .count = static_cast<std::uint16_t>(count),
                                   .kind = kind,
                                   .bytes = net::Bytes{begin + offset, begin + offset + size}};
    if (auto sent = link.send(game::coop::Message{std::move(chunk)}); !sent) {
      return sent;
    }
  }
  return {};
}

std::uint32_t CoopScreen::nextTransferId() {
  if (nextTransferId_ == 0) {
    ++nextTransferId_;
  }
  return nextTransferId_++;
}

core::Result<void> CoopScreen::provideGuestSave(std::string_view saveText, game::save::SlotId slot,
                                                bool backup) {
  if (!selectingGuestProfile_ || isHost_ || connection_.link() == nullptr) {
    return core::fail("the guest profile was not requested by this room");
  }
  if (const auto profile = extractCoopGuestPartnerProfile(saveText); !profile) {
    return std::unexpected{profile.error()};
  }
  if (auto sent = sendGuestSave(saveText); !sent) {
    return sent;
  }
  brought_->slot = slot;
  brought_->backup = backup;
  selectingGuestProfile_ = false;
  guestProfileRequest_ = false;
  localError_.clear();
  // Back to the room the player readied in (ui/online_room).
  ui_->show("online_room", true);
  publish();
  return {};
}

core::Result<void> CoopScreen::sendGuestSave(std::string_view saveText) {
  if (connection_.link() == nullptr) {
    return core::fail("the guest is not in a room");
  }
  const std::uint32_t transferId = nextTransferId();
  if (auto sent = sendProfile(*connection_.link(), game::coop::ProfilePayloadKind::GuestSave,
                              transferId, saveText);
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
  brought_ = BroughtSave{.code = connection_.code(),
                         .text = std::string{saveText},
                         .slot = brought_ ? brought_->slot : std::nullopt,
                         .backup = brought_ && brought_->backup};
  return {};
}

std::optional<CoopGuestHome::Home> CoopScreen::broughtHome() const {
  if (isHost_ || !brought_ || !brought_->slot) {
    return std::nullopt;
  }
  return CoopGuestHome::Home{
      .slot = *brought_->slot, .backup = brought_->backup, .text = brought_->text};
}

void CoopScreen::keepBrought(std::string text) {
  if (brought_) {
    brought_->text = std::move(text);
    brought_->backup = false;
  }
}

core::Result<void> CoopScreen::sendWorld(game::coop::CoopLink& link, std::string_view saveText) {
  const auto save = game::save::parseSaveFile(saveText);
  if (!save)
    return std::unexpected{save.error()};
  if (save->header.summary.map.empty())
    return core::fail("the host save has no map");
  const std::uint32_t transferId = nextTransferId();
  if (auto sent = sendProfile(link, game::coop::ProfilePayloadKind::HostSave, transferId, saveText);
      !sent) {
    return sent;
  }
  return link.send(game::coop::Message{game::coop::JoinState{
      .mapId = save->header.summary.map,
      .saveTransferId = transferId,
      .saveChunkCount = static_cast<std::uint16_t>(game::coop::profileChunkCount(saveText.size())),
      .entities = {}}});
}

std::vector<game::coop::PlayerId> CoopScreen::guestsWaitingForWorld() const {
  std::vector<game::coop::PlayerId> out;
  if (!isHost_ || !connection_.playing()) {
    return out;
  }
  for (const CoopConnection::Guest& guest : connection_.guests()) {
    const auto lobby = guests_.find(guest.id);
    if (lobby != guests_.end() && lobby->second.ready && lobby->second.saveText &&
        !lobby->second.started) {
      out.push_back(guest.id);
    }
  }
  return out;
}

bool CoopScreen::guestStarted(game::coop::PlayerId id) const {
  const auto lobby = guests_.find(id);
  return lobby != guests_.end() && lobby->second.started;
}

bool CoopScreen::joinedLate(game::coop::PlayerId id) const {
  const auto lobby = guests_.find(id);
  return lobby != guests_.end() && lobby->second.late;
}

core::Result<void> CoopScreen::sendJoinInProgress(game::coop::PlayerId id,
                                                  std::string_view saveText) {
  const auto lobby = guests_.find(id);
  if (!isHost_ || !connection_.playing() || lobby == guests_.end() || lobby->second.started) {
    return core::fail("no guest waits for the game under way");
  }
  for (const CoopConnection::Guest& guest : connection_.guests()) {
    if (guest.id != id) {
      continue;
    }
    if (auto sent = sendWorld(*guest.link, saveText); !sent) {
      return sent;
    }
    lobby->second.started = true;
    lobby->second.late = true;
    core::logInfo("coop: player {} joins the game under way", id);
    return {};
  }
  return core::fail("the guest left");
}

std::string CoopScreen::droppedFrom() const {
  const game::coop::CoopLink* link = connection_.link();
  if (isHost_ || link == nullptr || link->phase() != game::coop::LinkPhase::Failed) {
    return {};
  }
  // The route went quiet or broke (not the host's own goodbye): the seat waits for it.
  const auto failure = link->failure();
  if (failure != game::coop::LinkFailure::Silence &&
      failure != game::coop::LinkFailure::Disconnected) {
    return {};
  }
  return connection_.code();
}

core::Result<void> CoopScreen::sendHostStart(std::string_view saveText) {
  const auto in = connection_.guests();
  if (!selectingHostStart_ || !isHost_ || !guestReady() || in.empty()) {
    return core::fail("the host cannot start this room yet");
  }
  // Every guest gets the host's world.
  for (const CoopConnection::Guest& guest : in) {
    if (auto sent = sendWorld(*guest.link, saveText); !sent) {
      return sent;
    }
    guests_[guest.id].started = true;
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
  lobby_.reset();
  guests_.clear();
  lobbySent_.clear();
  // A start the room had prepared goes with it.
  remoteJoinState_.reset();
  remoteStartText_.reset();
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
  ui_->setSetting("coop.remote_ready", isHost_ ? guestReady() : remoteReady_);
  ui_->setSetting("coop.can_start", guestReady());
  ui_->setSetting("coop.in_progress", gameUnderway());
  ui_->setSetting("coop.open_to_join", openToNewPlayers());
}

} // namespace dwe::app
