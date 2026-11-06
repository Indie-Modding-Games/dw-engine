#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include "control_socket.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#endif

namespace {

using dwe::app::ControlLine;
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

// Calls receive until `count` lines came, for at most about two seconds.
std::vector<ControlLine> receiveLines(ControlSocket& socket, std::size_t count = 1) {
  std::vector<ControlLine> lines;
  for (int attempt = 0; attempt < 200 && lines.size() < count; ++attempt) {
    for (ControlLine& line : socket.receive()) {
      lines.push_back(std::move(line));
    }
    if (lines.size() < count) {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
  }
  return lines;
}

// The texts of the lines receiveLines gives.
std::vector<std::string> receiveSome(ControlSocket& socket, std::size_t count = 1) {
  std::vector<std::string> texts;
  for (ControlLine& line : receiveLines(socket, count)) {
    texts.push_back(std::move(line.text));
  }
  return texts;
}

// Calls receive until the socket has `count` clients, for at most about two seconds.
bool waitForClients(ControlSocket& socket, std::size_t count) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    socket.receive();
    if (socket.clients() == count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  return false;
}

int connectTcp(std::uint16_t port) {
  const int client = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (client < 0 ||
      ::connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    return -1;
  }
  return client;
}

int connectUnix(const std::filesystem::path& path) {
  const int client = ::socket(AF_UNIX, SOCK_STREAM, 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const std::string name = path.string();
  std::memcpy(&address.sun_path[0], name.c_str(), name.size() + 1);
  if (client < 0 ||
      ::connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    return -1;
  }
  return client;
}

bool sendText(int client, std::string_view text) {
  return ::send(client, text.data(), text.size(), 0) == static_cast<ssize_t>(text.size());
}

// What a client reads within about two seconds, up to and with the first line break.
std::string readLine(int client) {
  std::string line;
  char c = 0;
  pollfd waiting{.fd = client, .events = POLLIN, .revents = 0};
  while (::poll(&waiting, 1, 2000) > 0 && ::recv(client, &c, 1, 0) == 1) {
    line += c;
    if (c == '\n') {
      break;
    }
  }
  return line;
}

// Two clients send their requests interleaved, each line in pieces, and each reads only its own
// replies.
void checkTwoClients(ControlSocket& socket, int first, int second) {
  REQUIRE(waitForClients(socket, 2));
  REQUIRE(sendText(first, "{\"id\":1,"));
  REQUIRE(sendText(second, "{\"id\":2,\"cmd\":\"status\"}\n{\"id\":3,"));
  REQUIRE(sendText(first, "\"cmd\":\"live\"}\n"));
  REQUIRE(sendText(second, "\"cmd\":\"live\"}\n"));
  std::vector<ControlLine> lines = receiveLines(socket, 3);
  REQUIRE(lines.size() == 3);
  const auto clientOf = [&](std::string_view text) {
    const auto found = std::ranges::find(lines, text, &ControlLine::text);
    return found != lines.end() ? found->client : 0;
  };
  const std::uint64_t one = clientOf(R"({"id":1,"cmd":"live"})");
  const std::uint64_t two = clientOf(R"({"id":2,"cmd":"status"})");
  CHECK(one != 0);
  CHECK(two != 0);
  CHECK(one != two);
  CHECK(clientOf(R"({"id":3,"cmd":"live"})") == two);
  // A client's lines come in the order it sent them.
  std::erase_if(lines, [&](const ControlLine& line) { return line.client != two; });
  CHECK(lines.front().text == R"({"id":2,"cmd":"status"})");

  socket.send(two, R"({"id":2,"ok":true})");
  socket.send(one, R"({"id":1,"ok":true})");
  socket.send(two, R"({"id":3,"ok":true})");
  CHECK(readLine(first) == "{\"id\":1,\"ok\":true}\n");
  CHECK(readLine(second) == "{\"id\":2,\"ok\":true}\n");
  CHECK(readLine(second) == "{\"id\":3,\"ok\":true}\n");

  // The first leaves in the middle of a line: the second goes on as before.
  REQUIRE(sendText(first, "{\"id\":4,\"cm"));
  ::close(first);
  REQUIRE(waitForClients(socket, 1));
  CHECK(socket.takeGone() == std::vector<std::uint64_t>{one});
  socket.send(one, R"({"id":4,"ok":true})");
  REQUIRE(sendText(second, "{\"id\":5,\"cmd\":\"status\"}\n"));
  lines = receiveLines(socket);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].client == two);
  socket.send(two, R"({"id":5,"ok":true})");
  CHECK(readLine(second) == "{\"id\":5,\"ok\":true}\n");

  ::close(second);
  REQUIRE(waitForClients(socket, 0));
}

TEST_CASE("the TCP channel takes a loopback client's lines and answers it") {
  auto opened = ControlSocket::open("tcp:0");
  REQUIRE_MESSAGE(opened.has_value(), opened.error().message);
  ControlSocket& socket = **opened;
  REQUIRE(socket.port() != 0);

  const int client = connectTcp(socket.port());
  REQUIRE(client >= 0);

  const std::string request = "{\"id\":1,\"cmd\":\"status\"}\r\n{\"id\":2,\"cmd\":";
  REQUIRE(::send(client, request.data(), request.size(), 0) ==
          static_cast<ssize_t>(request.size()));
  CHECK(receiveSome(socket) == std::vector<std::string>{R"({"id":1,"cmd":"status"})"});
  const std::string rest = "\"quit\"}\n";
  REQUIRE(::send(client, rest.data(), rest.size(), 0) == static_cast<ssize_t>(rest.size()));
  CHECK(receiveSome(socket) == std::vector<std::string>{R"({"id":2,"cmd":"quit"})"});

  CHECK(socket.clients() == 1);
  socket.send(1, R"({"id":1,"ok":true})");
  CHECK(readLine(client) == "{\"id\":1,\"ok\":true}\n");
  ::close(client);
}

TEST_CASE("two TCP clients at once each get only their own replies") {
  auto opened = ControlSocket::open("tcp:0");
  REQUIRE_MESSAGE(opened.has_value(), opened.error().message);
  const int first = connectTcp((*opened)->port());
  const int second = connectTcp((*opened)->port());
  REQUIRE(first >= 0);
  REQUIRE(second >= 0);
  checkTwoClients(**opened, first, second);
}

TEST_CASE("two Unix socket clients at once each get only their own replies") {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / std::format("dwe-control-two-{}.sock", ::getpid());
  auto opened = ControlSocket::open(path.string());
  REQUIRE_MESSAGE(opened.has_value(), opened.error().message);
  const int first = connectUnix(path);
  const int second = connectUnix(path);
  REQUIRE(first >= 0);
  REQUIRE(second >= 0);
  checkTwoClients(**opened, first, second);
}

TEST_CASE("a client past the fourth is told so and closed") {
  auto opened = ControlSocket::open("tcp:0");
  REQUIRE_MESSAGE(opened.has_value(), opened.error().message);
  ControlSocket& socket = **opened;
  std::vector<int> clients;
  for (std::size_t index = 0; index < ControlSocket::MaxClients; ++index) {
    clients.push_back(connectTcp(socket.port()));
    REQUIRE(clients.back() >= 0);
  }
  REQUIRE(waitForClients(socket, ControlSocket::MaxClients));
  const int fifth = connectTcp(socket.port());
  REQUIRE(fifth >= 0);
  socket.receive();
  CHECK(readLine(fifth) ==
        "{\"id\":null,\"ok\":false,\"error\":\"control: too many clients (4 at most)\"}\n");
  char byte = 0;
  CHECK(::recv(fifth, &byte, 1, 0) == 0);
  CHECK(socket.clients() == ControlSocket::MaxClients);
  ::close(fifth);
  for (const int client : clients) {
    ::close(client);
  }
  // Once they left, a new one is taken, with a number never given before.
  REQUIRE(waitForClients(socket, 0));
  const int later = connectTcp(socket.port());
  REQUIRE(later >= 0);
  REQUIRE(sendText(later, "{}\n"));
  const auto lines = receiveLines(socket);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].client == ControlSocket::MaxClients + 1);
  ::close(later);
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
