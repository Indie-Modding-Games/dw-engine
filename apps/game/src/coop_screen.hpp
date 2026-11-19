#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/coop/profile_transfer.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "coop_connection.hpp"
#include "options.hpp"

namespace dwe::app {

inline constexpr std::string_view CoopOpenCommand = "coop_open";

// The signaling server the co-op rooms use: --versus-server, else the packs' network config; empty
// when neither names one.
std::string coopSignalServer(const assets::VirtualFileSystem& files, const Options& options);

// The main-menu cooperative lobby: creates or joins a WebRTC room and keeps the validated coop
// handshake alive. The connected link is exposed for the gameplay bridge to attach next.
class CoopScreen {
public:
  CoopScreen(const assets::VirtualFileSystem& files, const Options& options, ui::Ui& ui);
  bool handle(const ui::UiCommand& command);
  [[nodiscard]] bool takesText() const { return active_ && connection_.takesText(); }
  void type(std::string_view text, bool enter);
  void tick(double nowSeconds);
  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] game::coop::CoopLink* link() { return connection_.link(); }
  void setPlayerName(std::string name) { connection_.setPlayerName(std::move(name)); }
  [[nodiscard]] std::string remoteName() const { return connection_.remoteName(); }
  [[nodiscard]] std::uint64_t linkGeneration() const { return connection_.linkGeneration(); }
  void handLinkToPlay() { connection_.handLinkToPlay(); }
  void takeLinkBack() { connection_.takeLinkBack(); }
  [[nodiscard]] bool connected() const { return connection_.connected(); }
  // The species of the other player's partner, from the save they sent (empty until it arrives).
  [[nodiscard]] std::string_view remotePartnerSpecies() const { return remotePartnerSpecies_; }
  [[nodiscard]] bool isHost() const { return isHost_; }
  [[nodiscard]] bool guestReady() const { return remoteReady_; }
  [[nodiscard]] bool localReady() const { return localReady_; }
  [[nodiscard]] CoopConnection::View view() const { return connection_.view(); }
  [[nodiscard]] std::string code() const { return connection_.code(); }
  // Why the room failed or the last step was refused: an error code, or a message.
  [[nodiscard]] std::string error() const;
  [[nodiscard]] bool canStart() const { return remoteReady_ && guestSaveText_.has_value(); }
  // The rooms screen's steps (online_rooms.hpp: CoopRoomBackend): the same as the coop_*
  // commands, except that a code joins at once and leaving closes even a connected room.
  void joinCode(std::string_view code);
  void leave();
  [[nodiscard]] bool takeStartRequested() { return std::exchange(startRequested_, false); }
  [[nodiscard]] bool takeGuestProfileRequest() {
    return std::exchange(guestProfileRequest_, false);
  }
  [[nodiscard]] bool selectingGuestProfile() const { return selectingGuestProfile_; }
  [[nodiscard]] bool selectingHostStart() const { return selectingHostStart_; }
  [[nodiscard]] std::optional<std::string> takeRemoteStartText();
  [[nodiscard]] core::Result<void> provideGuestSave(std::string_view saveText);
  [[nodiscard]] core::Result<void> sendHostStart(std::string_view saveText);
  void reportError(std::string message);

private:
  void publish();
  void receiveMessages();
  void completeRemoteStart();
  [[nodiscard]] core::Result<void> sendProfile(game::coop::ProfilePayloadKind kind,
                                               std::uint32_t transferId, std::string_view saveText);

  ui::Ui* ui_;
  CoopConnection connection_;
  bool active_ = false;
  bool isHost_ = false;
  bool localReady_ = false;
  bool remoteReady_ = false;
  bool startRequested_ = false;
  bool guestProfileRequest_ = false;
  bool selectingGuestProfile_ = false;
  bool selectingHostStart_ = false;
  std::string localError_;
  std::uint32_t nextTransferId_ = 1;
  std::optional<std::uint32_t> guestTransferId_;
  std::optional<std::uint32_t> hostTransferId_;
  game::coop::ProfileAssembler guestAssembler_;
  game::coop::ProfileAssembler hostAssembler_;
  std::optional<std::string> guestSaveText_;
  std::optional<std::string> hostSaveText_;
  std::string remotePartnerSpecies_;
  std::optional<game::coop::JoinState> remoteJoinState_;
  std::optional<std::string> remoteStartText_;
};

} // namespace dwe::app
