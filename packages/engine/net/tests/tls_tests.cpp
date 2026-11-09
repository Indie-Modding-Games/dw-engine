#include <dwe/net/rtc_peer_link.hpp>
#include <dwe/net/signal.hpp>
#include <dwe/net/trusted_roots.hpp>

#include <doctest/doctest.h>
#include <rtc/rtc.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "test_certificates.hpp"

// The signaling client refuses a wss:// server it cannot trust: a real libdatachannel WebSocket
// against a local TLS server (libdatachannel's own) with the certificates of test_certificates.hpp.

using namespace dwe::net;
using namespace std::chrono_literals;

namespace {

constexpr auto SocketTimeout = 10s;

// A WebSocket server with TLS on a free port of this machine. Without a certificate it makes a
// self-signed one of its own.
class TlsServer {
public:
  TlsServer() : TlsServer{{}, {}} {}

  TlsServer(std::string_view certificate, std::string_view key) {
    rtcWsServerConfiguration configuration{};
    configuration.enableTls = true;
    if (!certificate.empty()) {
      folder_ = std::filesystem::temp_directory_path() /
                std::format("dwe-net-tls-{}",
                            std::chrono::steady_clock::now().time_since_epoch().count());
      std::filesystem::create_directories(folder_);
      certificateFile_ = write("server.crt", certificate);
      keyFile_ = write("server.key", key);
      configuration.certificatePemFile = certificateFile_.c_str();
      configuration.keyPemFile = keyFile_.c_str();
    }
    server_ = rtcCreateWebSocketServer(&configuration, onClient);
  }

  ~TlsServer() {
    if (server_ >= 0) {
      rtcDeleteWebSocketServer(server_);
    }
    if (!folder_.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(folder_, ignored);
    }
  }
  TlsServer(const TlsServer&) = delete;
  TlsServer& operator=(const TlsServer&) = delete;
  TlsServer(TlsServer&&) = delete;
  TlsServer& operator=(TlsServer&&) = delete;

  [[nodiscard]] bool listening() const { return server_ >= 0; }

  [[nodiscard]] std::string url(std::string_view host) const {
    return std::format("wss://{}:{}/", host, rtcGetWebSocketServerPort(server_));
  }

private:
  // The server keeps the clients that came; the tests only look at the client's side.
  static void RTC_API onClient(int /*server*/, int /*client*/, void* /*pointer*/) {}

  [[nodiscard]] std::string write(std::string_view name, std::string_view text) const {
    const std::filesystem::path path = folder_ / name;
    std::ofstream{path, std::ios::binary} << text << '\n';
    return path.string();
  }

  int server_ = -1;
  std::filesystem::path folder_;
  std::string certificateFile_;
  std::string keyFile_;
};

// Where the socket ended up: Open, or Failed / Closed when the handshake was refused.
SocketState settle(SignalSocket& socket) {
  const auto deadline = std::chrono::steady_clock::now() + SocketTimeout;
  while (socket.state() == SocketState::Connecting && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(5ms);
  }
  return socket.state();
}

// The room code the server gives a host on `socket`, or nothing.
std::string liveRoom(std::unique_ptr<SignalSocket> socket) {
  SignalClient host{std::move(socket)};
  const auto deadline = std::chrono::steady_clock::now() + SocketTimeout;
  while (host.roomCode().empty() && !host.failure() &&
         std::chrono::steady_clock::now() < deadline) {
    host.poll();
    std::this_thread::sleep_for(5ms);
  }
  return host.roomCode();
}

} // namespace

TEST_CASE("a wss server with a certificate valid for its host and trusted opens") {
  const TlsServer server{test::LocalhostCertificate, test::LocalhostKey};
  REQUIRE(server.listening());
  const auto socket = openWebSocketTrusting(server.url("localhost"), std::string{test::TestRoot});
  CHECK(settle(*socket) == SocketState::Open);
}

TEST_CASE("a wss server whose certificate is for another host is refused") {
  const TlsServer server{test::WrongHostCertificate, test::WrongHostKey};
  REQUIRE(server.listening());
  const auto socket = openWebSocketTrusting(server.url("localhost"), std::string{test::TestRoot});
  CHECK(settle(*socket) != SocketState::Open);
}

TEST_CASE("a wss server whose certificate no trusted root signed is refused") {
  // Signed by the test root, which the game's own roots and the system's do not include.
  const TlsServer signedByTestRoot{test::LocalhostCertificate, test::LocalhostKey};
  REQUIRE(signedByTestRoot.listening());
  const auto socket = openWebSocket(signedByTestRoot.url("localhost"));
  CHECK(settle(*socket) != SocketState::Open);

  const TlsServer selfSigned;
  REQUIRE(selfSigned.listening());
  const auto other = openWebSocket(selfSigned.url("localhost"));
  CHECK(settle(*other) != SocketState::Open);
}

TEST_CASE("the shipped roots are certificates") {
  const std::string roots = shippedSignalRoots();
  CHECK(roots.starts_with("-----BEGIN CERTIFICATE-----"));
  CHECK(roots.find("-----END CERTIFICATE-----\n-----BEGIN CERTIFICATE-----") != std::string::npos);
  CHECK(trustedSignalRoots().starts_with(roots));
}

TEST_CASE("the live signaling server makes a room over a verified wss (DWE_VERSUS_LIVE_SERVER)") {
  // Opt-in: it needs the internet. DWE_VERSUS_LIVE_SERVER=wss://dwe-versus-signal.regamelabs.
  // workers.dev checks that the deployed server's certificate passes the verification with every
  // root the game trusts, and with only the shipped ones (what Windows may have), and that it is
  // refused when none of the roots signed it.
  const char* live = std::getenv("DWE_VERSUS_LIVE_SERVER");
  if (live == nullptr || *live == '\0') {
    MESSAGE("DWE_VERSUS_LIVE_SERVER is not set: skipped");
    return;
  }
  CHECK(normalizedRoomCode(liveRoom(openWebSocket(hostUrl(live)))));
  CHECK(normalizedRoomCode(liveRoom(openWebSocketTrusting(hostUrl(live), shippedSignalRoots()))));
  const auto untrusted = openWebSocketTrusting(hostUrl(live), std::string{test::TestRoot});
  CHECK(settle(*untrusted) != SocketState::Open);
}
