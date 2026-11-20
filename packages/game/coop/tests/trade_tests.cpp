#include <dwe/game/coop/link.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/coop/trade.hpp>
#include <dwe/net/loopback.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace dwe::game::coop;

namespace {

// A bag of item counts with room for a few kinds of item (a stand-in for the game's inventory).
class TestBag final : public TradeBag {
public:
  explicit TestBag(std::map<std::string, int> items, std::size_t kinds = 10)
      : items_{std::move(items)}, kinds_{kinds} {}

  [[nodiscard]] TradeCheck check(std::span<const TradeItem> give,
                                 std::span<const TradeItem> get) const override {
    auto trial = items_;
    return swapOn(trial, give, get);
  }
  TradeCheck apply(std::span<const TradeItem> give, std::span<const TradeItem> get) override {
    auto trial = items_;
    const TradeCheck check = swapOn(trial, give, get);
    if (check == TradeCheck::Ok) {
      before_ = std::exchange(items_, std::move(trial));
      ++applied;
    }
    return check;
  }
  void undo() override {
    items_ = before_;
    ++undone;
  }

  [[nodiscard]] const std::map<std::string, int>& items() const { return items_; }
  // Something else changed the bag between the check and the swap.
  void set(const std::string& item, int count) { items_[item] = count; }

  int applied = 0;
  int undone = 0;

private:
  TradeCheck swapOn(std::map<std::string, int>& trial, std::span<const TradeItem> give,
                    std::span<const TradeItem> get) const {
    for (const TradeItem& item : give) {
      if (item.item == "key") {
        return TradeCheck::Untradeable;
      }
      auto found = trial.find(item.item);
      if (found == trial.end() || found->second < item.count) {
        return TradeCheck::Missing;
      }
      found->second -= item.count;
      if (found->second == 0) {
        trial.erase(found);
      }
    }
    for (const TradeItem& item : get) {
      trial[item.item] += item.count;
    }
    return trial.size() > kinds_ ? TradeCheck::NoRoom : TradeCheck::Ok;
  }

  std::map<std::string, int> items_;
  std::map<std::string, int> before_;
  std::size_t kinds_;
};

// The host's broker and up to four players' parties and bags, wired as the app wires them: the
// parties' messages go to the broker, its mails to their party, and their replies back, until
// nothing is left in flight. `held` keeps mails for a player instead (a slow link).
struct Session {
  TradeBroker broker;
  std::vector<TradePlace> places;
  std::map<PlayerId, TradeParty> parties;
  std::map<PlayerId, TestBag> bags;
  std::map<PlayerId, std::vector<Message>> held;
  std::optional<PlayerId> holding;

  void join(PlayerId id, std::map<std::string, int> items, std::size_t kinds = 10) {
    places.push_back({.player = id, .map = "town"});
    parties.emplace(id, TradeParty{id});
    bags.emplace(std::piecewise_construct, std::forward_as_tuple(id),
                 std::forward_as_tuple(std::move(items), kinds));
  }
  void drop(PlayerId id) {
    std::erase_if(places, [&](const TradePlace& place) { return place.player == id; });
    parties.erase(id);
    held.erase(id);
    deliver(broker.leave(id));
  }
  TradePlace& place(PlayerId id) {
    for (TradePlace& place : places) {
      if (place.player == id) {
        return place;
      }
    }
    FAIL("no such player");
    return places.front();
  }
  void send(PlayerId from, const std::optional<Message>& message) {
    REQUIRE(message.has_value());
    deliver(broker.receive(from, *message, places));
  }
  void deliver(std::vector<TradeMail> mails) {
    for (TradeMail& mail : mails) {
      if (holding == mail.to) {
        held[mail.to].push_back(std::move(mail.message));
        continue;
      }
      auto party = parties.find(mail.to);
      if (party == parties.end()) {
        continue;
      }
      for (const Message& reply : party->second.receive(mail.message, bags.at(mail.to))) {
        deliver(broker.receive(mail.to, reply, places));
      }
    }
  }
  // Hands a held player its mails; `keepHolding` holds what they bring about for it again.
  void release(PlayerId id, bool keepHolding = false) {
    holding = keepHolding ? std::optional{id} : std::nullopt;
    for (const Message& message : std::exchange(held[id], {})) {
      for (const Message& reply : party(id).receive(message, bag(id))) {
        deliver(broker.receive(id, reply, places));
      }
    }
  }
  void tick() { deliver(broker.tick(places)); }
  TradeParty& party(PlayerId id) { return parties.at(id); }
  TestBag& bag(PlayerId id) { return bags.at(id); }

  // From the ask to both confirmations of these offers.
  void open(PlayerId asker, PlayerId asked) {
    send(asker, party(asker).ask(asked));
    send(asked, party(asked).answer(true));
  }
  // Both offers and the first confirmation; `holdSecond` holds the second player's mails before
  // its confirmation freezes the table.
  void agree(PlayerId first, std::vector<TradeItem> firstGives, PlayerId second,
             std::vector<TradeItem> secondGives, bool holdSecond = false) {
    if (!firstGives.empty()) {
      send(first, party(first).offer(std::move(firstGives)));
    }
    if (!secondGives.empty()) {
      send(second, party(second).offer(std::move(secondGives)));
    }
    send(first, party(first).confirm(true));
    if (holdSecond) {
      holding = second;
    }
    send(second, party(second).confirm(true));
  }
};

std::optional<TradeOutcome> outcome(TradeParty& party) {
  const auto closed = party.takeClosed();
  return closed ? std::optional{closed->outcome} : std::nullopt;
}

} // namespace

TEST_CASE("two players ask, offer, confirm and swap their items at once") {
  Session session;
  session.join(HostPlayerId, {{"meat", 5}});
  session.join(GuestPlayerId, {{"disk", 2}, {"apple", 1}});

  session.send(GuestPlayerId, session.party(GuestPlayerId).ask(HostPlayerId));
  REQUIRE(session.party(HostPlayerId).state());
  CHECK(session.party(HostPlayerId).state()->phase == TradePhase::Asked);
  CHECK(session.party(HostPlayerId).side() == 1U);
  CHECK_FALSE(session.party(GuestPlayerId).asking());
  // Only the asked player answers.
  CHECK_FALSE(session.party(GuestPlayerId).answer(true));
  session.send(HostPlayerId, session.party(HostPlayerId).answer(true));
  CHECK(session.party(GuestPlayerId).state()->phase == TradePhase::Open);

  session.agree(GuestPlayerId, {{.item = "disk", .count = 2}}, HostPlayerId,
                {{.item = "meat", .count = 3}});

  CHECK(outcome(session.party(GuestPlayerId)) == TradeOutcome::Done);
  CHECK(outcome(session.party(HostPlayerId)) == TradeOutcome::Done);
  CHECK(session.bag(GuestPlayerId).items() ==
        std::map<std::string, int>{{"apple", 1}, {"meat", 3}});
  CHECK(session.bag(HostPlayerId).items() == std::map<std::string, int>{{"disk", 2}, {"meat", 2}});
  CHECK_FALSE(session.party(GuestPlayerId).state());
  CHECK(session.broker.open() == 0);
}

TEST_CASE("changing an offer clears both confirmations; a stale one is not taken") {
  Session session;
  session.join(HostPlayerId, {{"meat", 5}});
  session.join(GuestPlayerId, {{"disk", 2}});
  session.open(HostPlayerId, GuestPlayerId);
  session.send(HostPlayerId, session.party(HostPlayerId).offer({{.item = "meat", .count = 1}}));
  session.send(HostPlayerId, session.party(HostPlayerId).confirm(true));
  CHECK(session.party(GuestPlayerId).state()->confirmed[0]);

  const std::uint32_t seen = session.party(HostPlayerId).state()->revision;
  session.send(GuestPlayerId, session.party(GuestPlayerId).offer({{.item = "disk", .count = 1}}));
  const auto& state = *session.party(HostPlayerId).state();
  CHECK(state.revision == seen + 1);
  CHECK(state.confirmed == std::array{false, false});
  CHECK(state.offers[1] == std::vector<TradeItem>{{.item = "disk", .count = 1}});

  // A confirmation of the table before the change does nothing.
  session.deliver(session.broker.receive(
      HostPlayerId, TradeConfirm{.trade = state.trade, .revision = seen, .confirmed = true},
      session.places));
  CHECK_FALSE(session.broker.tradeOf(HostPlayerId)->confirmed[0]);
  // Nor does an offer while one is on the way confirm before the host shows it.
  session.holding = HostPlayerId;
  session.send(HostPlayerId, session.party(HostPlayerId).offer({{.item = "meat", .count = 2}}));
  CHECK_FALSE(session.party(HostPlayerId).canConfirm());
  session.release(HostPlayerId);
  CHECK(session.party(HostPlayerId).canConfirm());
  CHECK(session.bag(HostPlayerId).applied == 0);
}

TEST_CASE("an ask is refused, called off, or turned down for a busy or faraway player") {
  Session session;
  session.join(HostPlayerId, {});
  session.join(1, {});
  session.join(2, {});

  SUBCASE("refused") {
    session.send(1, session.party(1).ask(HostPlayerId));
    session.send(HostPlayerId, session.party(HostPlayerId).answer(false));
    CHECK(outcome(session.party(1)) == TradeOutcome::Refused);
    CHECK(outcome(session.party(HostPlayerId)) == TradeOutcome::Refused);
  }
  SUBCASE("the asker calls it off") {
    session.send(1, session.party(1).ask(HostPlayerId));
    session.send(1, session.party(1).cancel());
    CHECK(outcome(session.party(HostPlayerId)) == TradeOutcome::Cancelled);
  }
  SUBCASE("the asker calls it off before the host numbers it") {
    session.holding = 1;
    session.send(1, session.party(1).ask(HostPlayerId));
    CHECK_FALSE(session.party(1).cancel());
    session.release(1);
    CHECK(outcome(session.party(1)) == TradeOutcome::Cancelled);
    CHECK(session.broker.open() == 0);
  }
  SUBCASE("the other player is in a trade already") {
    session.open(HostPlayerId, 1);
    session.send(2, session.party(2).ask(1));
    const auto closed = session.party(2).takeClosed();
    REQUIRE(closed);
    CHECK(closed->outcome == TradeOutcome::Busy);
    CHECK(closed->by == 1);
    // A player in a trade cannot ask for another.
    CHECK_FALSE(session.party(1).ask(2));
  }
  SUBCASE("on another map, or in a battle") {
    session.place(2).map = "forest";
    session.send(1, session.party(1).ask(2));
    CHECK(outcome(session.party(1)) == TradeOutcome::Apart);
    session.place(2).map = "town";
    session.place(2).busy = true;
    session.send(1, session.party(1).ask(2));
    CHECK(outcome(session.party(1)) == TradeOutcome::Busy);
  }
  SUBCASE("oneself") {
    CHECK_FALSE(session.party(1).ask(1));
  }
}

TEST_CASE("a bag that cannot take the swap closes the trade with both bags untouched") {
  Session session;
  session.join(HostPlayerId, {{"meat", 5}});
  // The guest's bag holds two kinds and has room for no more.
  session.join(GuestPlayerId, {{"disk", 2}, {"apple", 1}}, 2);
  session.open(GuestPlayerId, HostPlayerId);

  SUBCASE("no room") {
    session.agree(GuestPlayerId, {{.item = "disk", .count = 1}}, HostPlayerId,
                  {{.item = "meat", .count = 1}});
    const auto closed = session.party(HostPlayerId).takeClosed();
    REQUIRE(closed);
    CHECK(closed->outcome == TradeOutcome::NoRoom);
    CHECK(closed->by == GuestPlayerId);
  }
  SUBCASE("the items are gone by the time it checks") {
    session.bag(GuestPlayerId).set("disk", 0);
    session.agree(GuestPlayerId, {{.item = "disk", .count = 2}}, HostPlayerId,
                  {{.item = "meat", .count = 1}});
    CHECK(outcome(session.party(HostPlayerId)) == TradeOutcome::Missing);
  }
  SUBCASE("an item the game does not let go") {
    session.agree(GuestPlayerId, {{.item = "key", .count = 1}}, HostPlayerId,
                  {{.item = "meat", .count = 1}});
    CHECK(outcome(session.party(HostPlayerId)) == TradeOutcome::Untradeable);
  }
  CHECK(session.bag(HostPlayerId).applied == 0);
  CHECK(session.bag(GuestPlayerId).applied == 0);
  CHECK(session.bag(HostPlayerId).items() == std::map<std::string, int>{{"meat", 5}});
}

TEST_CASE("a side that cannot apply the swap makes the other undo it") {
  Session session;
  session.join(HostPlayerId, {{"meat", 5}});
  session.join(GuestPlayerId, {{"disk", 2}});
  session.open(GuestPlayerId, HostPlayerId);
  // The host's side checks and applies at once; the guest's commit arrives after its bag changed.
  session.agree(HostPlayerId, {{.item = "meat", .count = 1}}, GuestPlayerId,
                {{.item = "disk", .count = 2}}, true);
  session.release(GuestPlayerId, true); // the prepare: it can
  CHECK(session.bag(HostPlayerId).applied == 1);
  session.bag(GuestPlayerId).set("disk", 1);
  session.release(GuestPlayerId); // the commit: it cannot any more
  CHECK(session.bag(HostPlayerId).undone == 1);
  CHECK(session.bag(HostPlayerId).items() == std::map<std::string, int>{{"meat", 5}});
  CHECK(session.bag(GuestPlayerId).applied == 0);
  CHECK(outcome(session.party(HostPlayerId)) == TradeOutcome::Missing);
}

TEST_CASE("leaving cancels a trade; after the commit the one who stays undoes the swap") {
  Session session;
  session.join(HostPlayerId, {{"meat", 5}});
  session.join(GuestPlayerId, {{"disk", 2}});
  session.open(HostPlayerId, GuestPlayerId);

  SUBCASE("while the table is open") {
    session.send(HostPlayerId, session.party(HostPlayerId).offer({{.item = "meat", .count = 1}}));
    session.drop(GuestPlayerId);
    const auto closed = session.party(HostPlayerId).takeClosed();
    REQUIRE(closed);
    CHECK(closed->outcome == TradeOutcome::Left);
    CHECK(closed->by == GuestPlayerId);
    CHECK(session.bag(HostPlayerId).applied == 0);
  }
  SUBCASE("after the commit, before the guest says it applied it") {
    session.agree(HostPlayerId, {{.item = "meat", .count = 1}}, GuestPlayerId,
                  {{.item = "disk", .count = 1}}, true);
    session.release(GuestPlayerId, true);
    // The commit is on its way to the guest, which drops.
    REQUIRE(session.held[GuestPlayerId].size() == 1);
    CHECK(session.bag(HostPlayerId).applied == 1);
    session.drop(GuestPlayerId);
    CHECK(session.bag(HostPlayerId).undone == 1);
    CHECK(session.bag(HostPlayerId).items() == std::map<std::string, int>{{"meat", 5}});
    CHECK(outcome(session.party(HostPlayerId)) == TradeOutcome::Left);
  }
  SUBCASE("a guest whose host is gone keeps its side as it is and closes") {
    session.parties.at(GuestPlayerId).hostLeft();
    CHECK(outcome(session.party(GuestPlayerId)) == TradeOutcome::Left);
    CHECK_FALSE(session.party(GuestPlayerId).state());
  }
  CHECK(session.broker.open() == (session.parties.contains(GuestPlayerId) ? 1U : 0U));
}

TEST_CASE("once frozen a trade cannot be changed or called off, and applies only once") {
  Session session;
  session.join(HostPlayerId, {{"meat", 5}});
  session.join(GuestPlayerId, {{"disk", 2}});
  session.open(HostPlayerId, GuestPlayerId);
  session.agree(HostPlayerId, {{.item = "meat", .count = 1}}, GuestPlayerId,
                {{.item = "disk", .count = 1}}, true);
  const std::uint32_t trade = session.broker.tradeOf(HostPlayerId)->trade;
  CHECK(session.broker.tradeOf(HostPlayerId)->phase == TradePhase::Executing);
  CHECK_FALSE(session.party(HostPlayerId).cancel());
  session.deliver(session.broker.receive(HostPlayerId, TradeCancel{trade}, session.places));
  session.deliver(session.broker.receive(
      HostPlayerId, TradeOffer{.trade = trade, .items = {{.item = "meat", .count = 4}}},
      session.places));
  CHECK(session.broker.tradeOf(HostPlayerId)->offers[0] ==
        std::vector<TradeItem>{{.item = "meat", .count = 1}});
  session.release(GuestPlayerId);
  CHECK(outcome(session.party(GuestPlayerId)) == TradeOutcome::Done);
  // A commit repeated after the close changes nothing.
  for (const Message& reply :
       session.party(GuestPlayerId).receive(TradeCommit{trade}, session.bag(GuestPlayerId))) {
    session.deliver(session.broker.receive(GuestPlayerId, reply, session.places));
  }
  CHECK(session.bag(GuestPlayerId).applied == 1);
  CHECK(session.bag(HostPlayerId).applied == 1);
  REQUIRE(session.broker.closed(trade));
  CHECK(session.broker.closed(trade)->outcome == TradeOutcome::Done);
}

TEST_CASE("a trade closes when its players go apart or into a battle") {
  Session session;
  session.join(HostPlayerId, {{"meat", 5}});
  session.join(GuestPlayerId, {{"disk", 2}});
  session.open(HostPlayerId, GuestPlayerId);
  session.tick();
  CHECK(session.broker.open() == 1);
  SUBCASE("apart") {
    session.place(HostPlayerId).map = "forest";
    session.tick();
    CHECK(outcome(session.party(GuestPlayerId)) == TradeOutcome::Apart);
  }
  SUBCASE("between maps") {
    session.place(GuestPlayerId).map.clear();
    session.tick();
    CHECK(outcome(session.party(GuestPlayerId)) == TradeOutcome::Apart);
  }
  SUBCASE("a battle") {
    session.place(GuestPlayerId).busy = true;
    session.tick();
    CHECK(outcome(session.party(HostPlayerId)) == TradeOutcome::Busy);
  }
  CHECK(session.broker.open() == 0);
}

TEST_CASE("four players run two trades at once, the host refereeing guests' trades too") {
  Session session;
  session.join(HostPlayerId, {{"meat", 5}});
  session.join(1, {{"disk", 2}});
  session.join(2, {{"apple", 3}});
  session.join(3, {{"chip", 1}});
  session.open(1, 2);
  session.open(HostPlayerId, 3);
  CHECK(session.broker.open() == 2);
  session.agree(1, {{.item = "disk", .count = 2}}, 2, {{.item = "apple", .count = 1}});
  session.agree(HostPlayerId, {}, 3, {{.item = "chip", .count = 1}});
  CHECK(outcome(session.party(1)) == TradeOutcome::Done);
  CHECK(outcome(session.party(3)) == TradeOutcome::Done);
  CHECK(session.bag(1).items() == std::map<std::string, int>{{"apple", 1}});
  CHECK(session.bag(2).items() == std::map<std::string, int>{{"apple", 2}, {"disk", 2}});
  CHECK(session.bag(HostPlayerId).items() == std::map<std::string, int>{{"chip", 1}, {"meat", 5}});
  CHECK(session.bag(3).items().empty());
}

TEST_CASE("a player's messages for a trade that is not theirs do nothing") {
  Session session;
  session.join(HostPlayerId, {{"meat", 5}});
  session.join(1, {{"disk", 2}});
  session.join(2, {});
  session.open(HostPlayerId, 1);
  const std::uint32_t trade = session.broker.tradeOf(HostPlayerId)->trade;
  CHECK(session.broker
            .receive(2, TradeOffer{.trade = trade, .items = {{.item = "x", .count = 1}}},
                     session.places)
            .empty());
  CHECK(session.broker.receive(2, TradeCancel{trade}, session.places).empty());
  // Nothing to confirm on an empty table.
  CHECK_FALSE(session.party(HostPlayerId).confirm(true));
  CHECK(session.broker.open() == 1);
}

TEST_CASE("trade messages round trip and damaged ones are refused") {
  const auto roundTrip = [](const Message& message) {
    const auto decoded = decode(encode(message));
    REQUIRE(decoded);
    REQUIRE(decoded->has_value());
    CHECK(**decoded == message);
  };
  roundTrip(TradeAsk{.with = 3});
  roundTrip(TradeAnswer{.trade = 7, .accept = true});
  roundTrip(TradeOffer{.trade = 7,
                       .items = {{.item = "meat", .count = 3}, {.item = "disk", .count = 1}}});
  roundTrip(TradeConfirm{.trade = 7, .revision = 4, .confirmed = true});
  roundTrip(TradeCancel{7});
  roundTrip(TradeState{.trade = 7,
                       .players = {2, 0},
                       .phase = TradePhase::Executing,
                       .revision = 9,
                       .offers = {std::vector<TradeItem>{{.item = "meat", .count = 99}}, {}},
                       .confirmed = {true, true}});
  roundTrip(TradePrepare{.trade = 7, .give = {{.item = "meat", .count = 1}}, .get = {}});
  roundTrip(TradeCommit{7});
  roundTrip(TradeRevert{7});
  roundTrip(TradeReply{.trade = 7, .step = TradeStep::Applied, .check = TradeCheck::NoRoom});
  roundTrip(TradeClosed{.trade = 7, .outcome = TradeOutcome::Untradeable, .by = 3});

  const auto refused = [](const Message& message) {
    const auto decoded = decode(encode(message));
    CHECK_FALSE(decoded);
  };
  refused(TradeOffer{.trade = 7,
                     .items = {{.item = "meat", .count = 1}, {.item = "meat", .count = 2}}});
  refused(TradeOffer{.trade = 7, .items = {{.item = "meat", .count = 0}}});
  refused(
      TradeOffer{.trade = 7,
                 .items = std::vector<TradeItem>(MaxTradeItems + 1, {.item = "meat", .count = 1})});
  refused(TradeAnswer{.trade = 0, .accept = true});
  TradeState twice;
  twice.trade = 7;
  twice.players = {1, 1};
  refused(twice);
  refused(TradeAsk{.with = 9});
  CHECK_FALSE(validOffer(std::vector<TradeItem>{{.item = "", .count = 1}}));
}

TEST_CASE("trade: each message goes only the way its side may send it over the link") {
  // link.cpp (directionAllowed) refuses a kind with no rule; a real game sends every trade message
  // through it.
  dwe::net::LoopbackNetwork network({});
  const Identity identity{.protocol = ProtocolVersion, .gameVersion = "1", .contentHash = "h"};
  CoopLink host = CoopLink::host(network.link(0), identity);
  CoopLink guest = CoopLink::guest(network.link(1), identity, "Guest");
  for (int i = 0; i < 8; ++i) {
    network.advance(dwe::net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  REQUIRE(guest.phase() == LinkPhase::Ready);
  while (host.receive() || guest.receive()) {
  }
  const std::vector<TradeItem> items{{.item = "meat", .count = 2}};
  // A player's own moves: to the host.
  const std::array<Message, 6> toHost{
      Message{TradeAsk{.with = HostPlayerId}},
      Message{TradeAnswer{.trade = 1, .accept = true}},
      Message{TradeOffer{.trade = 1, .items = items}},
      Message{TradeConfirm{.trade = 1, .revision = 2, .confirmed = true}},
      Message{TradeCancel{.trade = 1}},
      Message{TradeReply{.trade = 1, .step = TradeStep::Applied, .check = TradeCheck::Ok}}};
  // The referee's: to the guests.
  const std::array<Message, 5> toGuest{
      Message{TradeState{.trade = 1,
                         .players = {HostPlayerId, GuestPlayerId},
                         .phase = TradePhase::Open,
                         .revision = 2,
                         .offers = {items, {}},
                         .confirmed = {true, false}}},
      Message{TradePrepare{.trade = 1, .give = items, .get = {}}}, Message{TradeCommit{.trade = 1}},
      Message{TradeRevert{.trade = 1}},
      Message{TradeClosed{.trade = 1, .outcome = TradeOutcome::Done, .by = GuestPlayerId}}};
  for (const Message& message : toGuest) {
    CHECK_FALSE(guest.send(message));
    CHECK(host.send(message));
  }
  for (const Message& message : toHost) {
    CHECK_FALSE(host.send(message));
    CHECK(guest.send(message));
  }
  // Both ends read what crossed, in order.
  for (int i = 0; i < 8; ++i) {
    network.advance(dwe::net::Millis{1});
    host.poll(network.now());
    guest.poll(network.now());
  }
  std::size_t atGuest = 0;
  while (auto message = guest.receive()) {
    CHECK(*message == toGuest.at(atGuest++));
  }
  std::size_t atHost = 0;
  while (auto message = host.receive()) {
    CHECK(*message == toHost.at(atHost++));
  }
  CHECK(atGuest == toGuest.size());
  CHECK(atHost == toHost.size());
}
