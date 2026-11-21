#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/coop/profile_transfer.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/rtc_peer_link.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop_connection.hpp"
#include "coop_guest_return.hpp"
#include "options.hpp"

namespace dwe::app {

inline constexpr std::string_view CoopOpenCommand = "coop_open";

// The signaling server the co-op rooms use: --versus-server, else the packs' network config; empty
// when neither names one.
std::string coopSignalServer(const assets::VirtualFileSystem& files, const Options& options);

// A player in the room's lobby as the screens show it (#352): the host, each guest in, and a
// guest still on its way in.
struct CoopLobbyMember {
  game::coop::PlayerId id = game::coop::HostPlayerId;
  std::string name;
  bool host = false;
  bool ready = false;
  // On its way in: its link found the room and it has not said hello yet.
  bool arriving = false;
  // The player at this machine.
  bool local = false;
};

inline constexpr std::string_view CoopOpenToJoinSetting = "coop_open_to_join";

// The main-menu cooperative lobby: creates or joins a WebRTC room and keeps the validated coop
// handshake alive. The connected links are exposed for the gameplay bridges to attach next. The
// host keeps each guest's lobby state (its ready mark, the save it brings and its partner) by
// player id and tells every guest who is in the room (#352).
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
  void setPlayerIcon(std::string icon) { connection_.setPlayerIcon(std::move(icon)); }
  // Connect the next rooms through a relay (TURN) only: the other player never learns this
  // machine's address (the online profile's «relay only»).
  void setRelayOnly(bool relayOnly);
  // The other player's name (the first guest's on the host, the host's on the guest).
  [[nodiscard]] std::string remoteName() const { return connection_.remoteName(); }
  // The host's guests in the room, by id (none on a guest).
  [[nodiscard]] std::vector<CoopConnection::Guest> guests() const { return connection_.guests(); }
  // A guest's name (its online profile), on the host.
  [[nodiscard]] std::string guestName(game::coop::PlayerId id) const;
  // The species of a guest's partner, from the save it sent, on the host (empty until it arrives).
  [[nodiscard]] std::string_view partnerSpeciesOf(game::coop::PlayerId id) const;
  // Everyone in the room, host first then by id, as the host knows it (on a guest, as the host's
  // lobby last said; before it, the host and this guest).
  [[nodiscard]] std::vector<CoopLobbyMember> lobbyMembers() const;
  // How many players the room seats, host included.
  [[nodiscard]] int capacity() const;
  // The directory key of the rooms this game can join (net::compatKey).
  [[nodiscard]] const std::string& compat() const { return connection_.compat(); }
  // The room's phase (nullptr before the first room).
  [[nodiscard]] const net::RoomSession* room() const { return connection_.room(); }
  [[nodiscard]] CoopConnection& connection() { return connection_; }
  [[nodiscard]] std::uint64_t linkGeneration() const { return connection_.linkGeneration(); }
  void handLinkToPlay() { connection_.handLinkToPlay(); }
  void handLinkToPlay(std::uint64_t generation) { connection_.handLinkToPlay(generation); }
  void takeLinkBack(std::uint64_t generation) { connection_.takeLinkBack(generation); }
  void takeLinkBack() { connection_.takeLinkBack(); }
  [[nodiscard]] bool connected() const { return connection_.connected(); }
  // On a guest, the species of the host's partner, from the save it sent (empty until it arrives);
  // on the host, its first guest's (partnerSpeciesOf).
  [[nodiscard]] std::string_view remotePartnerSpecies() const;
  [[nodiscard]] bool isHost() const { return isHost_; }
  // On the host: every guest in is ready (with its save), and there is at least one.
  [[nodiscard]] bool guestReady() const;
  [[nodiscard]] bool localReady() const { return localReady_; }
  [[nodiscard]] CoopConnection::View view() const { return connection_.view(); }
  [[nodiscard]] std::string code() const { return connection_.code(); }
  // Why the room failed or the last step was refused: an error code, or a message.
  [[nodiscard]] std::string error() const;
  [[nodiscard]] bool canStart() const { return guestReady(); }
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
  [[nodiscard]] core::Result<void> provideGuestSave(std::string_view saveText,
                                                    game::save::SlotId slot, bool backup);
  [[nodiscard]] std::optional<CoopGuestHome::Home> broughtHome() const;
  void keepBrought(std::string text);
  [[nodiscard]] core::Result<void> sendHostStart(std::string_view saveText);
  // A game in progress (#356). On the host: the guests in that picked their save while the game
  // is under way and still wait for the host's world, by id; `sendJoinInProgress` sends one of
  // them the host's world as it is now (its game's save of this moment), as the start sent it to
  // the others. Only a guest sent the world plays (guestStarted).
  [[nodiscard]] std::vector<game::coop::PlayerId> guestsWaitingForWorld() const;
  [[nodiscard]] core::Result<void> sendJoinInProgress(game::coop::PlayerId id,
                                                      std::string_view saveText);
  [[nodiscard]] bool guestStarted(game::coop::PlayerId id) const;
  // The guest joined (or came back to) the game once it was under way.
  [[nodiscard]] bool joinedLate(game::coop::PlayerId id) const;
  // On a guest: the host's lobby says its game is under way (a pick of the save joins it).
  [[nodiscard]] bool gameUnderway() const {
    return isHost_ ? connection_.playing() : lobby_.has_value() && lobby_->playing;
  }
  void setOpenToJoin(bool open);
  [[nodiscard]] std::optional<bool> takeOpenToJoinChange() {
    return std::exchange(openToJoinChange_, std::nullopt);
  }
  // Whether the game under way takes new players (the host's choice, open by default).
  [[nodiscard]] bool openToNewPlayers() const {
    return isHost_ ? connection_.openToNewPlayers() : !lobby_.has_value() || lobby_->open;
  }
  void setCoopDifficulty(game::coop::CoopDifficulty difficulty) { difficulty_ = difficulty; }
  [[nodiscard]] std::optional<game::coop::CoopDifficulty> coopDifficulty() const;
  // On a guest that dropped out of a game under way: the room's code, to go back to it (it keeps
  // its seat for a while, CoopRejoinGrace); empty when the host ended the game itself.
  [[nodiscard]] std::string droppedFrom() const;
  void reportError(std::string message);

private:
  // What the host keeps of one guest in its lobby.
  struct GuestLobby {
    std::uint64_t generation = 0;
    bool ready = false;
    std::optional<std::uint32_t> transferId;
    game::coop::ProfileAssembler assembler;
    std::optional<std::string> saveText;
    std::string partnerSpecies;
    // It was sent the host's world (the start, or a join while the game is under way).
    bool started = false;
    bool late = false;
  };

  void publish();
  void receiveMessages();
  void receiveFrom(game::coop::CoopLink& link, GuestLobby* guest);
  // The host's lobby follows its links (a guest in, a guest gone) and tells every guest.
  void followGuests();
  [[nodiscard]] game::coop::Lobby lobbyMessage() const;
  void completeRemoteStart();
  // Sends the guest's save and readiness to the host.
  [[nodiscard]] core::Result<void> sendGuestSave(std::string_view saveText);
  // Sends one guest the host's world: the save's chunks, then where it starts.
  [[nodiscard]] core::Result<void> sendWorld(game::coop::CoopLink& link, std::string_view saveText);
  [[nodiscard]] core::Result<void> sendProfile(game::coop::CoopLink& link,
                                               game::coop::ProfilePayloadKind kind,
                                               std::uint32_t transferId, std::string_view saveText);

  ui::Ui* ui_;
  std::shared_ptr<net::RtcSettings> rtc_;
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
  // On a guest: its own save's transfer, the host's save and partner, and the host's lobby.
  std::optional<std::uint32_t> guestTransferId_;
  std::optional<std::uint32_t> hostTransferId_;
  game::coop::ProfileAssembler hostAssembler_;
  std::optional<std::string> guestSaveText_;
  std::optional<std::string> hostSaveText_;
  std::string remotePartnerSpecies_;
  std::optional<game::coop::Lobby> lobby_;
  game::coop::CoopDifficulty difficulty_ = game::coop::CoopDifficulty::Standard;
  bool openToJoin_ = true;
  std::optional<bool> openToJoinChange_;
  // On the host: each guest in, by id, and the lobby each guest's link was last told.
  std::map<game::coop::PlayerId, GuestLobby> guests_;
  std::map<std::uint64_t, game::coop::Lobby> lobbySent_;
  std::optional<game::coop::JoinState> remoteJoinState_;
  std::optional<std::string> remoteStartText_;
  // On a guest: the save it brought to the last room, by code, so that coming back to that room
  // after a drop takes it again without asking (#356).
  struct BroughtSave {
    std::string code;
    std::string text;
    std::optional<game::save::SlotId> slot;
    bool backup = false;
  };
  std::optional<BroughtSave> brought_;
};

} // namespace dwe::app
