#include <dwe/game/coop/trade.hpp>

#include <algorithm>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

namespace dwe::game::coop {

namespace {

// How many closed trades the broker remembers.
constexpr std::size_t TradeHistory = 32;

const TradePlace* placeOf(TradeWorld world, PlayerId player) {
  const auto found = std::ranges::find(world, player, &TradePlace::player);
  return found == world.end() ? nullptr : &*found;
}

TradeOutcome outcomeOf(TradeCheck check) {
  switch (check) {
  case TradeCheck::Ok:
    return TradeOutcome::Done;
  case TradeCheck::Missing:
    return TradeOutcome::Missing;
  case TradeCheck::Untradeable:
    return TradeOutcome::Untradeable;
  case TradeCheck::NoRoom:
    return TradeOutcome::NoRoom;
  }
  return TradeOutcome::Missing;
}

} // namespace

bool validOffer(std::span<const TradeItem> items) {
  if (items.size() > MaxTradeItems) {
    return false;
  }
  std::unordered_set<std::string> ids;
  return std::ranges::all_of(items, [&](const TradeItem& item) {
    return !item.item.empty() && item.count > 0 && item.count <= MaxTradeCount &&
           ids.insert(item.item).second;
  });
}

// ---- The host's broker ----

std::vector<TradeMail> TradeBroker::receive(PlayerId from, const Message& message,
                                            TradeWorld world) {
  std::vector<TradeMail> out;
  if (const auto* asked = std::get_if<TradeAsk>(&message)) {
    ask(from, *asked, world, out);
    return out;
  }
  const auto trade = std::visit(
      [](const auto& body) -> std::optional<std::uint32_t> {
        using T = std::decay_t<decltype(body)>;
        if constexpr (std::is_same_v<T, TradeAnswer> || std::is_same_v<T, TradeOffer> ||
                      std::is_same_v<T, TradeConfirm> || std::is_same_v<T, TradeCancel> ||
                      std::is_same_v<T, TradeReply>) {
          return body.trade;
        } else {
          return std::nullopt;
        }
      },
      message);
  std::size_t side = 0;
  Deal* deal = trade ? dealOf(*trade, from, side) : nullptr;
  if (deal == nullptr) {
    // Another player's trade, one closed already, or not a player's trade message at all.
    return out;
  }
  if (const auto* answered = std::get_if<TradeAnswer>(&message)) {
    answer(*deal, side, *answered, out);
  } else if (const auto* offered = std::get_if<TradeOffer>(&message)) {
    offer(*deal, side, *offered, out);
  } else if (const auto* confirmed = std::get_if<TradeConfirm>(&message)) {
    confirm(*deal, side, *confirmed, out);
  } else if (std::holds_alternative<TradeCancel>(message)) {
    // Once frozen the swap is under way: it ends on its own, done or undone.
    if (deal->state.phase != TradePhase::Executing) {
      close(*trade, TradeOutcome::Cancelled, from, out);
    }
  } else if (const auto* replied = std::get_if<TradeReply>(&message)) {
    reply(*deal, side, *replied, out);
  }
  return out;
}

std::vector<TradeMail> TradeBroker::tick(TradeWorld world) {
  std::vector<TradeMail> out;
  std::vector<std::pair<std::uint32_t, std::pair<TradeOutcome, PlayerId>>> closing;
  std::vector<PlayerId> gone;
  for (const auto& [trade, deal] : deals_) {
    const auto& players = deal.state.players;
    const TradePlace* first = placeOf(world, players[0]);
    const TradePlace* second = placeOf(world, players[1]);
    if (first == nullptr || second == nullptr) {
      gone.push_back(first == nullptr ? players[0] : players[1]);
      continue;
    }
    if (deal.state.phase == TradePhase::Executing) {
      continue;
    }
    if (first->busy || second->busy) {
      closing.push_back({trade, {TradeOutcome::Busy, first->busy ? players[0] : players[1]}});
    } else if (first->map.empty() || first->map != second->map) {
      closing.push_back({trade, {TradeOutcome::Apart, players[1]}});
    }
  }
  for (const auto& [trade, why] : closing) {
    close(trade, why.first, why.second, out);
  }
  for (const PlayerId player : gone) {
    auto left = leave(player);
    out.insert(out.end(), std::make_move_iterator(left.begin()),
               std::make_move_iterator(left.end()));
  }
  return out;
}

std::vector<TradeMail> TradeBroker::leave(PlayerId player) {
  std::vector<TradeMail> out;
  if (const Deal* deal = dealOf(player)) {
    close(deal->state.trade, TradeOutcome::Left, player, out, player);
  }
  return out;
}

std::optional<TradeState> TradeBroker::tradeOf(PlayerId player) const {
  for (const auto& [trade, deal] : deals_) {
    if (deal.state.players[0] == player || deal.state.players[1] == player) {
      return deal.state;
    }
  }
  return std::nullopt;
}

std::optional<TradeClosed> TradeBroker::closed(std::uint32_t trade) const {
  const auto found = std::ranges::find(history_, trade, &TradeClosed::trade);
  if (found == history_.end()) {
    return std::nullopt;
  }
  return *found;
}

TradeBroker::Deal* TradeBroker::dealOf(PlayerId player) {
  for (auto& [trade, deal] : deals_) {
    if (deal.state.players[0] == player || deal.state.players[1] == player) {
      return &deal;
    }
  }
  return nullptr;
}

TradeBroker::Deal* TradeBroker::dealOf(std::uint32_t trade, PlayerId from, std::size_t& side) {
  const auto found = deals_.find(trade);
  if (found == deals_.end()) {
    return nullptr;
  }
  const auto& players = found->second.state.players;
  if (players[0] == from) {
    side = 0;
  } else if (players[1] == from) {
    side = 1;
  } else {
    return nullptr;
  }
  return &found->second;
}

void TradeBroker::ask(PlayerId from, const TradeAsk& asked, TradeWorld world,
                      std::vector<TradeMail>& out) {
  const std::uint32_t trade = next_++;
  const TradePlace* asker = placeOf(world, from);
  const TradePlace* other = placeOf(world, asked.with);
  const auto refuse = [&](TradeOutcome outcome, PlayerId by) {
    const TradeClosed closed{.trade = trade, .outcome = outcome, .by = by};
    remember(closed);
    out.push_back({.to = from, .message = closed});
  };
  if (asker == nullptr) {
    return;
  }
  if (other == nullptr || asked.with == from) {
    refuse(TradeOutcome::Left, asked.with);
  } else if (dealOf(from) != nullptr || asker->busy) {
    refuse(TradeOutcome::Busy, from);
  } else if (dealOf(asked.with) != nullptr || other->busy) {
    refuse(TradeOutcome::Busy, asked.with);
  } else if (asker->map.empty() || asker->map != other->map) {
    refuse(TradeOutcome::Apart, asked.with);
  } else {
    Deal deal;
    deal.state.trade = trade;
    deal.state.players = {from, asked.with};
    deal.state.phase = TradePhase::Asked;
    broadcast(deals_.emplace(trade, std::move(deal)).first->second, out);
  }
}

void TradeBroker::answer(Deal& deal, std::size_t side, const TradeAnswer& answered,
                         std::vector<TradeMail>& out) {
  if (side != 1 || deal.state.phase != TradePhase::Asked) {
    return;
  }
  if (!answered.accept) {
    close(deal.state.trade, TradeOutcome::Refused, deal.state.players[1], out);
    return;
  }
  deal.state.phase = TradePhase::Open;
  deal.state.revision = 1;
  broadcast(deal, out);
}

void TradeBroker::offer(Deal& deal, std::size_t side, const TradeOffer& offered,
                        std::vector<TradeMail>& out) {
  if (deal.state.phase != TradePhase::Open || !validOffer(offered.items) ||
      deal.state.offers[side] == offered.items) {
    return;
  }
  deal.state.offers[side] = offered.items;
  // What either player confirmed is not on the table any more.
  ++deal.state.revision;
  deal.state.confirmed = {false, false};
  broadcast(deal, out);
}

void TradeBroker::confirm(Deal& deal, std::size_t side, const TradeConfirm& confirmed,
                          std::vector<TradeMail>& out) {
  TradeState& state = deal.state;
  if (state.phase != TradePhase::Open || confirmed.revision != state.revision ||
      state.confirmed[side] == confirmed.confirmed ||
      (confirmed.confirmed && state.offers[0].empty() && state.offers[1].empty())) {
    return;
  }
  state.confirmed[side] = confirmed.confirmed;
  if (!(state.confirmed[0] && state.confirmed[1])) {
    broadcast(deal, out);
    return;
  }
  // Both agreed on the same table: it freezes and each side checks the swap on its own bag.
  state.phase = TradePhase::Executing;
  broadcast(deal, out);
  for (std::size_t own = 0; own < 2; ++own) {
    out.push_back({.to = state.players[own],
                   .message = TradePrepare{.trade = state.trade,
                                           .give = state.offers[own],
                                           .get = state.offers[1 - own]}});
  }
}

void TradeBroker::reply(Deal& deal, std::size_t side, const TradeReply& replied,
                        std::vector<TradeMail>& out) {
  if (deal.state.phase != TradePhase::Executing) {
    return;
  }
  const std::uint32_t trade = deal.state.trade;
  const PlayerId player = deal.state.players[side];
  if (replied.step == TradeStep::Prepared) {
    if (deal.committed || deal.prepared[side]) {
      return;
    }
    deal.prepared[side] = replied.check;
    if (replied.check != TradeCheck::Ok) {
      close(trade, outcomeOf(replied.check), player, out);
    } else if (deal.prepared[1 - side] == TradeCheck::Ok) {
      deal.committed = true;
      for (const PlayerId to : deal.state.players) {
        out.push_back({.to = to, .message = TradeCommit{trade}});
      }
    }
    return;
  }
  if (!deal.committed || deal.applied[side]) {
    return;
  }
  deal.applied[side] = replied.check;
  if (replied.check != TradeCheck::Ok) {
    close(trade, outcomeOf(replied.check), player, out);
  } else if (deal.applied[1 - side] == TradeCheck::Ok) {
    close(trade, TradeOutcome::Done, deal.state.players[0], out);
  }
}

void TradeBroker::broadcast(const Deal& deal, std::vector<TradeMail>& out) const {
  for (const PlayerId to : deal.state.players) {
    out.push_back({.to = to, .message = deal.state});
  }
}

void TradeBroker::close(std::uint32_t trade, TradeOutcome outcome, PlayerId by,
                        std::vector<TradeMail>& out, std::optional<PlayerId> gone) {
  const auto found = deals_.find(trade);
  if (found == deals_.end()) {
    return;
  }
  const Deal& deal = found->second;
  for (std::size_t side = 0; side < 2; ++side) {
    const PlayerId player = deal.state.players[side];
    if (player == gone) {
      continue;
    }
    // A side told to apply the swap undoes it unless the swap is done or it said it could not
    // (then it changed nothing). The link keeps the order, so an undo always follows the commit.
    const bool mayHaveApplied =
        deal.committed && !(deal.applied[side] && *deal.applied[side] != TradeCheck::Ok);
    if (outcome != TradeOutcome::Done && mayHaveApplied) {
      out.push_back({.to = player, .message = TradeRevert{trade}});
    }
  }
  const TradeClosed closed{.trade = trade, .outcome = outcome, .by = by};
  for (const PlayerId player : deal.state.players) {
    if (player != gone) {
      out.push_back({.to = player, .message = closed});
    }
  }
  remember(closed);
  deals_.erase(found);
}

void TradeBroker::remember(const TradeClosed& closed) {
  history_.push_back(closed);
  while (history_.size() > TradeHistory) {
    history_.pop_front();
  }
}

// ---- A player's party ----

std::optional<Message> TradeParty::ask(PlayerId with) {
  if (state_ || asking_ || with == self_ || !validPlayer(with)) {
    return std::nullopt;
  }
  asking_ = with;
  cancelAsked_ = false;
  return TradeAsk{.with = with};
}

std::optional<Message> TradeParty::answer(bool accept) {
  if (!state_ || state_->phase != TradePhase::Asked || side() != 1U) {
    return std::nullopt;
  }
  return TradeAnswer{.trade = state_->trade, .accept = accept};
}

std::optional<Message> TradeParty::offer(std::vector<TradeItem> items) {
  const auto own = side();
  if (!state_ || !own || state_->phase != TradePhase::Open || !validOffer(items) ||
      items == draft_) {
    return std::nullopt;
  }
  draft_ = items;
  // As the host will: the table changed, so neither confirmation stands.
  state_->offers[*own] = items;
  state_->confirmed = {false, false};
  offerInFlight_ = true;
  return TradeOffer{.trade = state_->trade, .items = std::move(items)};
}

bool TradeParty::canConfirm() const {
  return state_ && side() && state_->phase == TradePhase::Open && !offerInFlight_ &&
         !(state_->offers[0].empty() && state_->offers[1].empty());
}

std::optional<Message> TradeParty::confirm(bool confirmed) {
  const auto own = side();
  if (!canConfirm() || state_->confirmed[*own] == confirmed) {
    return std::nullopt;
  }
  state_->confirmed[*own] = confirmed;
  return TradeConfirm{.trade = state_->trade, .revision = state_->revision, .confirmed = confirmed};
}

std::optional<Message> TradeParty::cancel() {
  if (state_ && state_->phase != TradePhase::Executing) {
    return TradeCancel{.trade = state_->trade};
  }
  if (!state_ && asking_) {
    // The host has not numbered the ask yet: it is called off as soon as it does.
    cancelAsked_ = true;
  }
  return std::nullopt;
}

std::vector<Message> TradeParty::receive(const Message& message, TradeBag& bag) {
  std::vector<Message> replies;
  if (const auto* state = std::get_if<TradeState>(&message)) {
    const std::size_t own = state->players[0] == self_ ? 0U : 1U;
    if (state->players[own] != self_ || (state_ && state_->trade != state->trade)) {
      return replies;
    }
    if (asking_ && own == 0) {
      asking_.reset();
    }
    // The host's table, except this player's own offer while its last change is on the way.
    const bool echoed = !offerInFlight_ || state->offers[own] == draft_;
    std::vector<TradeItem> keep = draft_;
    state_ = *state;
    if (echoed) {
      offerInFlight_ = false;
      draft_ = state->offers[own];
    } else {
      state_->offers[own] = std::move(keep);
      state_->confirmed = {false, false};
    }
    if (cancelAsked_ && state->phase != TradePhase::Executing) {
      cancelAsked_ = false;
      replies.emplace_back(TradeCancel{.trade = state->trade});
    }
  } else if (const auto* prepare = std::get_if<TradePrepare>(&message)) {
    if (!state_ || state_->trade != prepare->trade) {
      return replies;
    }
    prepared_ = *prepare;
    replies.emplace_back(TradeReply{.trade = prepare->trade,
                                    .step = TradeStep::Prepared,
                                    .check = bag.check(prepare->give, prepare->get)});
  } else if (const auto* commit = std::get_if<TradeCommit>(&message)) {
    TradeCheck check = TradeCheck::Ok;
    if (applied_ != commit->trade) {
      if (!prepared_ || prepared_->trade != commit->trade) {
        // Nothing to apply: this side never checked that swap.
        check = TradeCheck::Missing;
      } else {
        check = bag.apply(prepared_->give, prepared_->get);
        if (check == TradeCheck::Ok) {
          applied_ = commit->trade;
        }
      }
    }
    replies.emplace_back(
        TradeReply{.trade = commit->trade, .step = TradeStep::Applied, .check = check});
  } else if (const auto* revert = std::get_if<TradeRevert>(&message)) {
    if (applied_ == revert->trade) {
      bag.undo();
      applied_.reset();
    }
  } else if (const auto* closed = std::get_if<TradeClosed>(&message)) {
    const bool ours = state_ ? state_->trade == closed->trade : asking_.has_value();
    if (!ours) {
      return replies;
    }
    closed_ = *closed;
    state_.reset();
    asking_.reset();
    prepared_.reset();
    applied_.reset();
    draft_.clear();
    cancelAsked_ = false;
    offerInFlight_ = false;
  }
  return replies;
}

void TradeParty::hostLeft() {
  if (state_ || asking_) {
    closed_ = TradeClosed{
        .trade = state_ ? state_->trade : 0U, .outcome = TradeOutcome::Left, .by = HostPlayerId};
  }
  state_.reset();
  asking_.reset();
  prepared_.reset();
  applied_.reset();
  draft_.clear();
  cancelAsked_ = false;
  offerInFlight_ = false;
}

std::optional<std::size_t> TradeParty::side() const {
  if (!state_) {
    return std::nullopt;
  }
  if (state_->players[0] == self_) {
    return 0U;
  }
  if (state_->players[1] == self_) {
    return 1U;
  }
  return std::nullopt;
}

std::optional<TradeClosed> TradeParty::takeClosed() {
  return std::exchange(closed_, std::nullopt);
}

} // namespace dwe::game::coop
