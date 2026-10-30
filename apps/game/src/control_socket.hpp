#pragma once

#include <dwe/core/error.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

// The control channel's socket (--control <path>, remote_control.hpp): a Unix socket that takes one
// client at a time, read and written without ever blocking the game, a JSON line per message. Only
// on systems with Unix sockets; elsewhere open says so.
class ControlSocket {
public:
  // Listens at `path`, replacing a socket left there by a game that ended without removing it.
  static core::Result<std::unique_ptr<ControlSocket>> open(const std::filesystem::path& path);

  ControlSocket(const ControlSocket&) = delete;
  ControlSocket& operator=(const ControlSocket&) = delete;
  ControlSocket(ControlSocket&&) = delete;
  ControlSocket& operator=(ControlSocket&&) = delete;
  // Closes the client and the socket and removes the socket's file.
  ~ControlSocket();

  // The whole lines the client sent since the last call. Takes a client when none is connected.
  std::vector<std::string> receive();

  // Queues a line for the client and sends what the socket takes now; the rest goes with the next
  // calls. Dropped when no client is connected.
  void send(std::string_view line);

private:
  ControlSocket(int listener, std::filesystem::path path);
  void flush();
  void dropClient();

  int listener_ = -1;
  int client_ = -1;
  std::filesystem::path path_;
  std::string received_;
  std::string unsent_;
};

} // namespace dwe::app
