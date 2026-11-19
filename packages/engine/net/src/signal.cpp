#include <dwe/net/signal.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
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
  if (reason == "limit" || reason == "bad-name") {
    return PeerFailure::Refused;
  }
  return PeerFailure::ServerUnreachable;
}

std::string withoutTrailingSlash(std::string_view server) {
  while (server.ends_with('/')) {
    server.remove_suffix(1);
  }
  return std::string{server};
}

std::string base(std::string_view server) {
  return withoutTrailingSlash(server) + "/" + std::string{SignalProtocol};
}

std::string textOf(const json& value, const char* key) {
  const auto found = value.find(key);
  return found != value.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

// The code point that starts at `at` of UTF-8 text, and how many bytes it takes (0 when it is not
// well formed).
std::pair<char32_t, std::size_t> codePointAt(std::string_view text, std::size_t at) {
  const auto byte = static_cast<unsigned char>(text[at]);
  std::size_t length = 0;
  char32_t point = 0;
  if (byte < 0x80) {
    return {byte, 1};
  }
  if ((byte & 0xE0U) == 0xC0U) {
    length = 2;
    point = byte & 0x1FU;
  } else if ((byte & 0xF0U) == 0xE0U) {
    length = 3;
    point = byte & 0x0FU;
  } else if ((byte & 0xF8U) == 0xF0U) {
    length = 4;
    point = byte & 0x07U;
  } else {
    return {0, 0};
  }
  if (at + length > text.size()) {
    return {0, 0};
  }
  for (std::size_t next = 1; next < length; ++next) {
    const auto continuation = static_cast<unsigned char>(text[at + next]);
    if ((continuation & 0xC0U) != 0x80U) {
      return {0, 0};
    }
    point = (point << 6U) | (continuation & 0x3FU);
  }
  return {point, length};
}

// A letter the directory takes in a nickname (limits.ts: NAME).
bool nameLetter(char32_t point) {
  const bool ascii = (point >= U'a' && point <= U'z') || (point >= U'A' && point <= U'Z') ||
                     (point >= U'0' && point <= U'9') || point == U' ' || point == U'.' ||
                     point == U'_' || point == U'\'' || point == U'-';
  // The Latin letters with accents, without × and ÷.
  const bool accented = point >= 0xC0 && point <= 0x24F && point != 0xD7 && point != 0xF7;
  return ascii || accented;
}

constexpr std::size_t DirectoryNameLength = 16;

std::vector<IceServer> iceServersOf(const json& message) {
  std::vector<IceServer> servers;
  const auto list = message.find("iceServers");
  if (list == message.end() || !list->is_array()) {
    return servers;
  }
  for (const json& entry : *list) {
    if (!entry.is_object()) {
      continue;
    }
    IceServer server{.urls = {},
                     .username = textOf(entry, "username"),
                     .credential = textOf(entry, "credential")};
    if (const auto urls = entry.find("urls"); urls != entry.end() && urls->is_array()) {
      for (const json& url : *urls) {
        if (url.is_string()) {
          server.urls.push_back(url.get<std::string>());
        }
      }
    }
    if (!server.urls.empty()) {
      servers.push_back(std::move(server));
    }
  }
  return servers;
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

std::string directoryName(std::string_view name) {
  std::string kept;
  std::size_t letters = 0;
  for (std::size_t at = 0; at < name.size() && letters < DirectoryNameLength;) {
    const auto [point, length] = codePointAt(name, at);
    if (length == 0) {
      ++at;
      continue;
    }
    if (nameLetter(point) && !(point == U' ' && (kept.empty() || kept.ends_with(' ')))) {
      kept.append(name.substr(at, length));
      ++letters;
    }
    at += length;
  }
  while (kept.ends_with(' ')) {
    kept.pop_back();
  }
  return kept;
}

std::string compatKey(std::string_view mode, std::string_view version, std::string_view content) {
  constexpr std::size_t ContentLetters = 16;
  constexpr std::size_t KeyLetters = 64;
  std::string key{mode};
  key += '-';
  key += version;
  key += '-';
  key += content.substr(0, ContentLetters);
  for (char& letter : key) {
    const bool kept = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
                      (letter >= '0' && letter <= '9') || letter == '.' || letter == '_' ||
                      letter == '-';
    if (!kept) {
      letter = '-';
    }
  }
  if (key.size() > KeyLetters) {
    key.resize(KeyLetters);
  }
  return key;
}

std::string percentEncoded(std::string_view text) {
  constexpr std::string_view Hex = "0123456789ABCDEF";
  std::string out;
  for (const char letter : text) {
    const auto byte = static_cast<unsigned char>(letter);
    const bool unreserved = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
                            byte == '.' || byte == '~';
    if (unreserved) {
      out.push_back(letter);
    } else {
      out.push_back('%');
      out.push_back(Hex.at(byte >> 4U));
      out.push_back(Hex.at(byte & 0x0FU));
    }
  }
  return out;
}

std::string hostUrl(std::string_view server, bool listed) {
  return base(server) + "/host" + (listed ? "?public=1" : "");
}

std::string hostUrl(std::string_view server, const PublicHost& listing) {
  std::string url = base(server) + "/host?public=1";
  const std::string name = directoryName(listing.name);
  if (!name.empty()) {
    url += "&name=" + percentEncoded(name);
  }
  if (!listing.icon.empty()) {
    url += "&icon=" + percentEncoded(listing.icon);
  }
  if (!listing.compat.empty()) {
    url += "&compat=" + percentEncoded(listing.compat);
  }
  if (!listing.mode.empty()) {
    url += "&mode=" + percentEncoded(listing.mode);
  }
  return url;
}

std::string roomsUrl(std::string_view server, std::string_view compat) {
  return base(server) + "/rooms" +
         (compat.empty() ? std::string{} : "?compat=" + percentEncoded(compat));
}

std::string infoUrl(std::string_view server) {
  return base(server) + "/info";
}

std::string stateUrl(std::string_view server, std::string_view code, std::string_view token,
                     std::string_view state) {
  return base(server) + "/rooms/" + percentEncoded(code) + "/state?token=" + percentEncoded(token) +
         "&state=" + percentEncoded(state);
}

std::vector<std::string> relayUrls(std::span<const IceServer> servers) {
  std::vector<std::string> urls;
  for (const IceServer& server : servers) {
    if (server.username.empty() || server.credential.empty()) {
      continue;
    }
    for (const std::string& url : server.urls) {
      // turn:host[:port][?transport=udp]; turns: and TCP go through TLS or TCP, which libjuice
      // does not relay over.
      if (!url.starts_with("turn:") || url.contains("transport=tcp") ||
          url.contains("transport=tls") || url.contains('@')) {
        continue;
      }
      urls.push_back("turn:" + percentEncoded(server.username) + ":" +
                     percentEncoded(server.credential) + "@" + url.substr(5));
    }
  }
  return urls;
}

bool ServerInfo::has(std::string_view feature) const {
  return std::ranges::contains(features, feature);
}

std::optional<ServerInfo> parseServerInfo(std::string_view text) {
  const json parsed = json::parse(text, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("features") ||
      !parsed["features"].is_array()) {
    return std::nullopt;
  }
  ServerInfo info{.protocol = textOf(parsed, "protocol"), .features = {}};
  for (const json& feature : parsed["features"]) {
    if (feature.is_string()) {
      info.features.push_back(feature.get<std::string>());
    }
  }
  return info;
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
                     .state = text_of(room, "state"),
                     .phase = text_of(room, "phase"),
                     .hostName = text_of(room, "hostName"),
                     .icon = text_of(room, "icon"),
                     .compat = text_of(room, "compat"),
                     .mode = text_of(room, "mode")};
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

SignalRequest::SignalRequest(std::unique_ptr<SignalSocket> socket) : socket_{std::move(socket)} {
  if (socket_ == nullptr) {
    failed_ = true;
  }
}

void SignalRequest::poll() {
  if (done()) {
    return;
  }
  if (std::optional<std::string> text = socket_->receive()) {
    answer_ = std::move(text);
    socket_->close();
    return;
  }
  const SocketState state = socket_->state();
  if (state == SocketState::Closed || state == SocketState::Failed) {
    failed_ = true;
  }
}

std::string SignalRequest::errorReason() const {
  if (!answer_) {
    return {};
  }
  const json message = json::parse(*answer_, nullptr, false);
  if (!message.is_object() || textOf(message, "type") != "error") {
    return {};
  }
  const std::string reason = textOf(message, "reason");
  return reason.empty() ? std::string{"unknown"} : reason;
}

RoomListRequest::RoomListRequest(std::unique_ptr<SignalSocket> socket)
    : request_{std::move(socket)} {}

void RoomListRequest::poll() {
  if (done()) {
    return;
  }
  request_.poll();
  if (request_.answer()) {
    rooms_ = parseRoomList(*request_.answer());
  }
}

ServerInfoRequest::ServerInfoRequest(std::unique_ptr<SignalSocket> socket)
    : request_{std::move(socket)} {}

void ServerInfoRequest::poll() {
  if (done()) {
    return;
  }
  request_.poll();
  if (request_.answer()) {
    info_ = parseServerInfo(*request_.answer());
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
    token_ = textOf(message, "token");
    iceServers_ = iceServersOf(message);
  } else if (type == "peer" || type == "joined") {
    paired_ = true;
    if (type == "joined") {
      iceServers_ = iceServersOf(message);
    }
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
