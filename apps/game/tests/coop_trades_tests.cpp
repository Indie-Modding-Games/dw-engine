// Item trades between co-op players (#360) on two real bags: the broker and both players' sides
// talk through the wire codec, as a host and a guest would, and each side swaps its own inventory.
#include <dwe/game/coop/link.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/coop/trade.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/net/loopback.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/session_system.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "coop_gameplay_bridge.hpp"
#include "coop_trade_bag.hpp"
#include "packages/game/evolution/tests/fixture.hpp"
#include "packages/game/inventory/tests/fixture.hpp"
#include "session_fixture.hpp"

namespace coop = dwe::game::coop;
namespace items = dwe::game::inventory::testing;
using dwe::app::InventoryTradeBag;
using dwe::game::evolution::testing::TableFixture;
using dwe::session::GameSession;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::SessionSystem;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// One player's game: a started session with an empty bag on the made-up items, and its trade side.
struct Player {
  Player(const TestPacks& packs, coop::PlayerId id) : party{id} {
    const TableFixture forms;
    auto evolution = evolutionAdapter(packs, forms);
    auto adapter =
        std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                                .items = items::fixtureItems(),
                                                                .effects = items::fixtureEffects(),
                                                                .evolution = evolution.get()});
    inventory = adapter.get();
    std::vector<std::unique_ptr<SessionSystem>> systems;
    systems.push_back(std::move(evolution));
    systems.push_back(std::move(adapter));
    session = newGameWith(packs, std::move(systems));
    std::vector<dwe::assets::ItemHandle> given;
    for (const auto& slot : bag().slots()) {
      if (slot) {
        given.push_back(slot->item);
      }
    }
    for (const dwe::assets::ItemHandle item : given) {
      bag().remove(item, bag().countOf(item));
    }
    trades.attach(inventory);
  }

  [[nodiscard]] dwe::game::inventory::Inventory& bag() const { return inventory->inventory(); }
  [[nodiscard]] std::int32_t count(std::string_view id) const {
    return bag().totalOf(items::handleOf(id));
  }
  void give(std::string_view id, std::int32_t count) const {
    REQUIRE(bag().add(items::handleOf(id), count).added == count);
  }

  std::unique_ptr<GameSession> session;
  InventoryAdapter* inventory = nullptr;
  InventoryTradeBag trades;
  coop::TradeParty party;
};

// What crosses the link: every message is encoded and decoded as the wire would.
coop::Message wire(const coop::Message& message) {
  auto decoded = coop::decode(coop::encode(message));
  REQUIRE(decoded);
  REQUIRE(decoded->has_value());
  return **decoded;
}

// A host and a guest on one map, the host refereeing.
struct Room {
  explicit Room(const TestPacks& packs)
      : host{packs, coop::HostPlayerId}, guest{packs, coop::GuestPlayerId} {}

  [[nodiscard]] Player& of(coop::PlayerId id) { return id == coop::HostPlayerId ? host : guest; }

  void toBroker(coop::PlayerId from, const std::optional<coop::Message>& message) {
    if (message) {
      deliver(broker.receive(from, wire(*message), places));
    }
  }
  void deliver(std::vector<coop::TradeMail> mails) {
    while (!mails.empty()) {
      std::vector<coop::TradeMail> next;
      for (const coop::TradeMail& mail : mails) {
        Player& player = of(mail.to);
        if (dropGuest && mail.to == coop::GuestPlayerId) {
          continue;
        }
        for (const coop::Message& reply : player.party.receive(wire(mail.message), player.trades)) {
          auto more = broker.receive(mail.to, wire(reply), places);
          next.insert(next.end(), more.begin(), more.end());
        }
      }
      mails = std::move(next);
    }
  }
  void offer(coop::PlayerId id, std::vector<coop::TradeItem> items) {
    toBroker(id, of(id).party.offer(std::move(items)));
  }
  void open() {
    toBroker(coop::HostPlayerId, host.party.ask(coop::GuestPlayerId));
    toBroker(coop::GuestPlayerId, guest.party.answer(true));
    REQUIRE(host.party.state());
    REQUIRE(host.party.state()->phase == coop::TradePhase::Open);
  }

  Player host;
  Player guest;
  coop::TradeBroker broker;
  std::array<coop::TradePlace, 2> places{
      coop::TradePlace{.player = coop::HostPlayerId, .map = "town", .busy = false},
      coop::TradePlace{.player = coop::GuestPlayerId, .map = "town", .busy = false}};
  bool dropGuest = false;
};

} // namespace

TEST_CASE("co-op trade: two bags swap their offers through the wire") {
  const TestPacks packs;
  Room room{packs};
  room.host.give(items::Tonic, 5);
  room.guest.give(items::Bandage, 3);
  room.open();
  room.offer(coop::HostPlayerId, {{.item = std::string{items::Tonic}, .count = 2}});
  room.offer(coop::GuestPlayerId, {{.item = std::string{items::Bandage}, .count = 3}});
  REQUIRE(room.host.party.canConfirm());
  REQUIRE(room.guest.party.canConfirm());
  room.toBroker(coop::HostPlayerId, room.host.party.confirm(true));
  // One ready is not a trade.
  CHECK(room.host.count(items::Tonic) == 5);
  room.toBroker(coop::GuestPlayerId, room.guest.party.confirm(true));

  CHECK(room.host.count(items::Tonic) == 3);
  CHECK(room.host.count(items::Bandage) == 3);
  CHECK(room.guest.count(items::Tonic) == 2);
  CHECK(room.guest.count(items::Bandage) == 0);
  const auto closed = room.host.party.takeClosed();
  REQUIRE(closed);
  CHECK(closed->outcome == coop::TradeOutcome::Done);
  CHECK(room.broker.open() == 0);
}

TEST_CASE("co-op trade: a changed offer takes both ready marks back") {
  const TestPacks packs;
  Room room{packs};
  room.host.give(items::Tonic, 5);
  room.guest.give(items::Bandage, 3);
  room.open();
  room.offer(coop::HostPlayerId, {{.item = std::string{items::Tonic}, .count = 1}});
  room.offer(coop::GuestPlayerId, {{.item = std::string{items::Bandage}, .count = 1}});
  room.toBroker(coop::HostPlayerId, room.host.party.confirm(true));
  REQUIRE(room.guest.party.state()->confirmed[0]);
  room.offer(coop::GuestPlayerId, {{.item = std::string{items::Bandage}, .count = 2}});
  CHECK_FALSE(room.guest.party.state()->confirmed[0]);
  CHECK_FALSE(room.guest.party.state()->confirmed[1]);
  CHECK(room.host.count(items::Tonic) == 5);
}

TEST_CASE("co-op trade: a key item cannot be given and nothing moves") {
  const TestPacks packs;
  Room room{packs};
  room.host.give(items::Rod, 1);
  room.guest.give(items::Bandage, 1);
  CHECK_FALSE(dwe::app::tradeable(*room.host.inventory, items::Rod));
  CHECK(dwe::app::tradeable(*room.host.inventory, items::Tonic));
  room.open();
  room.offer(coop::HostPlayerId, {{.item = std::string{items::Rod}, .count = 1}});
  room.offer(coop::GuestPlayerId, {{.item = std::string{items::Bandage}, .count = 1}});
  room.toBroker(coop::HostPlayerId, room.host.party.confirm(true));
  room.toBroker(coop::GuestPlayerId, room.guest.party.confirm(true));
  CHECK(room.host.count(items::Rod) == 1);
  CHECK(room.guest.count(items::Bandage) == 1);
  CHECK(room.guest.count(items::Rod) == 0);
  const auto closed = room.guest.party.takeClosed();
  REQUIRE(closed);
  CHECK(closed->outcome == coop::TradeOutcome::Untradeable);
}

TEST_CASE("co-op trade: a full bag refuses and both bags stay as they were") {
  const TestPacks packs;
  Room room{packs};
  room.host.give(items::Tonic, 1);
  // The guest's bag full of other kinds, none of which it gives.
  const std::array kinds{items::Bandage,   items::Ether,      items::Floppy,     items::Revival,
                         items::PowerDisk, items::OmniDisk,   items::ScriptChip, items::Bun,
                         items::Steak,     items::Crest,      items::BrainChip,  items::Charm,
                         items::Toilet,    items::DoubleChip, items::Rod};
  for (const std::string_view kind : kinds) {
    if (room.guest.bag().hasRoomFor(items::handleOf(items::Tonic))) {
      room.guest.give(kind, 1);
    }
  }
  REQUIRE_FALSE(room.guest.bag().hasRoomFor(items::handleOf(items::Tonic)));
  room.open();
  room.offer(coop::HostPlayerId, {{.item = std::string{items::Tonic}, .count = 1}});
  room.toBroker(coop::HostPlayerId, room.host.party.confirm(true));
  room.toBroker(coop::GuestPlayerId, room.guest.party.confirm(true));
  CHECK(room.host.count(items::Tonic) == 1);
  CHECK(room.guest.count(items::Tonic) == 0);
  const auto closed = room.host.party.takeClosed();
  REQUIRE(closed);
  CHECK(closed->outcome == coop::TradeOutcome::NoRoom);
}

TEST_CASE("co-op trade: the bag's undo puts the items back and keeps the money") {
  const TestPacks packs;
  Player player{packs, coop::HostPlayerId};
  player.give(items::Tonic, 4);
  const std::array give{coop::TradeItem{.item = std::string{items::Tonic}, .count = 4}};
  const std::array get{coop::TradeItem{.item = std::string{items::Bandage}, .count = 2}};
  REQUIRE(player.trades.check(give, get) == coop::TradeCheck::Ok);
  REQUIRE(player.trades.apply(give, get) == coop::TradeCheck::Ok);
  CHECK(player.count(items::Tonic) == 0);
  CHECK(player.count(items::Bandage) == 2);
  player.bag().addMoney(50);
  player.give(items::Bun, 1);
  const std::int32_t money = player.bag().money();
  player.trades.undo();
  CHECK(player.count(items::Tonic) == 4);
  CHECK(player.count(items::Bandage) == 0);
  // What the bag took meanwhile (a claimed chest) stays.
  CHECK(player.count(items::Bun) == 1);
  CHECK(player.bag().money() == money);
  // Missing items and unknown ids.
  const std::array tooMany{coop::TradeItem{.item = std::string{items::Tonic}, .count = 5}};
  CHECK(player.trades.check(tooMany, {}) == coop::TradeCheck::Missing);
  const std::array unknown{coop::TradeItem{.item = "no_such_item", .count = 1}};
  CHECK(player.trades.check({}, unknown) == coop::TradeCheck::NoRoom);
}

TEST_CASE("co-op trade: a guest that drops during the swap leaves the host's bag as it was") {
  const TestPacks packs;
  Room room{packs};
  room.host.give(items::Tonic, 2);
  room.guest.give(items::Bandage, 2);
  room.open();
  room.offer(coop::HostPlayerId, {{.item = std::string{items::Tonic}, .count = 2}});
  room.offer(coop::GuestPlayerId, {{.item = std::string{items::Bandage}, .count = 2}});
  room.toBroker(coop::HostPlayerId, room.host.party.confirm(true));
  // The guest's ready reaches the host, then the link goes before the guest checks its side: the
  // host's side was asked to prepare but nothing was committed, and the leave closes the trade.
  room.dropGuest = true;
  room.toBroker(coop::GuestPlayerId, room.guest.party.confirm(true));
  room.deliver(room.broker.leave(coop::GuestPlayerId));
  CHECK(room.host.count(items::Tonic) == 2);
  CHECK(room.host.count(items::Bandage) == 0);
  CHECK(room.broker.open() == 0);
}

TEST_CASE("co-op trade: a host and a guest trade over a loopback link") {
  const TestPacks packs;
  Player host{packs, coop::HostPlayerId};
  Player guest{packs, coop::GuestPlayerId};
  host.give(items::Tonic, 3);
  guest.give(items::Bandage, 2);

  const coop::Identity identity{
      .protocol = coop::ProtocolVersion, .gameVersion = "test", .contentHash = "content"};
  dwe::net::LoopbackNetwork network({});
  coop::CoopLink hostLink = coop::CoopLink::host(network.link(0), identity);
  coop::CoopLink guestLink = coop::CoopLink::guest(network.link(1), identity, "Guest");
  dwe::app::NetworkFieldController hostRemote;
  dwe::app::NetworkFieldController guestRemote;
  dwe::app::CoopGameplayBridge hostBridge{hostLink, hostRemote};
  dwe::app::CoopGameplayBridge guestBridge{guestLink, guestRemote};
  coop::TradeBroker broker;
  const std::array places{
      coop::TradePlace{.player = coop::HostPlayerId, .map = "town", .busy = false},
      coop::TradePlace{.player = coop::GuestPlayerId, .map = "town", .busy = false}};

  // The host's mails: its own side locally, the guest's over the link.
  std::vector<coop::TradeMail> pending;
  const auto route = [&](std::vector<coop::TradeMail> mails) {
    pending.insert(pending.end(), mails.begin(), mails.end());
    while (!pending.empty()) {
      const coop::TradeMail mail = pending.front();
      pending.erase(pending.begin());
      if (mail.to == coop::GuestPlayerId) {
        REQUIRE(hostBridge.send(mail.message));
        continue;
      }
      for (const coop::Message& reply : host.party.receive(mail.message, host.trades)) {
        auto more = broker.receive(coop::HostPlayerId, reply, places);
        pending.insert(pending.end(), more.begin(), more.end());
      }
    }
  };
  const auto fromHost = [&](const std::optional<coop::Message>& message) {
    if (message) {
      route(broker.receive(coop::HostPlayerId, *message, places));
    }
  };
  const auto fromGuest = [&](const std::optional<coop::Message>& message) {
    if (message) {
      REQUIRE(guestBridge.send(*message));
    }
  };
  const auto pump = [&] {
    for (int step = 0; step < 20; ++step) {
      network.advance(dwe::net::Millis{1});
      hostBridge.poll(network.now());
      guestBridge.poll(network.now());
      while (auto message = hostBridge.receiveTrade()) {
        route(broker.receive(coop::GuestPlayerId, *message, places));
      }
      while (auto message = guestBridge.receiveTrade()) {
        for (const coop::Message& reply : guest.party.receive(*message, guest.trades)) {
          REQUIRE(guestBridge.send(reply));
        }
      }
    }
  };
  pump();
  REQUIRE(hostLink.phase() == coop::LinkPhase::Ready);
  REQUIRE(guestLink.phase() == coop::LinkPhase::Ready);

  fromGuest(guest.party.ask(coop::HostPlayerId));
  pump();
  REQUIRE(host.party.state());
  CHECK(host.party.state()->phase == coop::TradePhase::Asked);
  fromHost(host.party.answer(true));
  pump();
  REQUIRE(guest.party.state());
  CHECK(guest.party.state()->phase == coop::TradePhase::Open);

  fromHost(host.party.offer({{.item = std::string{items::Tonic}, .count = 3}}));
  fromGuest(guest.party.offer({{.item = std::string{items::Bandage}, .count = 1}}));
  pump();
  REQUIRE(host.party.canConfirm());
  REQUIRE(guest.party.canConfirm());
  fromHost(host.party.confirm(true));
  fromGuest(guest.party.confirm(true));
  pump();

  CHECK(host.count(items::Tonic) == 0);
  CHECK(host.count(items::Bandage) == 1);
  CHECK(guest.count(items::Tonic) == 3);
  CHECK(guest.count(items::Bandage) == 1);
  const auto closed = guest.party.takeClosed();
  REQUIRE(closed);
  CHECK(closed->outcome == coop::TradeOutcome::Done);
  CHECK(broker.open() == 0);
}

TEST_CASE("co-op trade: two guests trade through the host, each over its own link") {
  // The host's game (CoopTrades::tick and flush) reads each guest's trade messages from that
  // guest's link and sends the broker's mail on the link of the player it is for.
  const TestPacks packs;
  constexpr coop::PlayerId Second = 2;
  Player first{packs, coop::GuestPlayerId};
  Player second{packs, Second};
  first.give(items::Tonic, 2);
  second.give(items::Bandage, 3);

  const coop::Identity identity{
      .protocol = coop::ProtocolVersion, .gameVersion = "test", .contentHash = "content"};
  // One network, link and bridge pair per guest, as the host keeps one per guest.
  struct Seat {
    Seat(const coop::Identity& identity, coop::PlayerId id)
        : network{{}}, hostLink{coop::CoopLink::host(network.link(0), identity, "Host", {}, id)},
          guestLink{coop::CoopLink::guest(network.link(1), identity, "Guest")},
          hostBridge{hostLink, hostRemote}, guestBridge{guestLink, guestRemote} {}
    dwe::net::LoopbackNetwork network;
    coop::CoopLink hostLink;
    coop::CoopLink guestLink;
    dwe::app::NetworkFieldController hostRemote;
    dwe::app::NetworkFieldController guestRemote;
    dwe::app::CoopGameplayBridge hostBridge;
    dwe::app::CoopGameplayBridge guestBridge;
  };
  Seat one{identity, coop::GuestPlayerId};
  Seat two{identity, Second};
  const auto seatOf = [&](coop::PlayerId id) -> Seat& {
    return id == coop::GuestPlayerId ? one : two;
  };
  const auto playerOf = [&](coop::PlayerId id) -> Player& {
    return id == coop::GuestPlayerId ? first : second;
  };
  coop::TradeBroker broker;
  const std::array places{
      coop::TradePlace{.player = coop::HostPlayerId, .map = "town", .busy = false},
      coop::TradePlace{.player = coop::GuestPlayerId, .map = "town", .busy = false},
      coop::TradePlace{.player = Second, .map = "town", .busy = false}};
  const auto route = [&](const std::vector<coop::TradeMail>& mails) {
    for (const coop::TradeMail& mail : mails) {
      REQUIRE(mail.to != coop::HostPlayerId);
      REQUIRE(seatOf(mail.to).hostBridge.send(mail.message));
    }
  };
  const auto from = [&](coop::PlayerId id, const std::optional<coop::Message>& message) {
    if (message) {
      REQUIRE(seatOf(id).guestBridge.send(*message));
    }
  };
  const auto pump = [&] {
    for (int step = 0; step < 20; ++step) {
      for (const coop::PlayerId id : {coop::GuestPlayerId, Second}) {
        Seat& seat = seatOf(id);
        seat.network.advance(dwe::net::Millis{1});
        seat.hostBridge.poll(seat.network.now());
        seat.guestBridge.poll(seat.network.now());
        while (auto message = seat.hostBridge.receiveTrade()) {
          route(broker.receive(id, *message, places));
        }
        Player& player = playerOf(id);
        while (auto message = seat.guestBridge.receiveTrade()) {
          for (const coop::Message& reply : player.party.receive(*message, player.trades)) {
            REQUIRE(seat.guestBridge.send(reply));
          }
        }
      }
    }
  };
  pump();
  REQUIRE(one.guestLink.phase() == coop::LinkPhase::Ready);
  REQUIRE(two.guestLink.phase() == coop::LinkPhase::Ready);

  from(coop::GuestPlayerId, first.party.ask(Second));
  pump();
  REQUIRE(second.party.state());
  CHECK(second.party.state()->players[0] == coop::GuestPlayerId);
  from(Second, second.party.answer(true));
  pump();
  from(coop::GuestPlayerId, first.party.offer({{.item = std::string{items::Tonic}, .count = 2}}));
  from(Second, second.party.offer({{.item = std::string{items::Bandage}, .count = 3}}));
  pump();
  REQUIRE(first.party.canConfirm());
  REQUIRE(second.party.canConfirm());
  from(coop::GuestPlayerId, first.party.confirm(true));
  from(Second, second.party.confirm(true));
  pump();

  CHECK(first.count(items::Tonic) == 0);
  CHECK(first.count(items::Bandage) == 3);
  CHECK(second.count(items::Tonic) == 2);
  CHECK(second.count(items::Bandage) == 0);
  for (Player* player : {&first, &second}) {
    const auto closed = player->party.takeClosed();
    REQUIRE(closed);
    CHECK(closed->outcome == coop::TradeOutcome::Done);
  }
  CHECK(broker.open() == 0);
}
