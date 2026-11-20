#pragma once

#include <dwe/game/coop/protocol.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dwe::game::coop {

// Item trades between two players of the session (#360; docs/architecture/multiplayer.md, "Item
// trades"). The host's TradeBroker referees every trade; each player's TradeParty (the host's own
// too) keeps its side and checks and swaps the items on its own bag through a TradeBag. A bag is
// only ever written by its owner's game: the broker asks, it never moves items itself.
//
// A trade goes: ask (to a player on the same map, free of other trades and battles) → answer →
// both put items on the table (each change clears both confirmations) → both confirm the same
// revision → the broker freezes the table and asks each side to check the swap (prepare) → when
// both can, it tells both to apply it (commit) → when both applied it, done. A side that cannot
// check or apply it, a side that leaves, or a cancel before the freeze closes the trade with both
// bags as they were: a side that had applied it is told to undo it (revert).

// What the broker knows of a player of the session: the map it stands on (empty between maps) and
// whether it is busy (a battle).
struct TradePlace {
  PlayerId player = HostPlayerId;
  std::string map;
  bool busy = false;
};
using TradeWorld = std::span<const TradePlace>;

// A message for one player (the host's own party gets its own from the app, not the link).
struct TradeMail {
  PlayerId to = HostPlayerId;
  Message message;

  friend bool operator==(const TradeMail&, const TradeMail&) = default;
};

// Whether an offer is one the table takes: at most MaxTradeItems lines, each item once, 1 to
// MaxTradeCount of each.
[[nodiscard]] bool validOffer(std::span<const TradeItem> items);

// The host's referee. It holds no bag: it numbers the trades, keeps each table and judges every
// message by who sent it and the trade's phase. A player is in one trade at a time.
class TradeBroker {
public:
  // A trade message from player `from` (the host's own party's too), with the session's players
  // as they are now. What to send, and to whom.
  [[nodiscard]] std::vector<TradeMail> receive(PlayerId from, const Message& message,
                                               TradeWorld world);
  // Once per tick: trades not yet frozen whose players left, went apart or got busy close; a
  // frozen one whose player left closes too, the other side undoing what it applied.
  [[nodiscard]] std::vector<TradeMail> tick(TradeWorld world);
  // A player left the session (a Leave, a dropped link): its trade closes now.
  [[nodiscard]] std::vector<TradeMail> leave(PlayerId player);

  // The trade player `player` is in, as the table shows it.
  [[nodiscard]] std::optional<TradeState> tradeOf(PlayerId player) const;
  // How a past trade closed (the last few are kept, so a late or repeated message for it does
  // nothing); none for one open or unknown.
  [[nodiscard]] std::optional<TradeClosed> closed(std::uint32_t trade) const;
  [[nodiscard]] std::size_t open() const { return deals_.size(); }

private:
  struct Deal {
    TradeState state;
    // The sides' answers to the prepare and the commit, by side.
    std::array<std::optional<TradeCheck>, 2> prepared;
    std::array<std::optional<TradeCheck>, 2> applied;
    bool committed = false;
  };

  [[nodiscard]] Deal* dealOf(PlayerId player);
  [[nodiscard]] Deal* dealOf(std::uint32_t trade, PlayerId from, std::size_t& side);
  void ask(PlayerId from, const TradeAsk& ask, TradeWorld world, std::vector<TradeMail>& out);
  void answer(Deal& deal, std::size_t side, const TradeAnswer& answer, std::vector<TradeMail>& out);
  void offer(Deal& deal, std::size_t side, const TradeOffer& offer, std::vector<TradeMail>& out);
  void confirm(Deal& deal, std::size_t side, const TradeConfirm& confirm,
               std::vector<TradeMail>& out);
  void reply(Deal& deal, std::size_t side, const TradeReply& reply, std::vector<TradeMail>& out);
  void broadcast(const Deal& deal, std::vector<TradeMail>& out) const;
  // Closes the deal: the commit's undo to every side still there that may have applied it, then
  // the close to both. `gone` is a side that left (it gets nothing).
  void close(std::uint32_t trade, TradeOutcome outcome, PlayerId by, std::vector<TradeMail>& out,
             std::optional<PlayerId> gone = std::nullopt);
  void remember(const TradeClosed& closed);

  std::uint32_t next_ = 1;
  std::map<std::uint32_t, Deal> deals_;
  std::deque<TradeClosed> history_;
};

// A player's bag as a trade sees it (the app's inventory). `give` and `get` are ids and counts.
class TradeBag {
public:
  TradeBag() = default;
  virtual ~TradeBag() = default;
  TradeBag(const TradeBag&) = delete;
  TradeBag& operator=(const TradeBag&) = delete;
  TradeBag(TradeBag&&) = delete;
  TradeBag& operator=(TradeBag&&) = delete;

  // Whether the bag could swap them now (it changes nothing).
  [[nodiscard]] virtual TradeCheck check(std::span<const TradeItem> give,
                                         std::span<const TradeItem> get) const = 0;
  // Swaps them at once: all of it, or nothing when it cannot (the check's answer).
  virtual TradeCheck apply(std::span<const TradeItem> give, std::span<const TradeItem> get) = 0;
  // Undoes the last swap applied (the bag is held from the swap to the trade's close, so nothing
  // else touched those items).
  virtual void undo() = 0;
};

// One player's side of its trades: it turns what the player does into messages for the host and
// answers the host's prepares and commits on its own bag. The host's own party runs the same way,
// fed by the app instead of the link.
class TradeParty {
public:
  explicit TradeParty(PlayerId self) : self_{self} {}

  [[nodiscard]] PlayerId self() const { return self_; }

  // What the player does: each gives the message for the host, none when it makes no sense now
  // (an ask while in a trade, an offer outside the open table, a confirm of nothing...).
  [[nodiscard]] std::optional<Message> ask(PlayerId with);
  [[nodiscard]] std::optional<Message> answer(bool accept);
  // The player's whole offer (it shows at once; the host's next state confirms it).
  [[nodiscard]] std::optional<Message> offer(std::vector<TradeItem> items);
  [[nodiscard]] std::optional<Message> confirm(bool confirmed);
  [[nodiscard]] std::optional<Message> cancel();

  // A trade message from the host: the replies to send back.
  [[nodiscard]] std::vector<Message> receive(const Message& message, TradeBag& bag);
  // The host is gone: the trade with it (an applied swap stays: nothing more will come).
  void hostLeft();

  // The trade this player is in, as the host's last state showed it, with this player's own offer
  // as it last set it.
  [[nodiscard]] const std::optional<TradeState>& state() const { return state_; }
  // This player's side of the table (0 the asker, 1 the asked), when in a trade.
  [[nodiscard]] std::optional<std::size_t> side() const;
  [[nodiscard]] const std::vector<TradeItem>& ownOffer() const { return draft_; }
  // Whether a confirmation can be sent now (the open table, with this player's last offer in it).
  [[nodiscard]] bool canConfirm() const;
  // Waiting for the host to take its ask.
  [[nodiscard]] bool asking() const { return asking_.has_value(); }
  [[nodiscard]] std::optional<PlayerId> askingWhom() const { return asking_; }
  // Whether the bag is held (from the host's prepare to the close): nothing may change it.
  [[nodiscard]] bool holdsBag() const { return prepared_.has_value(); }
  // The trade that closed, once.
  [[nodiscard]] std::optional<TradeClosed> takeClosed();

private:
  PlayerId self_;
  std::optional<PlayerId> asking_;
  std::optional<TradeState> state_;
  std::vector<TradeItem> draft_;
  // The swap the host's prepare named, until the close; and the trade applied on the bag.
  std::optional<TradePrepare> prepared_;
  std::optional<std::uint32_t> applied_;
  std::optional<TradeClosed> closed_;
  // The player called off its ask before the host took it: the trade it gets is cancelled.
  bool cancelAsked_ = false;
  // An offer sent and not yet in a state from the host: a confirmation waits for it, so it never
  // confirms a revision older than what the player sees.
  bool offerInFlight_ = false;
};

} // namespace dwe::game::coop
