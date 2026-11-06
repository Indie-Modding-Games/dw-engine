#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::app {

// Where the control channel listens (--control <where>): a Unix socket's path, or a TCP port on
// the loopback for systems without Unix sockets (Windows) and tools that prefer TCP.
struct ControlEndpoint {
  // The Unix socket; none for TCP.
  std::optional<std::filesystem::path> unixPath;
  // The TCP port on 127.0.0.1, without a Unix path; 0 lets the system pick a free one.
  std::uint16_t tcpPort = 0;
};

// Reads --control's value: `tcp:<port>`, `tcp:127.0.0.1:<port>` or `tcp:localhost:<port>` listen on
// the loopback; any other host is refused, since the channel drives the game without asking. Any
// other value is a Unix socket's path.
core::Result<ControlEndpoint> parseControlEndpoint(std::string_view text);

// A line a client sent, and the client: a serial number from 1, never given to another client.
struct ControlLine {
  std::uint64_t client = 0;
  std::string text;
};

// The control channel's socket (--control, remote_control.hpp): it takes up to MaxClients clients
// at once (a test driver and a map editor, say), each read and written without ever blocking the
// game, a JSON line per message. A Unix socket where the system has them; a TCP socket bound to the
// loopback everywhere, whose clients must come from the loopback too.
class ControlSocket {
public:
  // The clients connected at once; one more is told so and closed.
  static constexpr std::size_t MaxClients = 4;

  // Listens where `where` says (parseControlEndpoint). A Unix socket replaces one left at its path
  // by a game that ended without removing it.
  static core::Result<std::unique_ptr<ControlSocket>> open(std::string_view where);
  static core::Result<std::unique_ptr<ControlSocket>> open(const ControlEndpoint& endpoint);

  ControlSocket(const ControlSocket&) = delete;
  ControlSocket& operator=(const ControlSocket&) = delete;
  ControlSocket(ControlSocket&&) = delete;
  ControlSocket& operator=(ControlSocket&&) = delete;
  // Closes the clients and the socket and removes a Unix socket's file.
  ~ControlSocket();

  // The TCP port listened on (the system's pick for port 0); 0 for a Unix socket.
  [[nodiscard]] std::uint16_t port() const { return port_; }

  // The whole lines the clients sent since the last call, client by client in the order read.
  // Takes the clients waiting to connect first, and sends what each client's queue still holds.
  std::vector<ControlLine> receive();

  // Queues a line for a client and sends what the socket takes now; the rest goes with the next
  // calls. Dropped when that client is gone.
  void send(std::uint64_t client, std::string_view line);

  // The clients that left (or were dropped) since the last call.
  [[nodiscard]] std::vector<std::uint64_t> takeGone() { return std::exchange(gone_, {}); }

  // The clients connected now.
  [[nodiscard]] std::size_t clients() const { return clients_.size(); }

private:
  struct Client {
    std::uint64_t id = 0;
    // The system's socket handle as an integer.
    std::intptr_t socket = -1;
    std::string received;
    std::string unsent;
  };

  // `listener` is the system's socket handle as an integer, -1 for none.
  ControlSocket(std::intptr_t listener, std::optional<std::filesystem::path> unixPath,
                std::uint16_t port);
  // Takes every waiting client: up to MaxClients, the others refused.
  void accept();
  // Sends what the client's queue holds and the socket takes; false when the client is broken.
  static bool flush(Client& client);
  // Reads what the client sent into its lines; false when it left or sent too long a line.
  static bool read(Client& client, std::vector<ControlLine>& lines);
  void drop(Client& client);

  // Unused where the build has no sockets.
  [[maybe_unused]] std::intptr_t listener_ = -1;
  std::optional<std::filesystem::path> unixPath_;
  std::uint16_t port_ = 0;
  std::vector<Client> clients_;
  std::uint64_t nextClient_ = 1;
  std::vector<std::uint64_t> gone_;
};

} // namespace dwe::app
