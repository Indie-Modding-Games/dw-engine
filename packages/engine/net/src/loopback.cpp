#include <dwe/net/channel_peer_link.hpp>
#include <dwe/net/loopback.hpp>

#include <algorithm>
#include <utility>

namespace dwe::net {

namespace {

constexpr std::int32_t Hundred = 100;

} // namespace

class LoopbackNetwork::End final : public DatagramLink {
public:
  End(LoopbackNetwork& network, std::size_t side) : network_{&network}, side_{side} {}

  void send(std::span<const std::uint8_t> datagram) override { network_->post(side_, datagram); }

  std::optional<Bytes> receive() override {
    std::deque<InFlight>& queue = network_->toward_.at(side_);
    const auto due = std::ranges::min_element(queue, [](const InFlight& a, const InFlight& b) {
      return a.due != b.due ? a.due < b.due : a.order < b.order;
    });
    if (due == queue.end() || due->due > network_->now_) {
      return std::nullopt;
    }
    Bytes datagram = std::move(due->datagram);
    queue.erase(due);
    return datagram;
  }

private:
  LoopbackNetwork* network_;
  std::size_t side_;
};

LoopbackNetwork::LoopbackNetwork(LinkConditions conditions, std::uint32_t seed)
    : conditions_{conditions}, random_{seed} {
  ends_.at(0) = std::make_unique<End>(*this, 0);
  ends_.at(1) = std::make_unique<End>(*this, 1);
  links_.at(0) = std::make_unique<ChannelPeerLink>(*ends_.at(0), "TEST");
  links_.at(1) = std::make_unique<ChannelPeerLink>(*ends_.at(1), "TEST");
}

LoopbackNetwork::~LoopbackNetwork() = default;

DatagramLink& LoopbackNetwork::peer(std::size_t side) {
  return *ends_.at(side);
}

PeerLink& LoopbackNetwork::link(std::size_t side) {
  return *links_.at(side);
}

void LoopbackNetwork::advance(Millis elapsed) {
  now_ += elapsed;
}

void LoopbackNetwork::post(std::size_t from, std::span<const std::uint8_t> datagram) {
  ++sent_;
  std::uniform_int_distribution<std::int32_t> percent{0, Hundred - 1};
  if (cut_ || percent(random_) < conditions_.lossPercent) {
    ++lost_;
    return;
  }
  const std::int32_t copies = percent(random_) < conditions_.duplicatePercent ? 2 : 1;
  std::deque<InFlight>& queue = toward_.at(1 - from);
  for (std::int32_t copy = 0; copy < copies; ++copy) {
    Millis delay = conditions_.latency;
    if (conditions_.jitter.count() > 0) {
      std::uniform_int_distribution<std::int64_t> extra{0, conditions_.jitter.count()};
      delay += Millis{extra(random_)};
    }
    queue.push_back({.due = now_ + delay,
                     .order = order_++,
                     .datagram = Bytes{datagram.begin(), datagram.end()}});
  }
}

} // namespace dwe::net
