#include "coop_connection.hpp"

#include <dwe/core/log.hpp>
#include <dwe/net/signal.hpp>

#include <algorithm>
#include <utility>

namespace dwe::app {
namespace {
bool codeCharacter(char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z');
}

std::string_view linkFailureName(game::coop::LinkFailure failure) {
  using Failure = game::coop::LinkFailure;
  switch (failure) {
  case Failure::None:
    return {};
  case Failure::NoAnswer:
    return "no_answer";
  case Failure::Silence:
    return "silence";
  case Failure::ProtocolMismatch:
    return "protocol_mismatch";
  case Failure::VersionMismatch:
    return "version_mismatch";
  case Failure::ContentMismatch:
    return "content_mismatch";
  case Failure::RoomFull:
    return "room_full";
  case Failure::Busy:
    return "busy";
  case Failure::Damaged:
    return "damaged";
  case Failure::Disconnected:
    return "disconnected";
  }
  return "unknown";
}

std::string_view peerFailureName(net::PeerFailure failure) {
  switch (failure) {
  case net::PeerFailure::None:
    return {};
  case net::PeerFailure::NoServer:
    return "no_server";
  case net::PeerFailure::ServerUnreachable:
    return "server_unreachable";
  case net::PeerFailure::WrongCode:
    return "wrong_code";
  case net::PeerFailure::RoomFull:
    return "room_full";
  case net::PeerFailure::TimedOut:
    return "signal_timeout";
  case net::PeerFailure::NoRoute:
    return "no_direct_route";
  case net::PeerFailure::Closed:
    return "connection_closed";
  case net::PeerFailure::Refused:
    return "server_refused";
  case net::PeerFailure::NoRelay:
    return "no_relay";
  }
  return "unknown";
}
} // namespace

void CoopConnection::open() {
  close();
  view_ = View::Menu;
}

void CoopConnection::beginRoom(net::RoomSessionDesc desc) {
  if (room_ && room_->noticesPending()) {
    closing_.push_back(std::move(*room_));
  }
  room_.emplace(std::move(desc));
}

void CoopConnection::closeRoom(std::string reason) {
  if (room_) {
    room_->close(std::move(reason));
  }
}

void CoopConnection::markRoomReady(bool ready) {
  if (room_) {
    room_->markReady(ready);
  }
}

void CoopConnection::markRoomPlaying() {
  if (room_) {
    room_->markPlaying();
  }
  // The game started: no more guests come in, and the link that waited for one goes.
  seatsOpen_ = false;
  std::erase_if(seats_, [](const Seat& seat) {
    return seat.link->phase() == game::coop::LinkPhase::Connecting;
  });
}

void CoopConnection::pollRoom() {
  if (room_) {
    room_->poll();
  }
  for (net::RoomSession& closing : closing_) {
    closing.poll();
  }
  std::erase_if(closing_, [](const net::RoomSession& room) { return !room.noticesPending(); });
}

void CoopConnection::close() {
  closeRoom({});
  if (link_ && view_ == View::Connected)
    link_->leave();
  for (Seat& seat : seats_) {
    if (seat.link->phase() == game::coop::LinkPhase::Ready) {
      seat.link->leave();
    }
  }
  seats_.clear();
  seatsOpen_ = false;
  guestEverIn_ = false;
  announced_ = 0;
  roomCode_.clear();
  roomToken_.clear();
  link_.reset();
  ++generation_;
  inPlay_.clear();
  peer_.reset();
  typed_.clear();
  error_.clear();
  view_ = View::Closed;
}

int CoopConnection::hostSeats() const {
  if (!desc_.seat) {
    return 2;
  }
  return std::clamp(desc_.seats, 2, static_cast<int>(game::coop::MaxPlayers));
}

const CoopConnection::Seat* CoopConnection::primarySeat() const {
  const Seat* first = nullptr;
  for (const Seat& seat : seats_) {
    if (seat.link->phase() == game::coop::LinkPhase::Ready &&
        (first == nullptr || seat.link->guestId() < first->link->guestId())) {
      first = &seat;
    }
  }
  if (first != nullptr) {
    return first;
  }
  if (const Seat* waiting = waitingSeat(); waiting != nullptr) {
    return waiting;
  }
  return seats_.empty() ? nullptr : &seats_.back();
}

game::coop::CoopLink* CoopConnection::link() {
  if (!hosting_) {
    return link_.get();
  }
  const Seat* seat = primarySeat();
  return seat != nullptr ? seat->link.get() : nullptr;
}

const game::coop::CoopLink* CoopConnection::link() const {
  if (!hosting_) {
    return link_.get();
  }
  const Seat* seat = primarySeat();
  return seat != nullptr ? seat->link.get() : nullptr;
}

std::vector<CoopConnection::Guest> CoopConnection::guests() const {
  std::vector<Guest> out;
  for (const Seat& seat : seats_) {
    if (seat.link->phase() == game::coop::LinkPhase::Ready) {
      out.push_back(
          {.id = seat.link->guestId(), .link = seat.link.get(), .generation = seat.generation});
    }
  }
  std::ranges::sort(out, {}, &Guest::id);
  return out;
}

std::vector<game::coop::PlayerId> CoopConnection::arriving() const {
  std::vector<game::coop::PlayerId> out;
  for (const Seat& seat : seats_) {
    if (seat.link->phase() == game::coop::LinkPhase::Connecting && seat.peer->peerFound()) {
      out.push_back(seat.link->guestId());
    }
  }
  std::ranges::sort(out);
  return out;
}

int CoopConnection::readyGuests() const {
  return static_cast<int>(std::ranges::count_if(
      seats_, [](const Seat& seat) { return seat.link->phase() == game::coop::LinkPhase::Ready; }));
}

void CoopConnection::handLinkToPlay() {
  if (!hosting_) {
    if (link_ != nullptr) {
      inPlay_.insert(generation_);
    }
    return;
  }
  for (const Guest& guest : guests()) {
    inPlay_.insert(guest.generation);
  }
}

bool CoopConnection::linkInPlay() const {
  if (!hosting_) {
    return link_ != nullptr && inPlay_.contains(generation_);
  }
  const auto in = guests();
  return !in.empty() && inPlay_.contains(in.front().generation);
}

std::optional<game::coop::PlayerId> CoopConnection::freeGuestId(const Seat* except) const {
  for (game::coop::PlayerId id = game::coop::GuestPlayerId;
       id < static_cast<game::coop::PlayerId>(hostSeats()); ++id) {
    const bool taken = std::ranges::any_of(
        seats_, [&](const Seat& seat) { return &seat != except && seat.link->guestId() == id; });
    if (!taken) {
      return id;
    }
  }
  return std::nullopt;
}

const CoopConnection::Seat* CoopConnection::waitingSeat() const {
  for (const Seat& seat : seats_) {
    if (seat.link->phase() == game::coop::LinkPhase::Connecting && !seat.peer->peerFound() &&
        seat.peer->state() != net::PeerState::Failed) {
      return &seat;
    }
  }
  return nullptr;
}

void CoopConnection::openSeat() {
  if (!seatsOpen_ || hostSeats() <= 2 || waitingSeat() != nullptr || roomCode_.empty() ||
      roomToken_.empty() || static_cast<int>(seats_.size()) >= hostSeats() - 1) {
    return;
  }
  const int taken = readyGuests();
  auto peer = desc_.seat(roomCode_, roomToken_, taken);
  if (!peer || peer->state() == net::PeerState::Failed) {
    // The server has no next seat for this room (gone, refused): the room keeps the guests it has.
    core::logWarning("coop: the room's next seat could not open; it takes no more guests");
    seatsOpen_ = false;
    return;
  }
  const game::coop::PlayerId id = freeGuestId(nullptr).value_or(game::coop::GuestPlayerId);
  auto link = std::make_unique<game::coop::CoopLink>(game::coop::CoopLink::host(
      *peer, desc_.identity, playerName_, game::coop::LinkTimeouts{}, id));
  seats_.push_back({.peer = std::move(peer), .link = std::move(link), .generation = 0});
  seats_.back().generation = nextGeneration();
  announced_ = taken;
}

bool CoopConnection::host(bool listed) {
  if (!desc_.configurationError.empty()) {
    view_ = View::Error;
    error_ = desc_.configurationError;
    return false;
  }
  if (!desc_.host) {
    view_ = View::Error;
    error_ = "no_host_transport";
    return false;
  }
  link_.reset();
  ++generation_;
  peer_.reset();
  seats_.clear();
  roomCode_.clear();
  roomToken_.clear();
  guestEverIn_ = false;
  announced_ = 0;
  hosting_ = true;
  seatsOpen_ = true;
  error_.clear();
  std::optional<net::PublicHost> listing;
  if (listed) {
    listing = net::PublicHost{.name = net::directoryName(playerName_),
                              .icon = playerIcon_,
                              .compat = desc_.compat,
                              .mode = "campaign"};
  }
  beginRoom({.hosting = true, .code = {}, .server = desc_.signalServer, .open = desc_.open});
  auto peer = desc_.host(listing);
  if (!peer) {
    view_ = View::Error;
    error_ = "transport_unavailable";
    closeRoom(error_);
    return false;
  }
  if (peer->state() == net::PeerState::Failed) {
    view_ = View::Error;
    error_ = std::string{peerFailureName(peer->failure())};
    room_->observe(*peer);
    return false;
  }
  auto link = std::make_unique<game::coop::CoopLink>(
      game::coop::CoopLink::host(*peer, desc_.identity, playerName_));
  seats_.push_back({.peer = std::move(peer), .link = std::move(link), .generation = 0});
  seats_.back().generation = nextGeneration();
  view_ = View::Hosting;
  return true;
}

void CoopConnection::beginJoin() {
  closeRoom({});
  peer_.reset();
  link_.reset();
  seats_.clear();
  hosting_ = false;
  seatsOpen_ = false;
  ++generation_;
  error_.clear();
  typed_.clear();
  view_ = View::JoinEntry;
}

bool CoopConnection::join(std::string_view code) {
  const auto normalized = net::normalizedRoomCode(code);
  if (!normalized) {
    error_ = "bad_code";
    return false;
  }
  if (!desc_.join) {
    view_ = View::Error;
    error_ = "no_join_transport";
    return false;
  }
  if (!desc_.configurationError.empty()) {
    view_ = View::Error;
    error_ = desc_.configurationError;
    return false;
  }
  hosting_ = false;
  seats_.clear();
  beginRoom({.hosting = false, .code = *normalized, .server = {}, .open = {}});
  peer_ = desc_.join(*normalized);
  if (!peer_) {
    view_ = View::Error;
    error_ = "transport_unavailable";
    closeRoom(error_);
    return false;
  }
  if (peer_->state() == net::PeerState::Failed) {
    view_ = View::Error;
    error_ = std::string{peerFailureName(peer_->failure())};
    room_->observe(*peer_);
    return false;
  }
  link_ = std::make_unique<game::coop::CoopLink>(
      game::coop::CoopLink::guest(*peer_, desc_.identity, playerName_));
  ++generation_;
  view_ = View::Joining;
  return true;
}

bool CoopConnection::submitJoin() {
  return join(typed_);
}

void CoopConnection::type(std::string_view text, bool enter) {
  if (view_ != View::JoinEntry)
    return;
  for (const char value : text) {
    if (codeCharacter(value) && typed_.size() < net::RoomCodeLength) {
      typed_.push_back(value);
      error_.clear();
    }
  }
  if (enter)
    (void)join(typed_);
}

void CoopConnection::fail(std::string error) {
  error_ = std::move(error);
  view_ = View::Error;
  closeRoom(error_);
}

void CoopConnection::tickHost(net::Millis now) {
  for (Seat& seat : seats_) {
    if (seat.link->phase() == game::coop::LinkPhase::Connecting) {
      // The waiting link welcomes its guest with the lowest id free at that moment.
      if (const auto id = freeGuestId(&seat)) {
        seat.link->assignGuest(*id);
      }
    }
    seat.link->poll(now);
    if (roomCode_.empty() && !seat.peer->roomCode().empty()) {
      roomCode_ = seat.peer->roomCode();
    }
    if (roomToken_.empty() && !seat.peer->roomToken().empty()) {
      roomToken_ = seat.peer->roomToken();
    }
  }
  // Until a guest is in, the room follows its first link (its code, its failure); a room of more
  // seats stays open once one was in, whoever leaves.
  if (room_ && !seats_.empty() && (!guestEverIn_ || hostSeats() <= 2)) {
    room_->observe(*seats_.front().peer);
  }
  const auto ended = [](const Seat& seat) {
    const auto phase = seat.link->phase();
    return phase == game::coop::LinkPhase::Rejected || phase == game::coop::LinkPhase::Failed ||
           phase == game::coop::LinkPhase::Left;
  };
  const auto reasonOf = [](const Seat& seat) {
    std::string error = std::string{peerFailureName(seat.peer->failure())};
    if (error.empty())
      error = std::string{linkFailureName(seat.link->failure())};
    if (error.empty() && seat.link->phase() == game::coop::LinkPhase::Rejected)
      error = "rejected";
    return error;
  };
  std::optional<std::string> lastReason;
  for (auto seat = seats_.begin(); seat != seats_.end();) {
    if (!ended(*seat)) {
      ++seat;
      continue;
    }
    const bool lastLink = seats_.size() == 1 && !guestEverIn_;
    if (hostSeats() <= 2 || lastLink) {
      // The room's only link ended (a two-seat room, or nobody ever came in): the room closes
      // with its reason, as it always did.
      fail(reasonOf(*seat));
      return;
    }
    core::logInfo("coop: player {} {} the room", seat->link->guestId(),
                  seat->link->remotePlayer() ? "left" : "did not get into");
    lastReason = reasonOf(*seat);
    inPlay_.erase(seat->generation);
    seat = seats_.erase(seat);
  }
  // A started game whose last guest went ends its room as a two-seat room did: no one is left to
  // play with and no one can come in.
  if (lastReason && !seatsOpen_ && seats_.empty()) {
    fail(*lastReason);
    return;
  }
  if (readyGuests() > 0) {
    guestEverIn_ = true;
    view_ = View::Connected;
  } else if (view_ == View::Connected) {
    // Everyone left a room of more seats: the host waits for the next guest.
    view_ = View::Hosting;
  }
  openSeat();
  // The server offers a seat that freed again: the waiting link says how many guests are in.
  if (const int taken = readyGuests(); taken != announced_) {
    if (const Seat* waiting = waitingSeat()) {
      waiting->peer->announceSeats(taken);
      announced_ = taken;
    }
  }
}

void CoopConnection::tick(net::Millis now) {
  pollRoom();
  if (view_ == View::Error)
    return;
  if (hosting_) {
    if (!seats_.empty())
      tickHost(now);
    return;
  }
  if (link_ == nullptr)
    return;
  link_->poll(now);
  if (room_ && peer_ != nullptr) {
    room_->observe(*peer_);
  }
  if (link_->phase() == game::coop::LinkPhase::Ready) {
    view_ = View::Connected;
  } else if (link_->phase() == game::coop::LinkPhase::Rejected ||
             link_->phase() == game::coop::LinkPhase::Failed ||
             link_->phase() == game::coop::LinkPhase::Left) {
    error_ = peer_ != nullptr ? std::string{peerFailureName(peer_->failure())} : std::string{};
    if (error_.empty())
      error_ = std::string{linkFailureName(link_->failure())};
    if (error_.empty() && link_->phase() == game::coop::LinkPhase::Rejected) {
      error_ = "rejected";
    }
    view_ = View::Error;
    closeRoom(error_);
  }
}

std::string CoopConnection::code() const {
  if (hosting_) {
    if (!roomCode_.empty()) {
      return roomCode_;
    }
    return !seats_.empty() ? seats_.front().peer->roomCode() : std::string{};
  }
  if (peer_ != nullptr && !peer_->roomCode().empty()) {
    return peer_->roomCode();
  }
  // A guest's link has no code of its own: the room's is the one it joined.
  return room_ && peer_ != nullptr ? room_->code() : std::string{};
}

std::string_view coopConnectionViewName(CoopConnection::View view) {
  switch (view) {
  case CoopConnection::View::Closed:
    return "closed";
  case CoopConnection::View::Menu:
    return "menu";
  case CoopConnection::View::JoinEntry:
    return "join_entry";
  case CoopConnection::View::Hosting:
    return "hosting";
  case CoopConnection::View::Joining:
    return "joining";
  case CoopConnection::View::Connected:
    return "connected";
  case CoopConnection::View::Error:
    return "error";
  }
  return "error";
}

} // namespace dwe::app
