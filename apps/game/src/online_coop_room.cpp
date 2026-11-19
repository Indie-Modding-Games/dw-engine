#include "online_coop_room.hpp"

#include <dwe/ui/ui.hpp>

namespace dwe::app {

void CoopRoomBackend::host(bool listed, double /*nowSeconds*/) {
  listed_ = listed;
  static_cast<void>(coop_->handle(ui::UiCommand{.name = "coop_host", .value = listed}));
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
    static_cast<void>(coop_->handle(ui::UiCommand{.name = "coop_ready", .value = {}}));
  }
}

std::optional<std::string> CoopRoomBackend::start() {
  if (!coop_->canStart()) {
    return std::string{"not_ready"};
  }
  static_cast<void>(coop_->handle(ui::UiCommand{.name = "coop_start", .value = {}}));
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
                .capacity = 2,
                .members = {},
                .error = {},
                .canStart = isHost && coop_->canStart(),
                .canReady = !isHost && connected,
                .localReady = !isHost && coop_->localReady(),
                .mode = "campaign"};
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
  // The host has no ready mark of its own: it starts the room when the guest is ready.
  if (isHost || connected) {
    room.members.push_back(unnamedMember(true, false, isHost));
    if (!isHost) {
      // The host's profile name, from its welcome (an older host sends none: the screens call it
      // the host).
      room.members.back().name = coop_->remoteName();
    }
  }
  if (connected) {
    const bool guestReady = isHost ? coop_->guestReady() : coop_->localReady();
    room.members.push_back(unnamedMember(false, guestReady, !isHost));
    if (isHost) {
      // The guest's profile name, from its hello.
      room.members.back().name = coop_->remoteName();
    }
  }
  return room;
}

} // namespace dwe::app
