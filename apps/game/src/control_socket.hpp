#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

// The control channel's socket (--control, remote_control.hpp): it takes one client at a time,
// read and written without ever blocking the game, a JSON line per message. A Unix socket where
// the system has them; a TCP socket bound to the loopback everywhere, whose clients must come from
// the loopback too.
class ControlSocket {
public:
  // Listens where `where` says (parseControlEndpoint). A Unix socket replaces one left at its path
  // by a game that ended without removing it.
  static core::Result<std::unique_ptr<ControlSocket>> open(std::string_view where);
  static core::Result<std::unique_ptr<ControlSocket>> open(const ControlEndpoint& endpoint);

  ControlSocket(const ControlSocket&) = delete;
  ControlSocket& operator=(const ControlSocket&) = delete;
  ControlSocket(ControlSocket&&) = delete;
  ControlSocket& operator=(ControlSocket&&) = delete;
  // Closes the client and the socket and removes a Unix socket's file.
  ~ControlSocket();

  // The TCP port listened on (the system's pick for port 0); 0 for a Unix socket.
  [[nodiscard]] std::uint16_t port() const { return port_; }

  // The whole lines the client sent since the last call. Takes a client when none is connected.
  std::vector<std::string> receive();

  // Queues a line for the client and sends what the socket takes now; the rest goes with the next
  // calls. Dropped when no client is connected.
  void send(std::string_view line);

private:
  // `listener` is the system's socket handle as an integer, -1 for none.
  ControlSocket(std::intptr_t listener, std::optional<std::filesystem::path> unixPath,
                std::uint16_t port);
  // Takes a waiting client, if any; false when none is connected afterwards.
  bool accept();
  void flush();
  void dropClient();

  // Unused where the build has no sockets.
  [[maybe_unused]] std::intptr_t listener_ = -1;
  [[maybe_unused]] std::intptr_t client_ = -1;
  std::optional<std::filesystem::path> unixPath_;
  std::uint16_t port_ = 0;
  std::string received_;
  std::string unsent_;
};

} // namespace dwe::app
