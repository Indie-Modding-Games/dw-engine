#pragma once

#include <dwe/net/signal.hpp>

#include <nlohmann/json.hpp>

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dwe::net::test {

// The signaling server's rooms (services/versus-signal) in one process, for the tests: a host
// gets the next code of `codes`, a guest joins by code, and until both say done each one's
// "signal" messages go to the other.
class FakeSignalServer {
public:
  explicit FakeSignalServer(std::vector<std::string> codes = {"K7Q2", "M4XP"})
      : codes_{std::move(codes)} {}

  // What libdatachannel's WebSocket would be: give it to RtcPeerLink as its SocketOpener.
  std::unique_ptr<SignalSocket> open(const std::string& url) {
    auto socket = std::make_unique<Socket>(*this);
    const std::string hostPath = "/v1/host";
    const std::string joinPath = "/v1/join/";
    if (!reachable) {
      socket->state_ = SocketState::Failed;
    } else if (url.ends_with(hostPath)) {
      const std::string code = codes_.at(next_++ % codes_.size());
      rooms_[code] = Room{.host = socket.get()};
      socket->room_ = code;
      socket->push({{"type", "room"}, {"code", code}});
    } else if (const auto at = url.find(joinPath); at != std::string::npos) {
      const std::string code = url.substr(at + joinPath.size());
      const auto room = rooms_.find(code);
      if (room == rooms_.end()) {
        socket->push({{"type", "error"}, {"reason", "no-room"}});
        socket->state_ = SocketState::Closed;
      } else if (room->second.guest != nullptr) {
        socket->push({{"type", "error"}, {"reason", "full"}});
        socket->state_ = SocketState::Closed;
      } else {
        room->second.guest = socket.get();
        socket->room_ = code;
        socket->push({{"type", "joined"}});
        room->second.host->push({{"type", "peer"}});
      }
    }
    return socket;
  }

  // Whether the server answers at all.
  bool reachable = true;
  // Rooms both peers said done to.
  std::size_t finishedRooms = 0;

private:
  class Socket;
  struct Room {
    Socket* host = nullptr;
    Socket* guest = nullptr;
    std::size_t done = 0;
  };

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
      } else if (message.value("type", "") == "done" && ++room->second.done == 2) {
        ++server_->finishedRooms;
        room->second.host->state_ = SocketState::Closed;
        room->second.guest->state_ = SocketState::Closed;
        room->second.host->room_.clear();
        room->second.guest->room_.clear();
        server_->rooms_.erase(room);
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

  private:
    friend class FakeSignalServer;
    FakeSignalServer* server_;
    std::string room_;
    SocketState state_ = SocketState::Open;
    std::deque<std::string> texts_;
  };

  std::vector<std::string> codes_;
  std::size_t next_ = 0;
  std::map<std::string, Room> rooms_;
};

} // namespace dwe::net::test
