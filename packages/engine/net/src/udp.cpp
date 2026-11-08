#include <dwe/net/udp.hpp>

#include <array>
#include <cstring>
#include <format>
#include <string>
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
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace dwe::net {

namespace {

#if defined(_WIN32)
using Native = SOCKET;
using Length = int;
constexpr Native NoSocket = INVALID_SOCKET;

// Winsock needs starting once per process; it stays up until the process ends.
bool startSockets() {
  static const bool started = [] {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return started;
}

void closeNative(Native socket) {
  closesocket(socket);
}

bool makeNonBlocking(Native socket) {
  u_long on = 1;
  return ioctlsocket(socket, static_cast<long>(FIONBIO), &on) == 0;
}

std::string lastError() {
  return std::format("error {}", WSAGetLastError());
}
#else
using Native = int;
using Length = std::size_t;
constexpr Native NoSocket = -1;

bool startSockets() {
  return true;
}

void closeNative(Native socket) {
  close(socket);
}

bool makeNonBlocking(Native socket) {
  const int flags = fcntl(socket, F_GETFL, 0);
  return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::string lastError() {
  return std::strerror(errno);
}
#endif

sockaddr_in addressOf(const Endpoint& endpoint) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  std::memcpy(&address.sin_addr, endpoint.address.data(), endpoint.address.size());
  return address;
}

Endpoint endpointOf(const sockaddr_in& address) {
  Endpoint endpoint;
  std::memcpy(endpoint.address.data(), &address.sin_addr, endpoint.address.size());
  endpoint.port = ntohs(address.sin_port);
  return endpoint;
}

} // namespace

struct UdpLink::Socket {
  Native native = NoSocket;
  ~Socket() {
    if (native != NoSocket) {
      closeNative(native);
    }
  }
  Socket() = default;
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&&) = delete;
  Socket& operator=(Socket&&) = delete;
};

namespace {

core::Result<std::pair<std::unique_ptr<UdpLink::Socket>, std::uint16_t>>
openBound(std::uint16_t port) {
  if (!startSockets()) {
    return core::fail("the system's sockets could not start");
  }
  auto socket = std::make_unique<UdpLink::Socket>();
  socket->native = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket->native == NoSocket) {
    return core::fail(std::format("no UDP socket: {}", lastError()));
  }
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(port);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(socket->native, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
    return core::fail(std::format("UDP port {} could not be opened: {}", port, lastError()));
  }
  if (!makeNonBlocking(socket->native)) {
    return core::fail(
        std::format("the UDP socket could not be made non-blocking: {}", lastError()));
  }
  sockaddr_in bound{};
  socklen_t length = sizeof(bound);
  if (getsockname(socket->native, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
    return core::fail(std::format("the UDP socket's port is unknown: {}", lastError()));
  }
  return std::pair{std::move(socket), ntohs(bound.sin_port)};
}

} // namespace

UdpLink::UdpLink(std::unique_ptr<Socket> socket, std::uint16_t localPort,
                 std::optional<Endpoint> peer)
    : socket_{std::move(socket)}, localPort_{localPort}, peer_{peer},
      lockedPeer_{peer.has_value()} {}

UdpLink::~UdpLink() = default;

core::Result<std::unique_ptr<UdpLink>> UdpLink::host(std::uint16_t port) {
  auto opened = openBound(port);
  if (!opened) {
    return std::unexpected{opened.error()};
  }
  return std::unique_ptr<UdpLink>{
      new UdpLink{std::move(opened->first), opened->second, std::nullopt}};
}

core::Result<std::unique_ptr<UdpLink>> UdpLink::join(const Endpoint& endpoint) {
  auto opened = openBound(0);
  if (!opened) {
    return std::unexpected{opened.error()};
  }
  return std::unique_ptr<UdpLink>{new UdpLink{std::move(opened->first), opened->second, endpoint}};
}

void UdpLink::send(std::span<const std::uint8_t> datagram) {
  if (!peer_) {
    return;
  }
  const sockaddr_in to = addressOf(*peer_);
  // A refused send is a datagram lost on the way: the reliable channel sends it again.
  (void)::sendto(socket_->native, reinterpret_cast<const char*>(datagram.data()),
                 static_cast<Length>(datagram.size()), 0, reinterpret_cast<const sockaddr*>(&to),
                 sizeof(to));
}

std::optional<Bytes> UdpLink::receive() {
  std::array<std::uint8_t, MaxDatagram> buffer{};
  while (true) {
    sockaddr_in from{};
    socklen_t length = sizeof(from);
    const auto got = ::recvfrom(socket_->native, reinterpret_cast<char*>(buffer.data()),
                                static_cast<Length>(buffer.size()), 0,
                                reinterpret_cast<sockaddr*>(&from), &length);
    if (got < 0) {
      // Nothing waiting (or an ICMP error the system reports here): nothing to read now.
      return std::nullopt;
    }
    const Endpoint sender = endpointOf(from);
    if (!lockedPeer_) {
      peer_ = sender;
      lockedPeer_ = true;
    } else if (sender != *peer_) {
      continue;
    }
    return Bytes{buffer.begin(), buffer.begin() + got};
  }
}

std::vector<std::array<std::uint8_t, 4>> localAddresses() {
  std::vector<std::array<std::uint8_t, 4>> addresses;
  const auto add = [&](const sockaddr* address) {
    if (address == nullptr || address->sa_family != AF_INET) {
      return;
    }
    const Endpoint endpoint = endpointOf(*reinterpret_cast<const sockaddr_in*>(address));
    if (endpoint.address[0] != 127) {
      addresses.push_back(endpoint.address);
    }
  };
#if defined(_WIN32)
  if (!startSockets()) {
    return addresses;
  }
  std::array<char, 256> name{};
  if (gethostname(name.data(), static_cast<int>(name.size())) != 0) {
    return addresses;
  }
  addrinfo hints{};
  hints.ai_family = AF_INET;
  addrinfo* found = nullptr;
  if (getaddrinfo(name.data(), nullptr, &hints, &found) != 0) {
    return addresses;
  }
  for (const addrinfo* each = found; each != nullptr; each = each->ai_next) {
    add(each->ai_addr);
  }
  freeaddrinfo(found);
#else
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0) {
    return addresses;
  }
  for (const ifaddrs* each = interfaces; each != nullptr; each = each->ifa_next) {
    if ((each->ifa_flags & IFF_UP) != 0) {
      add(each->ifa_addr);
    }
  }
  freeifaddrs(interfaces);
#endif
  return addresses;
}

} // namespace dwe::net
