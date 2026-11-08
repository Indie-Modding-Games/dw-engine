#include <dwe/net/loopback.hpp>
#include <dwe/net/peer_link.hpp>
#include <dwe/session/versus_match.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "versus_fixture.hpp"
#include "versus_flow.hpp"

using dwe::app::VersusFlow;
using dwe::app::VersusFlowDesc;
using dwe::app::VersusSave;
using dwe::net::Bytes;
using dwe::net::Lane;
using dwe::net::LoopbackNetwork;
using dwe::net::Millis;
using dwe::net::PeerFailure;
using dwe::net::PeerLink;
using dwe::net::PeerState;
using dwe::session::testing::errorOf;
using dwe::session::testing::TestPacks;
using versus_test::Machine;
using versus_test::registered;
using View = dwe::app::VersusFlow::View;

namespace {

// A game tick, as the flow is ticked: 60 a second.
constexpr Millis Tick{17};

// One of the loopback network's peer links handed to a flow, which owns its link.
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

// What the signaling server answers to a code no room has.
class NoRoomLink final : public PeerLink {
public:
  void send(Lane /*lane*/, std::span<const std::uint8_t> /*message*/) override {}
  std::optional<Bytes> receive(Lane /*lane*/) override { return std::nullopt; }
  void poll(Millis /*now*/) override {}
  [[nodiscard]] PeerState state() const override { return PeerState::Failed; }
  [[nodiscard]] PeerFailure failure() const override { return PeerFailure::WrongCode; }
  [[nodiscard]] std::optional<Millis> lastHeard() const override { return std::nullopt; }
};

std::vector<VersusSave> savesOf(std::vector<dwe::session::RegisteredPartner> partners) {
  std::vector<VersusSave> saves;
  saves.push_back({.player = "empty", .nickname = "none", .partners = {}});
  saves.push_back({.player = "player", .nickname = "pal", .partners = std::move(partners)});
  return saves;
}

// One player's machine with its versus flow on one side of the network.
struct Player {
  Player(const TestPacks& packs, LoopbackNetwork& network, std::size_t side,
         std::vector<dwe::session::RegisteredPartner> partners)
      : machine{packs} {
    auto identity =
        dwe::session::versusIdentity("0.1.0", machine.session->rules().current(), packs.files());
    REQUIRE_MESSAGE(identity, errorOf(identity));
    PeerLink* peer = &network.link(side);
    flow = std::make_unique<VersusFlow>(
        VersusFlowDesc{.battles = machine.battle,
                       .identity = std::move(*identity),
                       .saves = [partners = std::move(partners)] { return savesOf(partners); },
                       .host = [peer]() -> std::unique_ptr<PeerLink> {
                         return std::make_unique<BorrowedLink>(*peer);
                       },
                       // Only the loopback room's code has a room.
                       .join = [peer](std::string_view code) -> std::unique_ptr<PeerLink> {
                         if (code != peer->roomCode()) {
                           return std::make_unique<NoRoomLink>();
                         }
                         return std::make_unique<BorrowedLink>(*peer);
                       },
                       .seed = [] { return std::uint64_t{4321}; }});
    flow->open();
  }

  Machine machine;
  std::unique_ptr<VersusFlow> flow;
};

const dwe::ui::UiValue* valueOf(const dwe::screens::ScreenSettings& settings,
                                std::string_view name) {
  for (const dwe::screens::ScreenSetting& setting : settings) {
    if (setting.name == name) {
      return &setting.value;
    }
  }
  return nullptr;
}

std::string text(const VersusFlow& flow, std::string_view name) {
  const dwe::screens::ScreenSettings settings = flow.settings();
  const dwe::ui::UiValue* value = valueOf(settings, name);
  const auto* string = value != nullptr ? std::get_if<std::string>(value) : nullptr;
  return string != nullptr ? *string : std::string{};
}

double number(const VersusFlow& flow, std::string_view name) {
  const dwe::screens::ScreenSettings settings = flow.settings();
  const dwe::ui::UiValue* value = valueOf(settings, name);
  const auto* real = value != nullptr ? std::get_if<double>(value) : nullptr;
  return real != nullptr ? *real : -1.0;
}

// Both machines tick together until `done` or the ticks run out; each player now and then
// presses one of its commands.
template <typename Done>
void play(LoopbackNetwork& network, Player& host, Player& guest, int ticks, Done done) {
  for (int tick = 0; tick < ticks && !done(); ++tick) {
    host.flow->tick(network.now());
    guest.flow->tick(network.now());
    network.advance(Tick);
    if (tick % 41 == 0) {
      for (Player* player : {&host, &guest}) {
        if (player->flow->view() == View::Match) {
          player->flow->act(tick % 82 == 0 ? "right" : "confirm");
        }
      }
    }
  }
}

// Both players get from the menu to their save pick: the host hosts a series of `battles`, the
// guest types its join code and joins.
void connect(LoopbackNetwork& network, Player& host, Player& guest) {
  host.flow->act("confirm");
  REQUIRE(host.flow->view() == View::HostSetup);
  host.flow->act("right");
  CHECK(number(*host.flow, "versus.battles") == 3);
  host.flow->act("confirm");
  REQUIRE(host.flow->view() == View::Hosting);

  guest.flow->act("down");
  guest.flow->act("confirm");
  REQUIRE(guest.flow->view() == View::JoinEntry);
  CHECK(guest.flow->takesText());
  // The guest types the host's room code, as it reads it.
  const std::string code = text(*host.flow, "versus.code");
  REQUIRE(code == "TEST");
  guest.flow->type("te", false);
  guest.flow->type("st", true);
  REQUIRE(guest.flow->view() == View::Joining);

  play(network, host, guest, 600,
       [&] { return host.flow->view() == View::SavePick && guest.flow->view() == View::SavePick; });
  REQUIRE(host.flow->view() == View::SavePick);
  REQUIRE(guest.flow->view() == View::SavePick);
  // The guest's series length comes from the host.
  CHECK(number(*guest.flow, "versus.battles") == 3);
}

// A player picks its second save (the first has no partners) and three partners.
void pickLineup(Player& player, std::array<int, 3> rows) {
  player.flow->act("pick:0");
  CHECK(player.flow->view() == View::SavePick);
  player.flow->act("pick:1");
  REQUIRE(player.flow->view() == View::Lineup);
  for (const int row : rows) {
    player.flow->act("pick:" + std::to_string(row));
  }
  CHECK(player.flow->view() == View::WaitingLineup);
}

std::vector<dwe::session::RegisteredPartner> hostPartners() {
  return {registered("a", "first_kind", 300, 90), registered("b", "second_kind", 150, 40),
          registered("c", "first_kind", 600, 70)};
}

std::vector<dwe::session::RegisteredPartner> guestPartners() {
  return {registered("x", "second_kind", 500, 60), registered("y", "first_kind", 250, 100),
          registered("z", "second_kind", 50, 10)};
}

} // namespace

TEST_CASE("two machines play an online versus series from the menus to the result") {
  const TestPacks packs;
  LoopbackNetwork network{{.latency = Millis{40}, .jitter = Millis{30}, .lossPercent = 5}, 3};
  Player host{packs, network, 0, hostPartners()};
  Player guest{packs, network, 1, guestPartners()};
  connect(network, host, guest);

  pickLineup(host, {0, 1, 2});
  pickLineup(guest, {1, 0, 2});
  play(network, host, guest, 600,
       [&] { return host.flow->view() == View::Match && guest.flow->view() == View::Match; });
  REQUIRE(host.flow->view() == View::Match);
  REQUIRE(guest.flow->view() == View::Match);
  CHECK(number(*host.flow, "versus.local") == 0);
  CHECK(number(*guest.flow, "versus.local") == 1);
  CHECK(text(*host.flow, "versus.f.0.name") == "a");
  CHECK(text(*guest.flow, "versus.f.1.name") == "y");
  CHECK(number(*host.flow, "versus.menu") > 0);

  play(network, host, guest, 60 * 60 * 20,
       [&] { return host.flow->view() == View::Result && guest.flow->view() == View::Result; });
  INFO("host error: " << text(*host.flow, "versus.error_key"));
  INFO("guest error: " << text(*guest.flow, "versus.error_key"));
  REQUIRE(host.flow->view() == View::Result);
  REQUIRE(guest.flow->view() == View::Result);
  CHECK(number(*host.flow, "versus.results") == 3);
  CHECK(number(*host.flow, "versus.wins.0") == number(*guest.flow, "versus.wins.0"));
  CHECK(number(*host.flow, "versus.wins.1") == number(*guest.flow, "versus.wins.1"));
  const std::string hostSays = text(*host.flow, "versus.winner");
  const std::string guestSays = text(*guest.flow, "versus.winner");
  CHECK(hostSays == (guestSays == "you" ? "rival" : guestSays == "rival" ? "you" : "draw"));
  for (int round = 0; round < 3; ++round) {
    const std::string name = "versus.result." + std::to_string(round) + ".outcome";
    const std::string mine = text(*host.flow, name);
    const std::string theirs = text(*guest.flow, name);
    CHECK(mine == (theirs == "won" ? "lost" : theirs == "lost" ? "won" : "draw"));
  }

  host.flow->act("confirm");
  CHECK(host.flow->view() == View::Menu);
  CHECK(host.flow->series() == nullptr);
}

TEST_CASE(
    "a code that cannot be one stays on the entry; a wrong one or a silent host ends the join") {
  const TestPacks packs;
  LoopbackNetwork network{{}, 1};
  Player guest{packs, network, 1, guestPartners()};
  guest.flow->act("pick:1");
  REQUIRE(guest.flow->view() == View::JoinEntry);
  // Three characters, and a fourth that no code has (0 looks like O).
  guest.flow->type("k7q", true);
  CHECK(guest.flow->view() == View::JoinEntry);
  const dwe::screens::ScreenSettings settings = guest.flow->settings();
  const dwe::ui::UiValue* bad = valueOf(settings, "versus.bad_entry");
  REQUIRE(bad != nullptr);
  CHECK(std::get<bool>(*bad));
  guest.flow->type("0", true);
  CHECK(guest.flow->view() == View::JoinEntry);
  guest.flow->act("cancel");
  CHECK(text(*guest.flow, "versus.typed") == "k7q");
  // A well-formed code no room has: the server says so.
  guest.flow->type("2", false);
  guest.flow->act("confirm");
  guest.flow->tick(network.now());
  REQUIRE(guest.flow->view() == View::Error);
  CHECK(text(*guest.flow, "versus.error_key") == "versus.error.wrong_code");
  guest.flow->act("confirm");
  REQUIRE(guest.flow->view() == View::Menu);

  // The room's code, but nobody answers in it.
  guest.flow->act("pick:1");
  guest.flow->type("TEST", true);
  REQUIRE(guest.flow->view() == View::Joining);
  CHECK(text(*guest.flow, "versus.stage") == "connecting");
  for (int tick = 0; tick < 60 * 30 && guest.flow->view() == View::Joining; ++tick) {
    guest.flow->tick(network.now());
    network.advance(Tick);
  }
  REQUIRE(guest.flow->view() == View::Error);
  CHECK(text(*guest.flow, "versus.error_key") == "versus.error.no_answer");
  guest.flow->act("confirm");
  CHECK(guest.flow->view() == View::Menu);
  guest.flow->act("cancel");
  CHECK(guest.flow->closed());
}

TEST_CASE("a player who leaves a battle ends the series for the other one") {
  const TestPacks packs;
  LoopbackNetwork network{{.latency = Millis{20}}, 5};
  Player host{packs, network, 0, hostPartners()};
  Player guest{packs, network, 1, guestPartners()};
  connect(network, host, guest);
  pickLineup(host, {0, 1, 2});
  pickLineup(guest, {1, 0, 2});
  play(network, host, guest, 600,
       [&] { return host.flow->view() == View::Match && guest.flow->view() == View::Match; });
  REQUIRE(guest.flow->view() == View::Match);

  // A first cancel only asks; a second one leaves.
  guest.flow->act("cancel");
  CHECK(guest.flow->view() == View::Match);
  guest.flow->act("cancel");
  CHECK(guest.flow->view() == View::Menu);
  play(network, host, guest, 600, [&] { return host.flow->view() == View::Error; });
  REQUIRE(host.flow->view() == View::Error);
  CHECK(text(*host.flow, "versus.error_key") == "versus.error.left");
}

TEST_CASE("vs_select.c: a partner is picked once and a save needs one per battle") {
  const TestPacks packs;
  LoopbackNetwork network{{.latency = Millis{20}}, 7};
  Player host{packs, network, 0, hostPartners()};
  // The guest's save has two registered partners: too few for a series of three.
  Player guest{packs,
               network,
               1,
               {registered("x", "second_kind", 500, 60), registered("y", "first_kind", 250, 100)}};
  connect(network, host, guest);

  guest.flow->act("pick:1");
  CHECK(guest.flow->view() == View::SavePick);
  CHECK(text(*guest.flow, "versus.save.1.player") == "player");

  host.flow->act("pick:1");
  REQUIRE(host.flow->view() == View::Lineup);
  host.flow->act("pick:2");
  host.flow->act("pick:2");
  CHECK(number(*host.flow, "versus.picks") == 1);
  const dwe::screens::ScreenSettings settings = host.flow->settings();
  const dwe::ui::UiValue* picked = valueOf(settings, "versus.partner.2.picked");
  REQUIRE(picked != nullptr);
  CHECK(std::get<bool>(*picked));
  // Back takes the last pick off.
  host.flow->act("cancel");
  CHECK(number(*host.flow, "versus.picks") == 0);
  CHECK(host.flow->view() == View::Lineup);
}
