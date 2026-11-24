#include "online_coop_room.hpp"

#include <dwe/game/coop/protocol.hpp>
#include <dwe/ui/ui.hpp>

#include <string>
#include <string_view>

namespace dwe::app {

namespace {

std::string_view coopDifficultyName(game::coop::CoopDifficulty difficulty) {
  switch (difficulty) {
  case game::coop::CoopDifficulty::Off:
    return "off";
  case game::coop::CoopDifficulty::Hard:
    return "hard";
  case game::coop::CoopDifficulty::Standard:
    break;
  }
  return "standard";
}

} // namespace

void CoopRoomBackend::host(bool listed, double /*nowSeconds*/) {
  listed_ = listed;
  static_cast<void>(
      coop_->handle(ui::UiCommand{.name = std::string{CoopHostCommand}, .value = listed}));
}

void CoopRoomBackend::join(const RoomListing& listing, double /*nowSeconds*/) {
  listed_ = true;
  coop_->joinCode(listing.joinCode);
}

void CoopRoomBackend::joinByCode(std::string_view code, double /*nowSeconds*/) {
  listed_ = false;
  coop_->joinCode(code);
}

void CoopRoomBackend::leave() {
  coop_->leave();
}

void CoopRoomBackend::setReady(bool ready) {
  // Readying asks for the save the guest brings (the game opens the save slots); unreadying tells
  // the host at once.
  if (ready != coop_->localReady()) {
    static_cast<void>(
        coop_->handle(ui::UiCommand{.name = std::string{CoopReadyCommand}, .value = {}}));
  }
}

void CoopRoomBackend::setOpenToNewPlayers(bool open) {
  static_cast<void>(
      coop_->handle(ui::UiCommand{.name = std::string{CoopOpenToJoinCommand}, .value = open}));
}

std::optional<std::string> CoopRoomBackend::start() {
  if (!coop_->canStart()) {
    return std::string{"not_ready"};
  }
  static_cast<void>(
      coop_->handle(ui::UiCommand{.name = std::string{CoopStartCommand}, .value = {}}));
  return std::nullopt;
}

std::optional<RoomView> CoopRoomBackend::room() const {
  using View = CoopConnection::View;
  const View view = coop_->view();
  if (view == View::Closed || view == View::Menu || view == View::JoinEntry) {
    return std::nullopt;
  }
  const bool isHost = coop_->isHost();
  const bool connected = coop_->connected();
  RoomView room{.code = coop_->code(),
                .name = {},
                .nameKey = {},
                .isHost = isHost,
                .listed = listed_,
                .phase = RoomPhase::Connecting,
                .capacity = coop_->capacity(),
                .members = {},
                .error = {},
                .canStart = isHost && coop_->canStart(),
                .canReady = !isHost && connected,
                .localReady = !isHost && coop_->localReady(),
                .mode = "campaign",
                .inProgress = coop_->gameUnderway(),
                .openToNewPlayers = coop_->openToNewPlayers(),
                .coopDifficulty = {}};
  if (const auto difficulty = coop_->coopDifficulty()) {
    room.coopDifficulty = std::string{coopDifficultyName(*difficulty)};
  }
  // The room session keeps the phase (net::RoomSession); the lobby's error closes it.
  const net::RoomSession* session = coop_->room();
  room.phase = session != nullptr ? session->phase() : RoomPhase::Connecting;
  if (view == View::Error || room.phase == RoomPhase::Closed) {
    room.phase = RoomPhase::Closed;
    room.error = coop_->error();
    if (room.error.empty()) {
      room.error = "unknown";
    }
  }
  // Everyone in the room, the host first: up to four players (#352); a guest whose connection is
  // still being set up holds its seat as arriving.
  for (const CoopLobbyMember& member : coop_->lobbyMembers()) {
    room.members.push_back(unnamedMember(member.host, member.ready && !member.host, member.local));
    room.members.back().arriving = member.arriving;
    if (!member.local) {
      // The others' profile names, from their hello (or the host's lobby); an older peer sends
      // none and the screens name it by its place.
      room.members.back().name = member.name;
    }
  }
  return room;
}

} // namespace dwe::app
