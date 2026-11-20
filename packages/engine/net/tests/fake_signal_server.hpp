#pragma once

#include <dwe/net/signal.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dwe::net::test {

// The signaling server's rooms (services/versus-signal) in one process, for the tests: a host
// gets the next code of `codes`, a guest joins by code, and until both say done each one's
// "signal" messages go to the other. A room of more than two seats takes its host's next seats
// (`/v1/host/<code>?token=`): each seat is a pair of its own here, keyed "<code>/<n>".
class FakeSignalServer {
public:
  explicit FakeSignalServer(std::vector<std::string> codes = {"K7Q2", "M4XP"})
      : codes_{std::move(codes)} {}

  // What libdatachannel's WebSocket would be: give it to RtcPeerLink as its SocketOpener.
  std::unique_ptr<SignalSocket> open(const std::string& url) {
    auto socket = std::make_unique<Socket>(*this);
    const std::string hostPath = "/v1/host";
    const std::string joinPath = "/v1/join/";
    const std::string seatPath = "/v1/host/";
    const std::string roomsPath = "/v1/rooms";
    const std::string path = url.substr(0, url.find('?'));
    opened.push_back(url);
    if (!reachable) {
      socket->state_ = SocketState::Failed;
    } else if (path.ends_with("/v1/info")) {
      if (!knowsInfo) {
        // A first-protocol server: no such route, the socket fails.
        socket->state_ = SocketState::Failed;
      } else {
        nlohmann::json features = {"public-rooms", "room-state"};
        if (!relays.empty()) {
          features.push_back("relay");
        }
        socket->answer({{"protocol", "v1"}, {"features", features}});
      }
    } else if (path.ends_with("/state") && path.contains("/v1/rooms/")) {
      notice(*socket, url);
    } else if (path.ends_with(roomsPath)) {
      if (refuseList) {
        socket->answer({{"type", "error"}, {"reason", "limit"}});
        return socket;
      }
      // One snapshot of the listed rooms not playing, then the server closes.
      const std::string compat = query(url, "compat");
      nlohmann::json rooms = nlohmann::json::array();
      for (const auto& [code, room] : rooms_) {
        if (room.listed && room.phase != "playing" && (compat.empty() || room.compat == compat)) {
          rooms.push_back({{"roomId", code},
                           {"joinCode", code},
                           {"name", "Cooperative room"},
                           {"message", "Waiting for players"},
                           {"players", room.phase == "open" ? 1 : 2},
                           {"capacity", 2},
                           {"createdAt", 0},
                           {"state", room.phase == "open" ? "waiting" : "full"},
                           {"phase", room.phase},
                           {"hostName", room.hostName},
                           {"icon", "egg"},
                           {"compat", room.compat},
                           {"mode", "campaign"}});
        }
      }
      socket->answer({{"rooms", rooms}});
    } else if (const auto seatAt = path.find(seatPath); seatAt != std::string::npos) {
      const std::string code = path.substr(seatAt + seatPath.size());
      const auto seated = seated_.find(code);
      if (seated == seated_.end()) {
        socket->push({{"type", "error"}, {"reason", "no-room"}});
        socket->state_ = SocketState::Closed;
      } else if (query(url, "token") != seated->second.token) {
        socket->push({{"type", "error"}, {"reason", "bad-token"}});
        socket->state_ = SocketState::Closed;
      } else {
        seatsTaken.push_back(std::stoi(query(url, "taken")));
        const std::string key = code + "/" + std::to_string(++seated->second.opened);
        Room seat;
        seat.host = socket.get();
        rooms_[key] = seat;
        socket->room_ = key;
        nlohmann::json made = {{"type", "room"}, {"code", code}, {"token", seated->second.token}};
        withRelays(made);
        socket->push(made);
      }
    } else if (path.ends_with(hostPath)) {
      if (refuseHost) {
        socket->answer({{"type", "error"}, {"reason", "limit"}});
        return socket;
      }
      const std::string code = codes_.at(next_++ % codes_.size());
      const bool listed = query(url, "public") == "1";
      const int seats = query(url, "seats").empty() ? 2 : std::stoi(query(url, "seats"));
      const bool tokened = listed || seats > 2;
      if (seats > 2) {
        seated_[code] = Seated{.token = "token-" + code};
      }
      rooms_[code] = Room{.host = socket.get(),
                          .listed = listed,
                          .token = tokened ? "token-" + code : std::string{},
                          .hostName = query(url, "name"),
                          .compat = query(url, "compat")};
      socket->room_ = code;
      nlohmann::json made = {{"type", "room"}, {"code", code}};
      if (tokened) {
        made["token"] = rooms_[code].token;
      }
      withRelays(made);
      socket->push(made);
    } else if (const auto at = url.find(joinPath); at != std::string::npos) {
      const std::string code = url.substr(at + joinPath.size());
      auto room = rooms_.find(code);
      if (seated_.contains(code)) {
        // A seated room: the first of its host's sockets that waits for a guest.
        room = std::ranges::find_if(rooms_, [&](const auto& entry) {
          return (entry.first == code || entry.first.starts_with(code + "/")) &&
                 entry.second.host != nullptr && entry.second.guest == nullptr;
        });
        if (room == rooms_.end()) {
          socket->push({{"type", "error"}, {"reason", "full"}});
          socket->state_ = SocketState::Closed;
          return socket;
        }
      }
      if (room == rooms_.end() || room->second.host == nullptr) {
        socket->push({{"type", "error"}, {"reason", "no-room"}});
        socket->state_ = SocketState::Closed;
      } else if (room->second.guest != nullptr) {
        socket->push({{"type", "error"}, {"reason", "full"}});
        socket->state_ = SocketState::Closed;
      } else {
        room->second.guest = socket.get();
        room->second.phase = "connected";
        socket->room_ = room->first;
        nlohmann::json joined = {{"type", "joined"}};
        withRelays(joined);
        socket->push(joined);
        room->second.host->push({{"type", "peer"}});
      }
    }
    return socket;
  }

  // A listed room's phase as the directory has it ("" when there is none).
  [[nodiscard]] std::string phaseOf(const std::string& code) const {
    const auto room = rooms_.find(code);
    return room != rooms_.end() && room->second.listed ? room->second.phase : std::string{};
  }

  // Whether the server answers at all.
  bool reachable = true;
  // A first-protocol server, without /v1/info.
  bool knowsInfo = true;
  // The limits: the next host is refused, the list is refused.
  bool refuseHost = false;
  bool refuseList = false;
  // The relay servers handed out with room and joined (none: no TURN).
  nlohmann::json relays = nlohmann::json::array();
  // Rooms both peers said done to.
  std::size_t finishedRooms = 0;
  // Every URL a socket was opened to, in order.
  std::vector<std::string> opened;
  // The host notices the directory took, as "<code> <state>".
  std::vector<std::string> notices;
  // The guests a seated room's host said it holds: with each next seat, then by message.
  std::vector<int> seatsTaken;

private:
  class Socket;
  struct Room {
    Socket* host = nullptr;
    Socket* guest = nullptr;
    bool listed = false;
    std::string token;
    std::string hostName;
    std::string compat;
    // The directory's phase: open, connected, ready, playing.
    std::string phase = "open";
    std::size_t done = 0;
  };

  // A query parameter of a URL, percent-decoded ("" when it is not there).
  static std::string query(const std::string& url, const std::string& name) {
    const auto start = url.find('?');
    if (start == std::string::npos) {
      return {};
    }
    std::string rest = url.substr(start + 1);
    while (!rest.empty()) {
      const auto end = rest.find('&');
      const std::string pair = rest.substr(0, end);
      if (pair.starts_with(name + "=")) {
        return decoded(pair.substr(name.size() + 1));
      }
      rest = end == std::string::npos ? std::string{} : rest.substr(end + 1);
    }
    return {};
  }

  static std::string decoded(const std::string& text) {
    std::string out;
    const auto hex = [](char digit) {
      return digit <= '9' ? digit - '0' : (digit | 0x20) - 'a' + 10;
    };
    for (std::size_t at = 0; at < text.size(); ++at) {
      if (text[at] == '%' && at + 2 < text.size()) {
        out.push_back(static_cast<char>(hex(text[at + 1]) * 16 + hex(text[at + 2])));
        at += 2;
      } else {
        out.push_back(text[at]);
      }
    }
    return out;
  }

  void withRelays(nlohmann::json& message) const {
    if (!relays.empty()) {
      message["iceServers"] = relays;
    }
  }

  // A host's state notice (`/v1/rooms/<code>/state?token=&state=`), as the directory answers it.
  void notice(Socket& socket, const std::string& url) {
    const std::string path = url.substr(0, url.find('?'));
    const std::string prefix = "/v1/rooms/";
    const auto at = path.find(prefix) + prefix.size();
    const std::string code = path.substr(at, path.size() - at - std::string{"/state"}.size());
    const std::string token = query(url, "token");
    const std::string state = query(url, "state");
    const auto room = rooms_.find(code);
    if (state != "ready" && state != "playing" && state != "closed") {
      socket.answer({{"type", "error"}, {"reason", "bad-state"}});
    } else if (room == rooms_.end() || !room->second.listed) {
      socket.answer({{"type", "error"}, {"reason", "no-room"}});
    } else if (token != room->second.token) {
      socket.answer({{"type", "error"}, {"reason", "bad-token"}});
    } else {
      notices.push_back(code + " " + state);
      if (state == "closed") {
        rooms_.erase(room);
      } else {
        room->second.phase = state;
      }
      socket.answer({{"type", "state"}});
    }
  }

  class Socket final : public SignalSocket {
  public:
    explicit Socket(FakeSignalServer& server) : server_{&server} {}
    ~Socket() override { close(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&&) = delete;
    Socket& operator=(Socket&&) = delete;

    void send(std::string text) override {
      const auto room = server_->rooms_.find(room_);
      if (state_ != SocketState::Open || room == server_->rooms_.end()) {
        return;
      }
      const nlohmann::json message = nlohmann::json::parse(text, nullptr, false);
      if (message.value("type", "") == "signal") {
        Socket* other = room->second.host == this ? room->second.guest : room->second.host;
        if (other != nullptr) {
          other->push(message);
        }
      } else if (message.value("type", "") == "seats") {
        server_->seatsTaken.push_back(message.value("taken", -1));
      } else if (message.value("type", "") == "done" && ++room->second.done == 2) {
        ++server_->finishedRooms;
        room->second.host->state_ = SocketState::Closed;
        room->second.guest->state_ = SocketState::Closed;
        room->second.host->room_.clear();
        room->second.guest->room_.clear();
        // A listed room stays in the directory (connected): its host tells how it goes.
        if (room->second.listed) {
          room->second.host = nullptr;
          room->second.guest = nullptr;
        } else {
          server_->rooms_.erase(room);
        }
      }
    }

    std::optional<std::string> receive() override {
      if (texts_.empty()) {
        return std::nullopt;
      }
      std::string text = std::move(texts_.front());
      texts_.pop_front();
      return text;
    }

    [[nodiscard]] SocketState state() const override { return state_; }

    void close() override {
      const auto room = server_->rooms_.find(room_);
      if (room != server_->rooms_.end()) {
        Socket* other = room->second.host == this ? room->second.guest : room->second.host;
        if (other != nullptr) {
          other->push({{"type", "error"}, {"reason", "left"}});
          other->room_.clear();
        }
        server_->rooms_.erase(room);
      }
      room_.clear();
      if (state_ == SocketState::Open || state_ == SocketState::Connecting) {
        state_ = SocketState::Closed;
      }
    }

    void push(const nlohmann::json& message) { texts_.push_back(message.dump()); }
    // One message, then the server closes (the list, /v1/info, a state notice).
    void answer(const nlohmann::json& message) {
      push(message);
      state_ = SocketState::Closed;
    }

  private:
    friend class FakeSignalServer;
    FakeSignalServer* server_;
    std::string room_;
    SocketState state_ = SocketState::Open;
    std::deque<std::string> texts_;
  };

  struct Seated {
    std::string token;
    int opened = 0;
  };

  std::vector<std::string> codes_;
  std::size_t next_ = 0;
  std::map<std::string, Seated> seated_;
  std::map<std::string, Room> rooms_;
};

} // namespace dwe::net::test
