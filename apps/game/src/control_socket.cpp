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
// Connections the system holds before the game takes them: every client may connect at once.
constexpr int ListenBacklog = static_cast<int>(ControlSocket::MaxClients) + 4;
// What a client past the last one is told before it is closed.
constexpr std::string_view TooManyClients =
    R"j({"id":null,"ok":false,"error":"control: too many clients (4 at most)"})j"
    "\n";
static_assert(ControlSocket::MaxClients == 4, "TooManyClients names the number");

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
  return ::ioctlsocket(socket, static_cast<long>(FIONBIO), &on) == 0;
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
      ::listen(listener, ListenBacklog) != 0) {
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
      ::listen(listener, ListenBacklog) != 0 ||
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
  for (Client& client : clients_) {
    closeSocket(nativeOf(client.socket));
  }
  closeSocket(nativeOf(listener_));
  if (unixPath_) {
    std::error_code ignored;
    std::filesystem::remove(*unixPath_, ignored);
  }
}

void ControlSocket::drop(Client& client) {
  if (client.socket != NoSocket) {
    closeSocket(nativeOf(client.socket));
    client.socket = NoSocket;
    gone_.push_back(client.id);
  }
  client.received.clear();
  client.unsent.clear();
}

void ControlSocket::accept() {
  while (true) {
    sockaddr_storage peer{};
    socklen_t length = sizeof(peer);
    const NativeSocket accepted =
        ::accept(nativeOf(listener_), reinterpret_cast<sockaddr*>(&peer), &length);
    if (handleOf(accepted) == NoSocket) {
      return;
    }
    // The listener is bound to the loopback, so no other host can reach it; checked anyway, since
    // a client drives the whole game.
    if (!unixPath_) {
      const auto* inet = reinterpret_cast<const sockaddr_in*>(&peer);
      if (peer.ss_family != AF_INET || (ntohl(inet->sin_addr.s_addr) >> 24U) != 127U) {
        core::logWarning("control: a client from outside the loopback was refused");
        closeSocket(accepted);
        continue;
      }
    }
    if (!setNonBlocking(accepted)) {
      closeSocket(accepted);
      continue;
    }
    keepFromSignals(accepted);
    if (clients_.size() >= MaxClients) {
      // Best effort: a fresh socket takes a short line at once.
      ::send(accepted, TooManyClients.data(), ioLength(TooManyClients.size()), SendFlags);
      closeSocket(accepted);
      core::logWarning("control: a client was refused, {} are connected", MaxClients);
      continue;
    }
    if (!unixPath_) {
      // Replies are short lines a tool waits for: no batching of them.
      int on = 1;
      ::setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on),
                   sizeof(on));
    }
    clients_.push_back(
        {.id = nextClient_++, .socket = handleOf(accepted), .received = {}, .unsent = {}});
    core::logInfo("control: client {} connected", clients_.back().id);
  }
}

bool ControlSocket::read(Client& client, std::vector<ControlLine>& lines) {
  std::array<char, 4096> buffer{};
  while (true) {
    const auto got = ::recv(nativeOf(client.socket), buffer.data(), ioLength(buffer.size()), 0);
    if (got > 0) {
      client.received.append(buffer.data(), static_cast<std::size_t>(got));
      if (client.received.size() > MaxLineBytes && !client.received.contains('\n')) {
        break;
      }
      continue;
    }
    if (got < 0 && wouldBlock()) {
      break;
    }
    // Closed by the client, or broken; a line it left unfinished goes with it.
    core::logInfo("control: client {} left", client.id);
    return false;
  }
  std::size_t start = 0;
  for (std::size_t end = client.received.find('\n'); end != std::string::npos;
       end = client.received.find('\n', start)) {
    std::string line = client.received.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      lines.push_back({.client = client.id, .text = std::move(line)});
    }
    start = end + 1;
  }
  client.received.erase(0, start);
  if (client.received.size() > MaxLineBytes) {
    core::logWarning("control: a line longer than {} bytes; client {} is dropped", MaxLineBytes,
                     client.id);
    return false;
  }
  return true;
}

std::vector<ControlLine> ControlSocket::receive() {
  accept();
  std::vector<ControlLine> lines;
  for (Client& client : clients_) {
    if (!read(client, lines) || !flush(client)) {
      drop(client);
    }
  }
  std::erase_if(clients_, [](const Client& client) { return client.socket == NoSocket; });
  return lines;
}

void ControlSocket::send(std::uint64_t client, std::string_view line) {
  const auto found = std::ranges::find(clients_, client, &Client::id);
  if (found == clients_.end()) {
    return;
  }
  found->unsent += line;
  found->unsent += '\n';
  if (!flush(*found)) {
    drop(*found);
    clients_.erase(found);
  }
}

bool ControlSocket::flush(Client& client) {
  while (!client.unsent.empty()) {
    const auto sent = ::send(nativeOf(client.socket), client.unsent.data(),
                             ioLength(client.unsent.size()), SendFlags);
    if (sent > 0) {
      client.unsent.erase(0, static_cast<std::size_t>(sent));
      continue;
    }
    return sent < 0 && wouldBlock();
  }
  return true;
}

#else

core::Result<std::unique_ptr<ControlSocket>> ControlSocket::open(const ControlEndpoint&) {
  return core::fail("control: --control needs sockets, which this build does not have");
}

ControlSocket::~ControlSocket() = default;
void ControlSocket::accept() {}
std::vector<ControlLine> ControlSocket::receive() {
  return {};
}
void ControlSocket::send(std::uint64_t, std::string_view) {}
bool ControlSocket::read(Client&, std::vector<ControlLine>&) {
  return false;
}
bool ControlSocket::flush(Client&) {
  return false;
}
void ControlSocket::drop(Client&) {}

#endif

ControlSocket::ControlSocket(std::intptr_t listener, std::optional<std::filesystem::path> unixPath,
                             std::uint16_t port)
    : listener_{listener}, unixPath_{std::move(unixPath)}, port_{port} {}

} // namespace dwe::app
