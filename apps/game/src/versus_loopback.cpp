#include "versus_loopback.hpp"

#include <dwe/core/log.hpp>

#include <span>
#include <string>
#include <utility>

namespace dwe::app {

namespace {

using net::Bytes;
using net::Lane;
using net::Millis;
using net::PeerFailure;
using net::PeerLink;
using net::PeerState;

// One end of the loopback network, which the network keeps; the flow owns this handle to it.
class BorrowedLink final : public PeerLink {
public:
  explicit BorrowedLink(PeerLink& peer) : peer_{&peer} {}
  void send(Lane lane, std::span<const std::uint8_t> message) override {
    peer_->send(lane, message);
  }
  std::optional<Bytes> receive(Lane lane) override { return peer_->receive(lane); }
  void poll(Millis now) override { peer_->poll(now); }
  [[nodiscard]] PeerState state() const override { return peer_->state(); }
  [[nodiscard]] PeerFailure failure() const override { return peer_->failure(); }
  [[nodiscard]] std::optional<Millis> lastHeard() const override { return peer_->lastHeard(); }
  [[nodiscard]] std::string roomCode() const override { return peer_->roomCode(); }

private:
  PeerLink* peer_;
};

// No room but this machine's own: joining one fails as a wrong code does.
class NoRoomLink final : public PeerLink {
public:
  void send(Lane /*lane*/, std::span<const std::uint8_t> /*message*/) override {}
  std::optional<Bytes> receive(Lane /*lane*/) override { return std::nullopt; }
  void poll(Millis /*now*/) override {}
  [[nodiscard]] PeerState state() const override { return PeerState::Failed; }
  [[nodiscard]] PeerFailure failure() const override { return PeerFailure::WrongCode; }
  [[nodiscard]] std::optional<Millis> lastHeard() const override { return std::nullopt; }
};

constexpr std::size_t HostEnd = 0;
constexpr std::size_t RivalEnd = 1;
// The rows the other player tries: the save list's and the partner list's longest.
constexpr std::int32_t SaveRows = 8;
constexpr std::int32_t PartnerRows = 40;

VersusFlowDesc rivalDesc(VersusFlowDesc desc, net::LoopbackNetwork& network) {
  desc.host = [] { return std::make_unique<NoRoomLink>(); };
  desc.join = [&network](std::string_view /*code*/) -> std::unique_ptr<PeerLink> {
    return std::make_unique<BorrowedLink>(network.link(RivalEnd));
  };
  // The other player never hosts.
  desc.seed = [] { return std::uint64_t{0}; };
  return desc;
}

} // namespace

VersusLoopback::VersusLoopback(VersusFlowDesc rival)
    : network_{net::LinkConditions{}}, rival_{rivalDesc(std::move(rival), network_)} {
  rival_.open();
}

VersusLoopback::~VersusLoopback() = default;

std::unique_ptr<net::PeerLink> VersusLoopback::host() {
  hosting_ = true;
  return std::make_unique<BorrowedLink>(network_.link(HostEnd));
}

std::unique_ptr<net::PeerLink> VersusLoopback::join(std::string_view /*code*/) {
  return std::make_unique<NoRoomLink>();
}

void VersusLoopback::tick(Millis elapsed) {
  rival_.tick(network_.now());
  act();
  network_.advance(elapsed);
}

void VersusLoopback::act() {
  using View = VersusFlow::View;
  switch (rival_.view()) {
  case View::Menu:
    if (hosting_ && !joined_) {
      joined_ = true;
      rival_.act("down");
      rival_.act("confirm");
      rival_.type(network_.link(HostEnd).roomCode(), true);
      core::logInfo("versus: the loopback player joins room {}", network_.link(HostEnd).roomCode());
    }
    break;
  case View::SavePick:
    for (std::int32_t row = 0; row < SaveRows && rival_.view() == View::SavePick; ++row) {
      rival_.act("pick:" + std::to_string(row));
    }
    break;
  case View::Lineup:
    for (std::int32_t row = 0; row < PartnerRows && rival_.view() == View::Lineup; ++row) {
      rival_.act("pick:" + std::to_string(row));
    }
    break;
  default:
    break;
  }
}

} // namespace dwe::app
