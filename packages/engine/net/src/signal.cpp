#include <dwe/net/signal.hpp>

#include <nlohmann/json.hpp>

#include <utility>

namespace dwe::net {

namespace {

using nlohmann::json;

PeerFailure failureOf(std::string_view reason) {
  if (reason == "no-room") {
    return PeerFailure::WrongCode;
  }
  if (reason == "full") {
    return PeerFailure::RoomFull;
  }
  if (reason == "expired") {
    return PeerFailure::TimedOut;
  }
  if (reason == "left") {
    return PeerFailure::Closed;
  }
  return PeerFailure::ServerUnreachable;
}

std::string withoutTrailingSlash(std::string_view server) {
  while (server.ends_with('/')) {
    server.remove_suffix(1);
  }
  return std::string{server};
}

} // namespace

std::optional<std::string> normalizedRoomCode(std::string_view typed) {
  std::string code;
  for (const char letter : typed) {
    if (letter == ' ' || letter == '-') {
      continue;
    }
    const char upper =
        letter >= 'a' && letter <= 'z' ? static_cast<char>(letter - 'a' + 'A') : letter;
    if (!RoomAlphabet.contains(upper)) {
      return std::nullopt;
    }
    code.push_back(upper);
  }
  if (code.size() != RoomCodeLength) {
    return std::nullopt;
  }
  return code;
}

std::string hostUrl(std::string_view server, bool listed) {
  return withoutTrailingSlash(server) + "/" + std::string{SignalProtocol} + "/host" +
         (listed ? "?public=1" : "");
}

std::string roomsUrl(std::string_view server) {
  return withoutTrailingSlash(server) + "/" + std::string{SignalProtocol} + "/rooms";
}

std::optional<std::vector<PublicRoom>> parseRoomList(std::string_view text) {
  const json parsed = json::parse(text, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return std::nullopt;
  }
  const auto list = parsed.find("rooms");
  if (list == parsed.end() || !list->is_array()) {
    return std::nullopt;
  }
  const auto text_of = [](const json& room, const char* key) {
    const auto found = room.find(key);
    return found != room.end() && found->is_string() ? found->get<std::string>() : std::string{};
  };
  const auto number_of = [](const json& room, const char* key) {
    const auto found = room.find(key);
    return found != room.end() && found->is_number() ? found->get<double>() : 0.0;
  };
  std::vector<PublicRoom> rooms;
  for (const json& room : *list) {
    if (!room.is_object()) {
      continue;
    }
    PublicRoom entry{.roomId = text_of(room, "roomId"),
                     .joinCode = text_of(room, "joinCode"),
                     .name = text_of(room, "name"),
                     .message = text_of(room, "message"),
                     .players = static_cast<int>(number_of(room, "players")),
                     .capacity = static_cast<int>(number_of(room, "capacity")),
                     .createdAt = number_of(room, "createdAt"),
                     .state = text_of(room, "state")};
    const std::optional<std::string> code = normalizedRoomCode(entry.joinCode);
    if (!code) {
      continue;
    }
    entry.joinCode = *code;
    if (entry.roomId.empty()) {
      entry.roomId = entry.joinCode;
    }
    rooms.push_back(std::move(entry));
  }
  return rooms;
}

RoomListRequest::RoomListRequest(std::unique_ptr<SignalSocket> socket)
    : socket_{std::move(socket)} {
  if (socket_ == nullptr) {
    failed_ = true;
  }
}

void RoomListRequest::poll() {
  if (done()) {
    return;
  }
  while (std::optional<std::string> text = socket_->receive()) {
    rooms_ = parseRoomList(*text);
    failed_ = !rooms_.has_value();
    socket_->close();
    return;
  }
  const SocketState state = socket_->state();
  if (state == SocketState::Closed || state == SocketState::Failed) {
    failed_ = true;
  }
}

std::string joinUrl(std::string_view server, std::string_view code) {
  return withoutTrailingSlash(server) + "/" + std::string{SignalProtocol} + "/join/" +
         std::string{code};
}

SignalClient::SignalClient(std::unique_ptr<SignalSocket> socket) : socket_{std::move(socket)} {}

void SignalClient::poll() {
  if (finished_ || failure_) {
    return;
  }
  while (std::optional<std::string> text = socket_->receive()) {
    take(*text);
    if (failure_) {
      return;
    }
  }
  switch (socket_->state()) {
  case SocketState::Open:
    opened_ = true;
    break;
  case SocketState::Connecting:
    break;
  case SocketState::Closed:
  case SocketState::Failed:
    // A server that never answered is out of reach; one that closed the room early dropped it.
    fail(opened_ && paired_ ? PeerFailure::Closed : PeerFailure::ServerUnreachable);
    break;
  }
}

void SignalClient::relay(std::string_view data) {
  if (finished_ || failure_) {
    return;
  }
  json message = {{"type", "signal"}, {"data", json::parse(data, nullptr, false)}};
  socket_->send(message.dump());
}

void SignalClient::finish() {
  if (finished_ || failure_) {
    return;
  }
  finished_ = true;
  socket_->send(json{{"type", "done"}}.dump());
}

std::optional<std::string> SignalClient::receive() {
  if (inbox_.empty()) {
    return std::nullopt;
  }
  std::string data = std::move(inbox_.front());
  inbox_.pop_front();
  return data;
}

void SignalClient::take(std::string_view text) {
  opened_ = true;
  const json message = json::parse(text, nullptr, false);
  if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) {
    return;
  }
  const auto type = message["type"].get<std::string>();
  if (type == "room" && message.contains("code") && message["code"].is_string()) {
    code_ = message["code"].get<std::string>();
  } else if (type == "peer" || type == "joined") {
    paired_ = true;
  } else if (type == "signal" && message.contains("data") && message["data"].is_object()) {
    inbox_.push_back(message["data"].dump());
  } else if (type == "error") {
    const std::string reason = message.contains("reason") && message["reason"].is_string()
                                   ? message["reason"].get<std::string>()
                                   : std::string{};
    fail(failureOf(reason));
  }
}

void SignalClient::fail(PeerFailure failure) {
  failure_ = failure;
  socket_->close();
}

} // namespace dwe::net
