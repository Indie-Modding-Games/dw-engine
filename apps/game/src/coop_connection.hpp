#pragma once

#include <dwe/game/coop/link.hpp>
#include <dwe/net/room_session.hpp>
#include <dwe/net/signal.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::app {

struct CoopConnectionDesc {
  game::coop::Identity identity;
  std::string configurationError;
  // Makes a room; a listed one goes in the signaling server's public list, with what its host
  // tells about it (none for a private room).
  std::function<std::unique_ptr<net::PeerLink>(const std::optional<net::PublicHost>& listing)> host;
  std::function<std::unique_ptr<net::PeerLink>(std::string_view)> join;
  // The host's next seat in its room of more than two seats (net::RtcPeerLink::hostSeat): the
  // room's code and token, and how many guests the host holds now. None: rooms seat two.
  std::function<std::unique_ptr<net::PeerLink>(std::string_view code, std::string_view token,
                                               int taken)>
      seat;
  // How many players a hosted room seats, host included (2 to game::coop::MaxPlayers); more than
  // two takes `seat`.
  int seats = 2;
  // The directory's compatibility key of these rooms (net::compatKey), so that only games that
  // can play together list each other's rooms.
  std::string compat;
  // The signaling server and its sockets, for a public room host's state notices
  // (net::RoomSession); none are sent without them.
  std::string signalServer;
  net::SocketOpener open;
};

// How long a guest that dropped out of a game under way keeps its seat: coming back within it, it
// gets the same player id (its seat key says it is the same player, #356).
inline constexpr net::Millis CoopRejoinGrace{5 * 60 * 1000};
// How long a link that said goodbye keeps its transport, so its Leave reaches the other side
// before the route closes.
inline constexpr net::Millis CoopLeaveFlush{1000};

// App-level host/join handshake. It owns the transports and CoopLinks for as long as the room is
// open, and keeps the room code and failure state available to the UI. The room's phase is a
// net::RoomSession: it follows the link, the lobby says when everyone is ready and when play
// starts, and a public room's host tells the directory.
//
// A guest has one link, to the host. The host has one link per guest (#352): in a room of more
// than two seats it keeps one more link waiting for the next guest while the room is in its lobby,
// gives each guest the lowest free player id, and a guest that leaves frees its seat without
// closing the room for the others.
//
// A room of more seats goes on taking players once its game started (#356): the host keeps a link
// waiting while it plays, a late joiner comes in through it as in the lobby, and a guest that
// dropped comes back to its own seat and player id within CoopRejoinGrace. The host may close the
// game to new players (openToNewPlayers); a returning guest still gets in.
class CoopConnection {
public:
  enum class View { Closed, Menu, JoinEntry, Hosting, Joining, Connected, Error };

  // One of the host's guests: its player id, its link and that link's generation (a new link for
  // the same id is another generation).
  struct Guest {
    game::coop::PlayerId id = game::coop::GuestPlayerId;
    game::coop::CoopLink* link = nullptr;
    std::uint64_t generation = 0;
  };

  explicit CoopConnection(CoopConnectionDesc desc) : desc_{std::move(desc)} {}
  CoopConnection(const CoopConnection&) = delete;
  CoopConnection& operator=(const CoopConnection&) = delete;
  CoopConnection(CoopConnection&&) = delete;
  CoopConnection& operator=(CoopConnection&&) = delete;
  // A game that ends without closing its room still says goodbye to everyone in it.
  ~CoopConnection();
  void open();
  void close();
  [[nodiscard]] bool host(bool listed = false);
  void beginJoin();
  bool submitJoin();
  // Joins a room by its code at once, without the typed entry (a room picked from the list, or a
  // code typed on the rooms screen).
  bool join(std::string_view code);
  void type(std::string_view text, bool enter);
  void tick(net::Millis now);

  [[nodiscard]] View view() const { return view_; }
  [[nodiscard]] bool takesText() const { return view_ == View::JoinEntry; }
  [[nodiscard]] bool connected() const { return view_ == View::Connected; }
  [[nodiscard]] std::string code() const;
  [[nodiscard]] const std::string& typed() const { return typed_; }
  [[nodiscard]] std::string_view error() const { return error_; }
  // The guest's link to the host; on the host, its first guest's (the lowest id), else the link
  // waiting for one.
  [[nodiscard]] game::coop::CoopLink* link();
  [[nodiscard]] const game::coop::CoopLink* link() const;
  [[nodiscard]] bool isHost() const { return hosting_; }
  // On the host: its guests in, by id (the waiting link and guests still saying hello are not).
  [[nodiscard]] std::vector<Guest> guests() const;
  // On the host: the ids of guests on their way in (their link found the room, no hello yet).
  [[nodiscard]] std::vector<game::coop::PlayerId> arriving() const;
  // How many players this room seats, host included (2 for a guest until the host's lobby says).
  [[nodiscard]] int seats() const { return hosting_ ? hostSeats() : 2; }
  // This player's name (its online profile): the guest gives it to the host when it joins, the
  // host to the guest when it lets it in, and a public room is listed with it.
  void setPlayerName(std::string name) { playerName_ = std::move(name); }
  [[nodiscard]] const std::string& playerName() const { return playerName_; }
  // The profile icon a public room is listed with.
  void setPlayerIcon(std::string icon) { playerIcon_ = std::move(icon); }
  // The directory's compatibility key of these rooms.
  [[nodiscard]] const std::string& compat() const { return desc_.compat; }
  // The room this player is in or last left (nullptr before the first one).
  [[nodiscard]] const net::RoomSession* room() const { return room_ ? &*room_ : nullptr; }
  // Everyone picked what they play (or one took it back), and the game started. A room of two
  // takes no more guests from then on; a room of more seats keeps one waiting (#356).
  void markRoomReady(bool ready);
  void markRoomPlaying();
  // On the host: the game is under way (markRoomPlaying).
  [[nodiscard]] bool playing() const { return playing_; }
  // On the host: whether a game under way takes new players (open, the default) or only the
  // guests coming back to their seat. The room's listing shows it.
  void setOpenToNewPlayers(bool open) { openToNewPlayers_ = open; }
  [[nodiscard]] bool openToNewPlayers() const { return openToNewPlayers_; }
  // On the host: the guests that dropped out of the game and keep their seat for now, by id.
  [[nodiscard]] std::vector<game::coop::PlayerId> held() const;
  // On a guest: this link came back to the seat it had in this room (its seat key went in the
  // hello). The host decides: the welcome's id says whether it got the same seat.
  [[nodiscard]] bool returning() const { return returning_; }
  // Moves the room's notices on, also once the lobby closed (tick() does it as well).
  void pollRoom();
  // pollRoom(), and moves the goodbyes of the links that left on until they went out.
  void settle(net::Millis now);
  // Nothing of a closed room is still on its way: its goodbyes and its notices to the directory.
  [[nodiscard]] bool settled() const;
  // The other player's name once it is in (the first guest's on the host, the host's on the
  // guest); empty otherwise.
  [[nodiscard]] std::string remoteName() const {
    const game::coop::CoopLink* other = link();
    return other != nullptr ? other->remoteName() : std::string{};
  }
  // Changes whenever the link is replaced or dropped, so whoever kept a pointer to the old one can
  // tell it is gone (a freed link's address may be reused by the next one).
  [[nodiscard]] std::uint64_t linkGeneration() const { return generation_; }
  // Each link has one inbox. Once the play session's gameplay bridge reads it, the lobby must not:
  // draining it too starved the bridge of snapshots and guest commands, so remote players drew as
  // bare shadows. A new link (another room, another guest) goes back to the lobby on its own.
  // Handing over hands every link in now (the guest's, or each of the host's guests).
  void handLinkToPlay();
  void takeLinkBack() { inPlay_.clear(); }
  // One link only: a guest's link that play reads from now on, or gives back (that guest left
  // the game while the others play on).
  void handLinkToPlay(std::uint64_t generation) { inPlay_.insert(generation); }
  void takeLinkBack(std::uint64_t generation) { inPlay_.erase(generation); }
  // The guest's link, or the host's first guest's.
  [[nodiscard]] bool linkInPlay() const;
  // Whether the play session reads the link of generation `generation`.
  [[nodiscard]] bool linkInPlay(std::uint64_t generation) const {
    return inPlay_.contains(generation);
  }

private:
  // One of the host's links: to a guest, or waiting for the next one.
  struct Seat {
    std::unique_ptr<net::PeerLink> peer;
    std::unique_ptr<game::coop::CoopLink> link;
    std::uint64_t generation = 0;
  };

  void refresh();
  [[nodiscard]] int hostSeats() const;
  void tickHost(net::Millis now);
  void fail(std::string error);
  [[nodiscard]] std::uint64_t nextGeneration() { return ++generation_; }
  // The lowest guest id no other seat holds (none when all are taken). `welcomed`: only the seats
  // whose guest is in count (a waiting link's id is only what it would give).
  [[nodiscard]] std::optional<game::coop::PlayerId> freeGuestId(const Seat* except,
                                                                bool welcomed = false) const;
  // The host's first guest's seat (the lowest id), else the one waiting for a guest.
  [[nodiscard]] const Seat* primarySeat() const;
  // The host's link still waiting for a guest, if any.
  [[nodiscard]] const Seat* waitingSeat() const;
  // Opens the next seat when the room takes more guests and none waits.
  void openSeat();
  // A guest's hello on one of the host's links: the seat it gets (#356).
  [[nodiscard]] game::coop::Admission admit(const game::coop::Hello& hello, const Seat& seat);
  [[nodiscard]] std::uint64_t newSeatKey();
  // Hands the host's link to admit().
  void watchHello(Seat& seat);
  [[nodiscard]] int readyGuests() const;
  // A new room replaces the last one; a closed one is kept until its notices reached the server.
  void beginRoom(net::RoomSessionDesc desc);
  void closeRoom(std::string reason);

  CoopConnectionDesc desc_;
  // The guest's link to the host.
  std::unique_ptr<net::PeerLink> peer_;
  std::unique_ptr<game::coop::CoopLink> link_;
  bool hosting_ = false;
  // The host's links: its guests, and the one waiting for the next (opened last).
  std::vector<Seat> seats_;
  // The room still takes guests (the lobby; not once playing, nor after a seat could not open).
  bool seatsOpen_ = false;
  // Any guest got in: from then a guest leaving no longer closes the room (a room of more seats).
  bool guestEverIn_ = false;
  net::SeatsNotice announced_;
  bool playing_ = false;
  bool openToNewPlayers_ = true;
  // A guest that dropped out of the game under way: its seat waits for it until `at` plus the
  // grace.
  struct Departed {
    game::coop::PlayerId id = game::coop::GuestPlayerId;
    std::uint64_t key = 0;
    net::Millis at{0};
  };
  std::vector<Departed> departed_;
  net::Millis now_{0};
  // A waiting seat that ended without a guest is opened again from then on (not every frame while
  // the server cannot be reached).
  net::Millis seatRetryAt_{0};
  std::uint64_t seatKeys_ = 0;
  // On a guest: the seat key the host gave it in the last room it was in, by code, and whether
  // the link now joining went back to it.
  struct Ticket {
    std::string code;
    std::uint64_t key = 0;
  };
  std::optional<Ticket> ticket_;
  bool returning_ = false;
  std::string roomCode_;
  std::string roomToken_;
  View view_ = View::Closed;
  std::string typed_;
  std::string error_;
  std::string playerName_ = "Player";
  std::string playerIcon_;
  std::optional<net::RoomSession> room_;
  std::vector<net::RoomSession> closing_;
  // Links that said goodbye, kept until `until` so their Leave goes out.
  struct Leaving {
    std::unique_ptr<net::PeerLink> peer;
    std::unique_ptr<game::coop::CoopLink> link;
    net::Millis until{0};
  };
  std::vector<Leaving> leaving_;
  std::uint64_t generation_ = 0;
  std::set<std::uint64_t> inPlay_;
};

[[nodiscard]] std::string_view coopConnectionViewName(CoopConnection::View view);

} // namespace dwe::app
