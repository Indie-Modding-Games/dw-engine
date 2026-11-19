#pragma once

#include <dwe/game/coop/link.hpp>
#include <dwe/net/room_session.hpp>
#include <dwe/net/signal.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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
  // The directory's compatibility key of these rooms (net::compatKey), so that only games that
  // can play together list each other's rooms.
  std::string compat;
  // The signaling server and its sockets, for a public room host's state notices
  // (net::RoomSession); none are sent without them.
  std::string signalServer;
  net::SocketOpener open;
};

// App-level host/join handshake. It owns the transport and CoopLink for as long as the lobby is
// open, and keeps the room code and failure state available to the UI. The room's phase is a
// net::RoomSession: it follows the link, the lobby says when both are ready and when play starts,
// and a public room's host tells the directory.
class CoopConnection {
public:
  enum class View { Closed, Menu, JoinEntry, Hosting, Joining, Connected, Error };

  explicit CoopConnection(CoopConnectionDesc desc) : desc_{std::move(desc)} {}
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
  [[nodiscard]] game::coop::CoopLink* link() { return link_.get(); }
  [[nodiscard]] const game::coop::CoopLink* link() const { return link_.get(); }
  // This player's name (its online profile): the guest gives it to the host when it joins, the
  // host to the guest when it lets it in, and a public room is listed with it.
  void setPlayerName(std::string name) { playerName_ = std::move(name); }
  // The profile icon a public room is listed with.
  void setPlayerIcon(std::string icon) { playerIcon_ = std::move(icon); }
  // The directory's compatibility key of these rooms.
  [[nodiscard]] const std::string& compat() const { return desc_.compat; }
  // The room this player is in or last left (nullptr before the first one).
  [[nodiscard]] const net::RoomSession* room() const { return room_ ? &*room_ : nullptr; }
  // Both players picked what they play (or one took it back), and the game started.
  void markRoomReady(bool ready);
  void markRoomPlaying();
  // Moves the room's notices on, also once the lobby closed (tick() does it as well).
  void pollRoom();
  // The other player's name once it is in (the guest's on the host, the host's on the guest);
  // empty otherwise.
  [[nodiscard]] std::string remoteName() const {
    return link_ != nullptr ? link_->remoteName() : std::string{};
  }
  // Changes whenever the link is replaced or dropped, so whoever kept a pointer to the old one can
  // tell it is gone (a freed link's address may be reused by the next one).
  [[nodiscard]] std::uint64_t linkGeneration() const { return generation_; }
  // The link has one inbox. Once the play session's gameplay bridge reads it, the lobby must not:
  // draining it too starved the bridge of snapshots and guest commands, so remote players drew as
  // bare shadows. A new link (another room) goes back to the lobby on its own.
  void handLinkToPlay() { playLink_ = generation_; }
  void takeLinkBack() { playLink_.reset(); }
  [[nodiscard]] bool linkInPlay() const { return link_ != nullptr && playLink_ == generation_; }

private:
  void refresh();
  // A new room replaces the last one; a closed one is kept until its notices reached the server.
  void beginRoom(net::RoomSessionDesc desc);
  void closeRoom(std::string reason);

  CoopConnectionDesc desc_;
  std::unique_ptr<net::PeerLink> peer_;
  std::unique_ptr<game::coop::CoopLink> link_;
  View view_ = View::Closed;
  std::string typed_;
  std::string error_;
  std::string playerName_ = "Player";
  std::string playerIcon_;
  std::optional<net::RoomSession> room_;
  std::vector<net::RoomSession> closing_;
  std::uint64_t generation_ = 0;
  std::optional<std::uint64_t> playLink_;
};

[[nodiscard]] std::string_view coopConnectionViewName(CoopConnection::View view);

} // namespace dwe::app
