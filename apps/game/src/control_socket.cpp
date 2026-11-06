#include "control_socket.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <format>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define DWE_CONTROL_SOCKETS 1
#elif defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define DWE_CONTROL_SOCKETS 1
#define DWE_CONTROL_UNIX_SOCKETS 1
#endif

namespace dwe::app {

namespace {

constexpr std::string_view TcpPrefix = "tcp:";
// The hosts a TCP endpoint may name: the loopback's.
constexpr std::array<std::string_view, 2> LoopbackHosts{"127.0.0.1", "localhost"};

} // namespace

core::Result<ControlEndpoint> parseControlEndpoint(std::string_view text) {
  if (text.empty()) {
    return core::fail("control: --control needs a socket path or tcp:<port>");
  }
  if (!text.starts_with(TcpPrefix)) {
    return ControlEndpoint{.unixPath = std::filesystem::path{text}, .tcpPort = 0};
  }
  std::string_view rest = text.substr(TcpPrefix.size());
  if (const std::size_t colon = rest.rfind(':'); colon != std::string_view::npos) {
    const std::string_view host = rest.substr(0, colon);
    if (std::ranges::find(LoopbackHosts, host) == LoopbackHosts.end()) {
      return core::fail(std::format("control: '{}': the channel only listens on the loopback "
                                    "(127.0.0.1 or localhost), never on '{}'",
                                    text, host));
    }
    rest = rest.substr(colon + 1);
  }
  std::uint16_t port = 0;
  const auto parsed = std::from_chars(rest.data(), rest.data() + rest.size(), port);
  if (rest.empty() || parsed.ec != std::errc{} || parsed.ptr != rest.data() + rest.size()) {
    return core::fail(
        std::format("control: '{}': expected tcp:<port> with a port from 0 to 65535", text));
  }
  return ControlEndpoint{.unixPath = std::nullopt, .tcpPort = port};
}

core::Result<std::unique_ptr<ControlSocket>> ControlSocket::open(std::string_view where) {
  auto endpoint = parseControlEndpoint(where);
  if (!endpoint) {
    return std::unexpected{endpoint.error()};
  }
  return open(*endpoint);
}

#if defined(DWE_CONTROL_SOCKETS)

namespace {

// A client that sends a longer line without a line break is dropped: no request is near it.
constexpr std::size_t MaxLineBytes = 1 << 20;

#if defined(_WIN32)

using NativeSocket = SOCKET;

std::string lastError() {
  return std::format("socket error {}", ::WSAGetLastError());
}

bool wouldBlock() {
  const int error = ::WSAGetLastError();
  return error == WSAEWOULDBLOCK || error == WSAEINTR;
}

void closeSocket(NativeSocket socket) {
  ::closesocket(socket);
}

bool setNonBlocking(NativeSocket socket) {
  u_long on = 1;
  return ::ioctlsocket(socket, FIONBIO, &on) == 0;
}

// Winsock starts once, the first time a socket is opened, and stays up until the game ends.
bool startSockets() {
  static const bool started = [] {
    WSADATA data{};
    return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return started;
}

#else

using NativeSocket = int;

std::string lastError() {
  return std::error_code{errno, std::generic_category()}.message();
}

bool wouldBlock() {
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

void closeSocket(NativeSocket socket) {
  ::close(socket);
}

bool setNonBlocking(NativeSocket socket) {
  const int flags = fcntl(socket, F_GETFL, 0);
  return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0 &&
         fcntl(socket, F_SETFD, FD_CLOEXEC) == 0;
}

bool startSockets() {
  return true;
}

#endif

// A write to a client that left fails instead of raising SIGPIPE: MSG_NOSIGNAL on Linux, the
// socket's SO_NOSIGPIPE on macOS, nothing to do on Windows.
#if defined(MSG_NOSIGNAL)
constexpr int SendFlags = MSG_NOSIGNAL;
#else
constexpr int SendFlags = 0;
#endif

void keepFromSignals([[maybe_unused]] NativeSocket socket) {
#if defined(SO_NOSIGPIPE)
  int on = 1;
  ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
}

// The length recv and send take: an int on Windows, a size_t elsewhere.
auto ioLength(std::size_t bytes) {
#if defined(_WIN32)
  return static_cast<int>(std::min<std::size_t>(bytes, MaxLineBytes));
#else
  return bytes;
#endif
}

NativeSocket nativeOf(std::intptr_t handle) {
  return static_cast<NativeSocket>(handle);
}

std::intptr_t handleOf(NativeSocket socket) {
  return static_cast<std::intptr_t>(socket);
}

constexpr std::intptr_t NoSocket = -1;

#if defined(DWE_CONTROL_UNIX_SOCKETS)

core::Result<NativeSocket> listenUnix(const std::filesystem::path& path) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const std::string name = path.string();
  if (name.empty() || name.size() >= sizeof(address.sun_path)) {
    return core::fail(std::format("control: the socket path '{}' is empty or longer than {} bytes",
                                  name, sizeof(address.sun_path) - 1));
  }
  std::memcpy(address.sun_path, name.c_str(), name.size() + 1);
  std::error_code error;
  if (std::filesystem::is_socket(path, error)) {
    std::filesystem::remove(path, error);
  }
  const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0) {
    return core::fail(std::format("control: socket: {}", lastError()));
  }
  if (!setNonBlocking(listener) ||
      ::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 1) != 0) {
    const std::string problem = lastError();
    ::close(listener);
    return core::fail(std::format("control: cannot listen at {}: {}", name, problem));
  }
  core::logInfo("control: listening at {}", name);
  return listener;
}

#endif

// Listens on 127.0.0.1 at `port`, and gives the port listened on.
core::Result<std::pair<NativeSocket, std::uint16_t>> listenTcp(std::uint16_t port) {
  if (!startSockets()) {
    return core::fail(std::format("control: the sockets cannot start: {}", lastError()));
  }
  const NativeSocket listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (handleOf(listener) == NoSocket) {
    return core::fail(std::format("control: socket: {}", lastError()));
  }
  // A game that just ended leaves its port in TIME_WAIT; the next one may take it at once.
  int reuse = 1;
  ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
               sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t length = sizeof(address);
  if (!setNonBlocking(listener) ||
      ::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 1) != 0 ||
      ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    const std::string problem = lastError();
    closeSocket(listener);
    return core::fail(std::format("control: cannot listen at 127.0.0.1:{}: {}", port, problem));
  }
  const std::uint16_t bound = ntohs(address.sin_port);
  core::logInfo("control: listening at tcp:127.0.0.1:{}", bound);
  return std::pair{listener, bound};
}

} // namespace

core::Result<std::unique_ptr<ControlSocket>> ControlSocket::open(const ControlEndpoint& endpoint) {
  if (endpoint.unixPath) {
#if defined(DWE_CONTROL_UNIX_SOCKETS)
    auto listener = listenUnix(*endpoint.unixPath);
    if (!listener) {
      return std::unexpected{listener.error()};
    }
    return std::unique_ptr<ControlSocket>{
        new ControlSocket{handleOf(*listener), endpoint.unixPath, 0}};
#else
    return core::fail("control: this system has no Unix sockets; use --control tcp:<port>");
#endif
  }
  auto listener = listenTcp(endpoint.tcpPort);
  if (!listener) {
    return std::unexpected{listener.error()};
  }
  return std::unique_ptr<ControlSocket>{
      new ControlSocket{handleOf(listener->first), std::nullopt, listener->second}};
}

ControlSocket::~ControlSocket() {
  dropClient();
  closeSocket(nativeOf(listener_));
  if (unixPath_) {
    std::error_code ignored;
    std::filesystem::remove(*unixPath_, ignored);
  }
}

void ControlSocket::dropClient() {
  if (client_ != NoSocket) {
    closeSocket(nativeOf(client_));
    client_ = NoSocket;
  }
  received_.clear();
  unsent_.clear();
}

bool ControlSocket::accept() {
  if (client_ != NoSocket) {
    return true;
  }
  sockaddr_storage peer{};
  socklen_t length = sizeof(peer);
  const NativeSocket accepted =
      ::accept(nativeOf(listener_), reinterpret_cast<sockaddr*>(&peer), &length);
  if (handleOf(accepted) == NoSocket) {
    return false;
  }
  // The listener is bound to the loopback, so no other host can reach it; checked anyway, since
  // a client drives the whole game.
  if (!unixPath_) {
    const auto* inet = reinterpret_cast<const sockaddr_in*>(&peer);
    if (peer.ss_family != AF_INET || (ntohl(inet->sin_addr.s_addr) >> 24U) != 127U) {
      core::logWarning("control: a client from outside the loopback was refused");
      closeSocket(accepted);
      return false;
    }
  }
  if (!setNonBlocking(accepted)) {
    closeSocket(accepted);
    return false;
  }
  keepFromSignals(accepted);
  if (!unixPath_) {
    // Replies are short lines a tool waits for: no batching of them.
    int on = 1;
    ::setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on),
                 sizeof(on));
  }
  client_ = handleOf(accepted);
  core::logInfo("control: a client connected");
  return true;
}

std::vector<std::string> ControlSocket::receive() {
  std::vector<std::string> lines;
  if (!accept()) {
    return lines;
  }
  std::array<char, 4096> buffer{};
  while (true) {
    const auto read = ::recv(nativeOf(client_), buffer.data(), ioLength(buffer.size()), 0);
    if (read > 0) {
      received_.append(buffer.data(), static_cast<std::size_t>(read));
      continue;
    }
    if (read < 0 && wouldBlock()) {
      break;
    }
    // Closed by the client, or broken: the next client may connect.
    core::logInfo("control: the client left");
    dropClient();
    return lines;
  }
  std::size_t start = 0;
  for (std::size_t end = received_.find('\n'); end != std::string::npos;
       end = received_.find('\n', start)) {
    std::string line = received_.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      lines.push_back(std::move(line));
    }
    start = end + 1;
  }
  received_.erase(0, start);
  if (received_.size() > MaxLineBytes) {
    core::logWarning("control: a line longer than {} bytes; the client is dropped", MaxLineBytes);
    dropClient();
  }
  return lines;
}

void ControlSocket::send(std::string_view line) {
  if (client_ == NoSocket) {
    return;
  }
  unsent_ += line;
  unsent_ += '\n';
  flush();
}

void ControlSocket::flush() {
  while (client_ != NoSocket && !unsent_.empty()) {
    const auto sent =
        ::send(nativeOf(client_), unsent_.data(), ioLength(unsent_.size()), SendFlags);
    if (sent > 0) {
      unsent_.erase(0, static_cast<std::size_t>(sent));
      continue;
    }
    if (sent < 0 && wouldBlock()) {
      return;
    }
    dropClient();
  }
}

#else

core::Result<std::unique_ptr<ControlSocket>> ControlSocket::open(const ControlEndpoint&) {
  return core::fail("control: --control needs sockets, which this build does not have");
}

ControlSocket::~ControlSocket() = default;
bool ControlSocket::accept() {
  return false;
}
std::vector<std::string> ControlSocket::receive() {
  return {};
}
void ControlSocket::send(std::string_view) {}
void ControlSocket::flush() {}
void ControlSocket::dropClient() {}

#endif

ControlSocket::ControlSocket(std::intptr_t listener, std::optional<std::filesystem::path> unixPath,
                             std::uint16_t port)
    : listener_{listener}, unixPath_{std::move(unixPath)}, port_{port} {}

} // namespace dwe::app
