#include <dwe/core/log.hpp>
#include <dwe/net/rtc_peer_link.hpp>
#include <dwe/net/trusted_roots.hpp>

#include <nlohmann/json.hpp>
#include <rtc/rtc.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

// libdatachannel's C API: it reports errors as return codes and never throws, as the project's
// code is built without exceptions (docs/adr/0007-no-exceptions.md). Its threads call the
// callbacks with the user pointer of each id; they only fill a mailbox, which poll() empties on
// the caller's thread.

namespace dwe::net {

namespace {

using nlohmann::json;

constexpr std::string_view ControlLabel = "control";
constexpr std::string_view InputsLabel = "inputs";
// Every this long the unreliable lane carries one byte, so the other peer knows this one is still
// there between battles (the versus link's silence timeout).
constexpr Millis KeepAliveEvery{250};
constexpr std::uint8_t KeepAlive = 0x00;
// How long poll() must have been away before the keepalives go from a thread of their own
// (keepAliveWhileBusy).
constexpr Millis BusyAfter{500};

// A clock of its own for the busy keepalives: the thread cannot read the game's.
Millis steadyNow() {
  return std::chrono::duration_cast<Millis>(std::chrono::steady_clock::now().time_since_epoch());
}

std::size_t laneIndex(Lane lane) {
  return lane == Lane::Reliable ? 0 : 1;
}

struct Mailbox;

// The user pointer of a data channel: its mailbox and its lane.
struct LaneBox {
  Mailbox* mailbox = nullptr;
  std::size_t lane = 0;
};

// What libdatachannel's threads hand over to the caller's thread.
struct Mailbox {
  std::mutex mutex;
  std::deque<std::string> toRelay;
  std::deque<int> arrivedChannels;
  std::array<std::deque<Bytes>, 2> messages;
  std::array<bool, 2> open{};
  bool channelClosed = false;
  rtcState connection = RTC_NEW;
  std::array<LaneBox, 2> lanes{LaneBox{this, 0}, LaneBox{this, 1}};
};

Mailbox* mailboxOf(void* pointer) {
  return static_cast<Mailbox*>(pointer);
}

void RTC_API onDescription(int /*pc*/, const char* sdp, const char* type, void* pointer) {
  if (Mailbox* box = mailboxOf(pointer)) {
    const std::scoped_lock lock{box->mutex};
    box->toRelay.push_back(json{{"description", sdp}, {"type", type}}.dump());
  }
}

void RTC_API onCandidate(int /*pc*/, const char* candidate, const char* mid, void* pointer) {
  if (Mailbox* box = mailboxOf(pointer)) {
    const std::scoped_lock lock{box->mutex};
    box->toRelay.push_back(json{{"candidate", candidate}, {"mid", mid}}.dump());
  }
}

void RTC_API onState(int /*pc*/, rtcState state, void* pointer) {
  if (Mailbox* box = mailboxOf(pointer)) {
    const std::scoped_lock lock{box->mutex};
    box->connection = state;
  }
}

void RTC_API onChannel(int /*pc*/, int channel, void* pointer) {
  if (Mailbox* box = mailboxOf(pointer)) {
    const std::scoped_lock lock{box->mutex};
    box->arrivedChannels.push_back(channel);
  }
}

void RTC_API onChannelOpen(int /*id*/, void* pointer) {
  if (const auto* lane = static_cast<const LaneBox*>(pointer)) {
    const std::scoped_lock lock{lane->mailbox->mutex};
    lane->mailbox->open.at(lane->lane) = true;
  }
}

void RTC_API onChannelClosed(int /*id*/, void* pointer) {
  if (const auto* lane = static_cast<const LaneBox*>(pointer)) {
    const std::scoped_lock lock{lane->mailbox->mutex};
    lane->mailbox->channelClosed = true;
  }
}

void RTC_API onChannelMessage(int /*id*/, const char* message, int size, void* pointer) {
  const auto* lane = static_cast<const LaneBox*>(pointer);
  if (lane == nullptr || size < 0) {
    return;
  }
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(message);
  Bytes copy{bytes, bytes + size};
  const std::scoped_lock lock{lane->mailbox->mutex};
  lane->mailbox->messages.at(lane->lane).push_back(std::move(copy));
}

// The WebSocket's side: its state and the texts that came.
struct SocketBox {
  std::mutex mutex;
  SocketState state = SocketState::Connecting;
  std::deque<std::string> texts;
};

void RTC_API onSocketOpen(int /*id*/, void* pointer) {
  if (auto* box = static_cast<SocketBox*>(pointer)) {
    const std::scoped_lock lock{box->mutex};
    box->state = SocketState::Open;
  }
}

void RTC_API onSocketClosed(int /*id*/, void* pointer) {
  if (auto* box = static_cast<SocketBox*>(pointer)) {
    const std::scoped_lock lock{box->mutex};
    if (box->state != SocketState::Failed) {
      box->state = SocketState::Closed;
    }
  }
}

void RTC_API onSocketError(int /*id*/, const char* error, void* pointer) {
  core::logWarning("versus: the signaling server: {}", error);
  if (auto* box = static_cast<SocketBox*>(pointer)) {
    const std::scoped_lock lock{box->mutex};
    box->state = SocketState::Failed;
  }
}

void RTC_API onSocketMessage(int /*id*/, const char* message, int size, void* pointer) {
  auto* box = static_cast<SocketBox*>(pointer);
  // A negative size is a text message, zero-terminated.
  if (box == nullptr || size >= 0) {
    return;
  }
  const std::scoped_lock lock{box->mutex};
  box->texts.emplace_back(message);
}

class WebSocketSignal final : public SignalSocket {
public:
  WebSocketSignal(const std::string& url, const std::string& trustedRoots) {
    // A wss:// server's certificate must be valid for its host and chain up to one of these
    // roots, or the socket fails (tooling/vcpkg-ports/libdatachannel/ws-ca-certificates.patch).
    rtcWsConfiguration configuration{};
    configuration.caCertificatePemFile = trustedRoots.c_str();
    socket_ = rtcCreateWebSocketEx(url.c_str(), &configuration);
    if (socket_ < 0) {
      core::logWarning("versus: cannot open {} ({})", url, socket_);
      box_->state = SocketState::Failed;
      return;
    }
    rtcSetUserPointer(socket_, box_.get());
    rtcSetOpenCallback(socket_, onSocketOpen);
    rtcSetClosedCallback(socket_, onSocketClosed);
    rtcSetErrorCallback(socket_, onSocketError);
    rtcSetMessageCallback(socket_, onSocketMessage);
  }

  ~WebSocketSignal() override {
    if (socket_ >= 0) {
      rtcSetUserPointer(socket_, nullptr);
      rtcDeleteWebSocket(socket_);
    }
  }
  WebSocketSignal(const WebSocketSignal&) = delete;
  WebSocketSignal& operator=(const WebSocketSignal&) = delete;
  WebSocketSignal(WebSocketSignal&&) = delete;
  WebSocketSignal& operator=(WebSocketSignal&&) = delete;

  void send(std::string text) override {
    if (socket_ >= 0 && rtcIsOpen(socket_)) {
      // A negative size sends a zero-terminated text.
      rtcSendMessage(socket_, text.c_str(), -1);
    }
  }

  std::optional<std::string> receive() override {
    const std::scoped_lock lock{box_->mutex};
    if (box_->texts.empty()) {
      return std::nullopt;
    }
    std::string text = std::move(box_->texts.front());
    box_->texts.pop_front();
    return text;
  }

  [[nodiscard]] SocketState state() const override {
    const std::scoped_lock lock{box_->mutex};
    return box_->state;
  }

  void close() override {
    if (socket_ >= 0) {
      rtcClose(socket_);
    }
  }

private:
  std::unique_ptr<SocketBox> box_ = std::make_unique<SocketBox>();
  int socket_ = -1;
};

} // namespace

std::unique_ptr<SignalSocket> openWebSocket(const std::string& url) {
  return openWebSocketTrusting(url, trustedSignalRoots());
}

std::unique_ptr<SignalSocket> openWebSocketTrusting(const std::string& url,
                                                    const std::string& trustedRoots) {
  return std::make_unique<WebSocketSignal>(url, trustedRoots);
}

struct RtcPeerLink::Impl {
  RtcSettings settings;
  bool hosting = false;
  std::unique_ptr<SignalClient> signal;
  std::unique_ptr<Mailbox> mailbox = std::make_unique<Mailbox>();
  int connection = -1;
  std::array<int, 2> channels{-1, -1};
  std::array<std::deque<Bytes>, 2> inbox;
  std::deque<Bytes> pendingReliable;
  PeerState state = PeerState::Connecting;
  PeerFailure failure = PeerFailure::None;
  std::optional<Millis> pairedAt;
  std::optional<Millis> lastHeard;
  Millis lastSent{0};
  bool everConnected = false;
  // The keepalives while the game is busy (keepAliveWhileBusy): a thread sends them on the
  // unreliable lane while it is open (libdatachannel sends from any thread) and poll() has been
  // away between BusyAfter and busyLimit, on the steady clock.
  std::atomic<std::int64_t> busyLimit{0};
  std::atomic<std::int64_t> polledAt{0};
  std::atomic<int> busyChannel{-1};
  std::jthread busyKeeper;

  void keepAliveWhileBusy(Millis limit) {
    busyLimit = limit.count();
    polledAt = steadyNow().count();
    if (limit <= Millis{0} || busyKeeper.joinable()) {
      return;
    }
    busyKeeper = std::jthread{[this](const std::stop_token& stop) {
      std::mutex mutex;
      std::condition_variable_any wake;
      while (!stop.stop_requested()) {
        {
          std::unique_lock lock{mutex};
          (void)wake.wait_for(lock, stop, KeepAliveEvery, [] { return false; });
        }
        const int channel = busyChannel.load();
        const Millis away = steadyNow() - Millis{polledAt.load()};
        if (!stop.stop_requested() && channel >= 0 && away >= BusyAfter &&
            away < Millis{busyLimit.load()}) {
          const char keepAlive = static_cast<char>(KeepAlive);
          rtcSendMessage(channel, &keepAlive, 1);
        }
      }
    }};
  }

  void stopBusyKeeper() {
    busyChannel = -1;
    if (busyKeeper.joinable()) {
      busyKeeper.request_stop();
      busyKeeper.join();
    }
  }

  void fail(PeerFailure why) {
    if (state == PeerState::Failed) {
      return;
    }
    busyChannel = -1;
    core::logInfo("versus: the peer link failed ({})", static_cast<int>(why));
    state = PeerState::Failed;
    failure = why;
  }

  void watch(int channel) {
    std::array<char, 32> label{};
    if (rtcGetDataChannelLabel(channel, label.data(), static_cast<int>(label.size())) < 0) {
      return;
    }
    const std::string_view name{label.data()};
    if (name != ControlLabel && name != InputsLabel) {
      return;
    }
    const std::size_t lane = name == ControlLabel ? 0 : 1;
    channels.at(lane) = channel;
    rtcSetUserPointer(channel, &mailbox->lanes.at(lane));
    rtcSetOpenCallback(channel, onChannelOpen);
    rtcSetClosedCallback(channel, onChannelClosed);
    rtcSetMessageCallback(channel, onChannelMessage);
    if (rtcIsOpen(channel)) {
      const std::scoped_lock lock{mailbox->mutex};
      mailbox->open.at(lane) = true;
    }
  }

  // The servers the peer connection uses: the game's own, then the relays the signaling server
  // handed out; relay only keeps the relays (turn:) alone.
  [[nodiscard]] std::vector<std::string> iceServers() const {
    std::vector<std::string> all = settings.iceServers;
    if (signal) {
      std::ranges::move(relayUrls(signal->iceServers()), std::back_inserter(all));
    }
    if (settings.relayOnly) {
      std::erase_if(all, [](const std::string& url) { return !url.starts_with("turn:"); });
    }
    return all;
  }

  bool makeConnection(const std::vector<std::string>& urls) {
    std::vector<const char*> servers;
    servers.reserve(urls.size());
    for (const std::string& server : urls) {
      servers.push_back(server.c_str());
    }
    rtcConfiguration configuration{};
    configuration.iceServers = servers.data();
    configuration.iceServersCount = static_cast<int>(servers.size());
    configuration.iceTransportPolicy =
        settings.relayOnly ? RTC_TRANSPORT_POLICY_RELAY : RTC_TRANSPORT_POLICY_ALL;
    connection = rtcCreatePeerConnection(&configuration);
    if (connection < 0) {
      core::logWarning("versus: cannot make a peer connection ({})", connection);
      return false;
    }
    rtcSetUserPointer(connection, mailbox.get());
    rtcSetLocalDescriptionCallback(connection, onDescription);
    rtcSetLocalCandidateCallback(connection, onCandidate);
    rtcSetStateChangeCallback(connection, onState);
    rtcSetDataChannelCallback(connection, onChannel);
    if (hosting) {
      watch(rtcCreateDataChannel(connection, std::string{ControlLabel}.c_str()));
      rtcDataChannelInit inputs{};
      inputs.reliability.unordered = true;
      inputs.reliability.unreliable = true;
      inputs.reliability.maxRetransmits = 0;
      watch(rtcCreateDataChannelEx(connection, std::string{InputsLabel}.c_str(), &inputs));
    }
    return true;
  }

  void takeSignal(const std::string& text) {
    const json data = json::parse(text, nullptr, false);
    if (!data.is_object() || connection < 0) {
      return;
    }
    const auto field = [&](std::string_view key) -> std::optional<std::string> {
      const auto found = data.find(key);
      if (found == data.end() || !found->is_string()) {
        return std::nullopt;
      }
      return found->get<std::string>();
    };
    if (const auto sdp = field("description"), type = field("type"); sdp && type) {
      if (rtcSetRemoteDescription(connection, sdp->c_str(), type->c_str()) < 0) {
        core::logWarning("versus: the other peer's description was refused");
      }
    } else if (const auto candidate = field("candidate"), mid = field("mid"); candidate && mid) {
      if (rtcAddRemoteCandidate(connection, candidate->c_str(), mid->c_str()) < 0) {
        core::logWarning("versus: a candidate of the other peer was refused");
      }
    }
  }

  void sendNow(std::size_t lane, std::span<const std::uint8_t> message) {
    const int channel = channels.at(lane);
    if (channel < 0) {
      return;
    }
    if (rtcSendMessage(channel, reinterpret_cast<const char*>(message.data()),
                       static_cast<int>(message.size())) < 0) {
      core::logWarning("versus: cannot send to the other peer");
      if (lane == 0) {
        fail(everConnected ? PeerFailure::Closed : PeerFailure::NoRoute);
      }
    }
  }

  void close() {
    stopBusyKeeper();
    for (const int channel : channels) {
      if (channel >= 0) {
        rtcSetUserPointer(channel, nullptr);
        rtcDeleteDataChannel(channel);
      }
    }
    if (connection >= 0) {
      rtcSetUserPointer(connection, nullptr);
      rtcDeletePeerConnection(connection);
    }
  }
};

RtcPeerLink::RtcPeerLink(RtcSettings settings, bool hosting) : impl_{std::make_unique<Impl>()} {
  impl_->settings = std::move(settings);
  impl_->hosting = hosting;
}

RtcPeerLink::~RtcPeerLink() {
  impl_->close();
}

std::unique_ptr<RtcPeerLink> RtcPeerLink::host(RtcSettings settings, const SocketOpener& open,
                                               const std::optional<PublicHost>& listing) {
  const std::string server = settings.signalServer;
  std::unique_ptr<RtcPeerLink> link{new RtcPeerLink{std::move(settings), true}};
  if (server.empty()) {
    link->impl_->fail(PeerFailure::NoServer);
    return link;
  }
  const int seats = link->impl_->settings.seats;
  link->impl_->signal = std::make_unique<SignalClient>(
      open(listing ? hostUrl(server, *listing, seats) : hostUrl(server, false, seats)));
  return link;
}

std::unique_ptr<RtcPeerLink> RtcPeerLink::hostSeat(RtcSettings settings, std::string_view code,
                                                   std::string_view token, int taken,
                                                   const SocketOpener& open) {
  const std::string server = settings.signalServer;
  std::unique_ptr<RtcPeerLink> link{new RtcPeerLink{std::move(settings), true}};
  if (server.empty()) {
    link->impl_->fail(PeerFailure::NoServer);
    return link;
  }
  link->impl_->signal = std::make_unique<SignalClient>(open(seatUrl(server, code, token, taken)));
  return link;
}

void RtcPeerLink::announceSeats(const SeatsNotice& notice) {
  if (impl_->signal && !impl_->signal->paired()) {
    impl_->signal->tellSeats(notice);
  }
}

std::unique_ptr<RtcPeerLink> RtcPeerLink::join(RtcSettings settings, std::string_view code,
                                               const SocketOpener& open) {
  const std::string server = settings.signalServer;
  std::unique_ptr<RtcPeerLink> link{new RtcPeerLink{std::move(settings), false}};
  if (server.empty()) {
    link->impl_->fail(PeerFailure::NoServer);
    return link;
  }
  link->impl_->signal = std::make_unique<SignalClient>(open(joinUrl(server, code)));
  return link;
}

void RtcPeerLink::send(Lane lane, std::span<const std::uint8_t> message) {
  Impl& impl = *impl_;
  if (impl.state == PeerState::Failed) {
    return;
  }
  if (impl.state != PeerState::Open) {
    if (lane == Lane::Reliable) {
      impl.pendingReliable.emplace_back(message.begin(), message.end());
    }
    return;
  }
  impl.sendNow(laneIndex(lane), message);
}

std::optional<Bytes> RtcPeerLink::receive(Lane lane) {
  auto& inbox = impl_->inbox.at(laneIndex(lane));
  if (inbox.empty()) {
    return std::nullopt;
  }
  Bytes message = std::move(inbox.front());
  inbox.pop_front();
  return message;
}

void RtcPeerLink::keepAliveWhileBusy(Millis limit) {
  impl_->keepAliveWhileBusy(limit);
}

void RtcPeerLink::poll(Millis now) {
  impl_->polledAt = steadyNow().count();
  Impl& impl = *impl_;
  if (impl.state == PeerState::Failed || !impl.signal) {
    return;
  }
  SignalClient& signal = *impl.signal;
  signal.poll();
  if (impl.state != PeerState::Open && signal.failure()) {
    impl.fail(*signal.failure());
    return;
  }
  if (signal.paired() && !impl.pairedAt) {
    impl.pairedAt = now;
    const std::vector<std::string> servers = impl.iceServers();
    if (impl.settings.relayOnly && servers.empty()) {
      core::logWarning("versus: relay only, and the signaling server handed out no relay");
      impl.fail(PeerFailure::NoRelay);
      return;
    }
    core::logInfo("versus: opening the route with {} ICE servers{}", servers.size(),
                  impl.settings.relayOnly ? " (relay only)" : "");
    if (!impl.makeConnection(servers)) {
      impl.fail(PeerFailure::NoRoute);
      return;
    }
  }
  while (std::optional<std::string> text = signal.receive()) {
    impl.takeSignal(*text);
  }

  std::deque<std::string> toRelay;
  std::deque<int> arrived;
  std::array<std::deque<Bytes>, 2> messages;
  std::array<bool, 2> open{};
  bool channelClosed = false;
  rtcState connection = RTC_NEW;
  {
    const std::scoped_lock lock{impl.mailbox->mutex};
    toRelay.swap(impl.mailbox->toRelay);
    arrived.swap(impl.mailbox->arrivedChannels);
    messages.swap(impl.mailbox->messages);
    open = impl.mailbox->open;
    channelClosed = impl.mailbox->channelClosed;
    connection = impl.mailbox->connection;
  }
  for (const std::string& data : toRelay) {
    signal.relay(data);
  }
  for (const int channel : arrived) {
    impl.watch(channel);
  }
  for (std::size_t lane = 0; lane < messages.size(); ++lane) {
    for (Bytes& message : messages.at(lane)) {
      impl.lastHeard = now;
      if (lane == 1 && message.size() == 1 && message.front() == KeepAlive) {
        continue;
      }
      impl.inbox.at(lane).push_back(std::move(message));
    }
  }

  if (connection == RTC_CONNECTED) {
    impl.everConnected = true;
  }
  if (impl.state == PeerState::Connecting) {
    if (open[0] && open[1]) {
      impl.state = PeerState::Open;
      impl.busyChannel = impl.channels.at(1);
      impl.lastHeard = now;
      signal.finish();
      std::deque<Bytes> pending = std::exchange(impl.pendingReliable, {});
      for (const Bytes& message : pending) {
        impl.sendNow(0, message);
      }
    } else if (connection == RTC_FAILED) {
      impl.fail(PeerFailure::NoRoute);
    } else if (impl.pairedAt && now - *impl.pairedAt >= impl.settings.connectTimeout) {
      impl.fail(impl.everConnected ? PeerFailure::TimedOut : PeerFailure::NoRoute);
    }
    return;
  }
  if (impl.state == PeerState::Open) {
    // A route that is only disconnected may come back; the versus link's silence timeout decides.
    if (channelClosed || connection == RTC_FAILED || connection == RTC_CLOSED) {
      impl.fail(PeerFailure::Closed);
      return;
    }
    if (now - impl.lastSent >= KeepAliveEvery) {
      const std::array<std::uint8_t, 1> keepAlive{KeepAlive};
      impl.sendNow(1, keepAlive);
      impl.lastSent = now;
    }
  }
}

PeerState RtcPeerLink::state() const {
  return impl_->state;
}

PeerFailure RtcPeerLink::failure() const {
  return impl_->failure;
}

std::optional<Millis> RtcPeerLink::lastHeard() const {
  return impl_->lastHeard;
}

std::string RtcPeerLink::roomCode() const {
  return impl_->signal ? impl_->signal->roomCode() : std::string{};
}

std::string RtcPeerLink::roomToken() const {
  return impl_->signal ? impl_->signal->roomToken() : std::string{};
}

bool RtcPeerLink::peerFound() const {
  return impl_->state == PeerState::Open || (impl_->signal && impl_->signal->paired());
}

} // namespace dwe::net
