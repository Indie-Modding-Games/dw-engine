#include "control_socket.hpp"

#include <dwe/core/log.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <system_error>
#include <utility>

#if defined(__unix__)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define DWE_CONTROL_SOCKET 1
#endif

namespace dwe::app {

#if defined(DWE_CONTROL_SOCKET)

namespace {

// A client that sends a longer line without a line break is dropped: no request is near it.
constexpr std::size_t MaxLineBytes = 1 << 20;

std::string lastError() {
  return std::error_code{errno, std::generic_category()}.message();
}

bool setNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 &&
         fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

} // namespace

core::Result<std::unique_ptr<ControlSocket>>
ControlSocket::open(const std::filesystem::path& path) {
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
  return std::unique_ptr<ControlSocket>{new ControlSocket{listener, path}};
}

ControlSocket::~ControlSocket() {
  dropClient();
  ::close(listener_);
  std::error_code ignored;
  std::filesystem::remove(path_, ignored);
}

void ControlSocket::dropClient() {
  if (client_ >= 0) {
    ::close(client_);
    client_ = -1;
  }
  received_.clear();
  unsent_.clear();
}

std::vector<std::string> ControlSocket::receive() {
  std::vector<std::string> lines;
  if (client_ < 0) {
    const int accepted = ::accept(listener_, nullptr, nullptr);
    if (accepted < 0) {
      return lines;
    }
    if (!setNonBlocking(accepted)) {
      ::close(accepted);
      return lines;
    }
    client_ = accepted;
    core::logInfo("control: a client connected");
  }
  std::array<char, 4096> buffer{};
  while (true) {
    const auto read = ::recv(client_, buffer.data(), buffer.size(), 0);
    if (read > 0) {
      received_.append(buffer.data(), static_cast<std::size_t>(read));
      continue;
    }
    if (read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
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
  if (client_ < 0) {
    return;
  }
  unsent_ += line;
  unsent_ += '\n';
  flush();
}

void ControlSocket::flush() {
  while (client_ >= 0 && !unsent_.empty()) {
    const auto sent = ::send(client_, unsent_.data(), unsent_.size(), MSG_NOSIGNAL);
    if (sent > 0) {
      unsent_.erase(0, static_cast<std::size_t>(sent));
      continue;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      return;
    }
    dropClient();
  }
}

ControlSocket::ControlSocket(int listener, std::filesystem::path path)
    : listener_{listener}, path_{std::move(path)} {}

#else

core::Result<std::unique_ptr<ControlSocket>> ControlSocket::open(const std::filesystem::path&) {
  return core::fail("control: --control needs Unix sockets, which this build does not have");
}

ControlSocket::~ControlSocket() = default;
std::vector<std::string> ControlSocket::receive() {
  return {};
}
void ControlSocket::send(std::string_view) {}
void ControlSocket::flush() {}
void ControlSocket::dropClient() {}
ControlSocket::ControlSocket(int listener, std::filesystem::path path)
    : listener_{listener}, path_{std::move(path)} {}

#endif

} // namespace dwe::app
