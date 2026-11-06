#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include "control_socket.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#endif

namespace {

using dwe::app::ControlSocket;
using dwe::app::parseControlEndpoint;

TEST_CASE("--control takes a Unix socket's path or a TCP port on the loopback") {
  const auto path = parseControlEndpoint("/tmp/dwe.sock");
  REQUIRE(path.has_value());
  CHECK(path->unixPath == std::filesystem::path{"/tmp/dwe.sock"});

  for (const char* text : {"tcp:4710", "tcp:127.0.0.1:4710", "tcp:localhost:4710"}) {
    const auto tcp = parseControlEndpoint(text);
    REQUIRE_MESSAGE(tcp.has_value(), text);
    CHECK_FALSE(tcp->unixPath.has_value());
    CHECK(tcp->tcpPort == 4710);
  }
  CHECK(parseControlEndpoint("tcp:0")->tcpPort == 0);

  // Only the loopback: the channel drives the whole game.
  CHECK(parseControlEndpoint("tcp:0.0.0.0:4710").error().message.contains("loopback"));
  CHECK(parseControlEndpoint("tcp:192.168.1.2:4710").error().message.contains("loopback"));
  for (const char* wrong : {"", "tcp:", "tcp:port", "tcp:70000", "tcp:-1", "tcp:12x"}) {
    CHECK_FALSE_MESSAGE(parseControlEndpoint(wrong).has_value(), wrong);
  }
}

#if defined(__unix__) || defined(__APPLE__)

// Calls receive until a line comes, for at most about two seconds.
std::vector<std::string> receiveSome(ControlSocket& socket) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    auto lines = socket.receive();
    if (!lines.empty()) {
      return lines;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  return {};
}

TEST_CASE("the TCP channel takes a loopback client's lines and answers it") {
  auto opened = ControlSocket::open("tcp:0");
  REQUIRE_MESSAGE(opened.has_value(), opened.error().message);
  ControlSocket& socket = **opened;
  REQUIRE(socket.port() != 0);

  const int client = ::socket(AF_INET, SOCK_STREAM, 0);
  REQUIRE(client >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(socket.port());
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  REQUIRE(::connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);

  const std::string request = "{\"id\":1,\"cmd\":\"status\"}\r\n{\"id\":2,\"cmd\":";
  REQUIRE(::send(client, request.data(), request.size(), 0) ==
          static_cast<ssize_t>(request.size()));
  CHECK(receiveSome(socket) == std::vector<std::string>{R"({"id":1,"cmd":"status"})"});
  const std::string rest = "\"quit\"}\n";
  REQUIRE(::send(client, rest.data(), rest.size(), 0) == static_cast<ssize_t>(rest.size()));
  CHECK(receiveSome(socket) == std::vector<std::string>{R"({"id":2,"cmd":"quit"})"});

  socket.send(R"({"id":1,"ok":true})");
  std::string reply(64, '\0');
  const auto read = ::recv(client, reply.data(), reply.size(), 0);
  REQUIRE(read > 0);
  reply.resize(static_cast<std::size_t>(read));
  CHECK(reply == "{\"id\":1,\"ok\":true}\n");
  ::close(client);
}

TEST_CASE("the Unix socket channel still works") {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / std::format("dwe-control-{}.sock", ::getpid());
  {
    auto opened = ControlSocket::open(path.string());
    REQUIRE_MESSAGE(opened.has_value(), opened.error().message);
    CHECK((*opened)->port() == 0);
    CHECK(std::filesystem::is_socket(path));
  }
  // The socket's file goes with the channel.
  CHECK_FALSE(std::filesystem::exists(path));
}

#endif

} // namespace
