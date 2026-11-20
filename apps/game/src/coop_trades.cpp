// The item trades between players of a co-op game (#360; docs/architecture/multiplayer.md, "Item
// trades"): an engine addition, the original has no online play.
#include "coop_trades.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/bag_view.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

#include "coop_gameplay_bridge.hpp"
#include "play_session.hpp"

namespace dwe::app {

namespace coop = game::coop;

namespace {

// The text key of a closed trade's notice: the player it names is `by`, or this player itself.
std::string noticeKey(coop::TradeOutcome outcome, bool self) {
  switch (outcome) {
  case coop::TradeOutcome::Done:
    return "coop_trade.notice.done";
  case coop::TradeOutcome::Refused:
    return "coop_trade.notice.refused";
  case coop::TradeOutcome::Cancelled:
    return self ? "coop_trade.notice.cancelled_self" : "coop_trade.notice.cancelled";
  case coop::TradeOutcome::Left:
    return "coop_trade.notice.left";
  case coop::TradeOutcome::Busy:
    return self ? "coop_trade.notice.busy_self" : "coop_trade.notice.busy";
  case coop::TradeOutcome::Apart:
    return "coop_trade.notice.apart";
  case coop::TradeOutcome::Missing:
    return self ? "coop_trade.notice.missing_self" : "coop_trade.notice.missing";
  case coop::TradeOutcome::Untradeable:
    return self ? "coop_trade.notice.untradeable_self" : "coop_trade.notice.untradeable";
  case coop::TradeOutcome::NoRoom:
    return self ? "coop_trade.notice.no_room_self" : "coop_trade.notice.no_room";
  }
  return "coop_trade.notice.cancelled";
}

std::string_view phaseName(const coop::TradeParty& party) {
  if (!party.state()) {
    return "asking";
  }
  switch (party.state()->phase) {
  case coop::TradePhase::Asked:
    return party.side() == 1U ? "invited" : "asking";
  case coop::TradePhase::Open:
    return "open";
  case coop::TradePhase::Executing:
    return "executing";
  }
  return "open";
}

std::int32_t offered(std::span<const coop::TradeItem> offer, std::string_view item) {
  const auto found = std::ranges::find(offer, item, &coop::TradeItem::item);
  return found == offer.end() ? 0 : found->count;
}

void publishLabel(screens::ScreenSettings& settings, const CoopPeerLabel& label) {
  settings.push_back({.name = "coop_trade.other.name", .value = label.name});
  settings.push_back({.name = "coop_trade.other.name_key", .value = label.key});
  settings.push_back(
      {.name = "coop_trade.other.number", .value = static_cast<double>(label.number)});
}

// An item of a list the document shows: its id, name, count, colour and icon.
void publishItem(screens::ScreenSettings& settings, std::string_view list, std::size_t index,
                 const assets::ItemEntry* entry, std::string_view id, std::int32_t count,
                 const screens::NameKeys& names) {
  settings.push_back({.name = screens::listEntryName(list, index, "id"), .value = std::string{id}});
  settings.push_back(
      {.name = screens::listEntryName(list, index, "name_key"), .value = names.item(id)});
  settings.push_back(
      {.name = screens::listEntryName(list, index, "count"), .value = static_cast<double>(count)});
  settings.push_back({.name = screens::listEntryName(list, index, "item_color"),
                      .value = static_cast<double>(entry != nullptr ? entry->itemColor : 255)});
  const auto icon = entry != nullptr ? screens::itemIconOf(*entry) : std::nullopt;
  settings.push_back({.name = screens::listEntryName(list, index, "icon"),
                      .value = icon ? icon->image : std::string{}});
  settings.push_back({.name = screens::listEntryName(list, index, "icon_rect"),
                      .value = icon ? icon->rect : std::string{}});
}

const assets::ItemEntry* entryOf(const session::InventoryAdapter* inventory, std::string_view id) {
  if (inventory == nullptr) {
    return nullptr;
  }
  const auto handle = inventory->items().handleOf(id);
  return handle ? &inventory->items().at(*handle) : nullptr;
}

} // namespace

// ---- The trades ----

void CoopTrades::tick(PlaySession& play, std::span<const CoopTradeLink> links, Role role,
                      coop::PlayerId self, std::span<const coop::TradePlace> world,
                      const LabelOf& labelOf) {
  if (role == Role::None || links.empty()) {
    if (role_ != Role::None) {
      reset();
    }
    return;
  }
  role_ = role;
  world_.assign(world.begin(), world.end());
  if (!party_ || party_->self() != self) {
    party_.emplace(self);
  }
  bag_.attach(play.coopInventory());

  for (const CoopTradeLink& link : links) {
    while (auto message = link.bridge->receiveTrade()) {
      if (role_ == Role::Host) {
        // A guest speaks for itself only: the link says who sent it.
        toBroker(link.player, *message);
      } else {
        for (coop::Message& reply : party_->receive(*message, bag_)) {
          fromParty(std::move(reply));
        }
      }
    }
  }
  // A battle of this player's own (a guest's solo one, or one it watches) calls its trade off.
  const bool battling = play.battleScreen().active() || play.heldForCoopBattle();
  if (battling && party_->state() && party_->state()->phase != coop::TradePhase::Executing) {
    fromParty(party_->cancel());
  }
  if (role_ == Role::Host) {
    deliver(broker_.tick(world_));
  }
  flush(links);
  if (auto closed = party_->takeClosed()) {
    noticeOf(*closed);
  }
  publish(play, labelOf);
}

bool CoopTrades::handle(const ui::UiCommand& command) {
  const std::string_view name = command.name;
  if (name != CoopTradeAnswerCommand && name != CoopTradeAddCommand &&
      name != CoopTradeTakeCommand && name != CoopTradeConfirmCommand &&
      name != CoopTradeCancelCommand) {
    return false;
  }
  if (!party_) {
    return true;
  }
  const bool yes = std::holds_alternative<bool>(command.value) && std::get<bool>(command.value);
  const auto* item = std::get_if<std::string>(&command.value);
  if (name == CoopTradeAnswerCommand) {
    fromParty(party_->answer(yes));
  } else if (name == CoopTradeConfirmCommand) {
    fromParty(party_->confirm(yes));
  } else if (name == CoopTradeCancelCommand) {
    fromParty(party_->cancel());
  } else if (item != nullptr) {
    changeOffer(*item, name == CoopTradeAddCommand ? 1 : -1);
  }
  return true;
}

void CoopTrades::changeOffer(std::string_view item, std::int32_t step) {
  const session::InventoryAdapter* inventory = bag_.inventory();
  if (!party_ || !party_->state() || inventory == nullptr) {
    return;
  }
  std::vector<coop::TradeItem> offer = party_->ownOffer();
  auto found = std::ranges::find(offer, item, &coop::TradeItem::item);
  const std::int32_t now = found == offer.end() ? 0 : found->count;
  const auto handle = inventory->items().handleOf(item);
  const std::int32_t held = handle ? inventory->inventory().totalOf(*handle) : 0;
  // Only what the bag holds and lets go of goes on the table.
  const std::int32_t next =
      std::clamp(now + step, 0, std::min<std::int32_t>(held, coop::MaxTradeCount));
  if (next == now || (step > 0 && !tradeable(*inventory, item))) {
    return;
  }
  if (next == 0) {
    offer.erase(found);
  } else if (found != offer.end()) {
    found->count = static_cast<std::uint16_t>(next);
  } else if (offer.size() < coop::MaxTradeItems) {
    offer.push_back({.item = std::string{item}, .count = static_cast<std::uint16_t>(next)});
  } else {
    return;
  }
  fromParty(party_->offer(std::move(offer)));
}

void CoopTrades::press(const input::InputState& input, const PlaySession& play) {
  if (!party_ || role_ == Role::None || !input.wasPressed(CoopTradeAction) ||
      !play.fieldRunning()) {
    return;
  }
  if (party_->state() || party_->asking()) {
    return;
  }
  // Who is near is read where the frame draws the players (askNearest), as the tag's hint is.
  askPressed_ = true;
}

bool CoopTrades::askNearest(const PlaySession& play, coop::PlayerId tagged) {
  const bool free = party_ && !party_->state() && !party_->asking() && play.fieldRunning();
  const auto with = free ? nearest(play) : std::nullopt;
  if (std::exchange(askPressed_, false) && free) {
    if (with) {
      fromParty(party_->ask(*with));
      return false;
    }
    notice_ = Notice{.key = "coop_trade.notice.no_one_near", .player = party_->self()};
  }
  return with == tagged;
}

std::optional<coop::PlayerId> CoopTrades::nearest(const PlaySession& play) const {
  const FieldScene& scene = play.scene();
  const auto own = scene.field().player();
  std::optional<coop::PlayerId> best;
  float bestDistance = CoopTradeReach;
  for (const game::field::PlayerId id : scene.coopPlayers()) {
    if (party_ && id == party_->self()) {
      continue;
    }
    const auto pose = scene.coopDrawnPose(id);
    if (!pose) {
      continue;
    }
    const float distance =
        std::hypot(pose->position.x - own.position.x, pose->position.z - own.position.z);
    if (distance <= bestDistance) {
      bestDistance = distance;
      best = id;
    }
  }
  return best;
}

void CoopTrades::playerLeft(coop::PlayerId player) {
  if (role_ != Role::Host) {
    return;
  }
  std::erase_if(world_, [&](const coop::TradePlace& place) { return place.player == player; });
  deliver(broker_.leave(player));
  std::erase_if(outbox_, [&](const coop::TradeMail& mail) { return mail.to == player; });
  if (party_) {
    if (auto closed = party_->takeClosed()) {
      noticeOf(*closed);
    }
  }
}

void CoopTrades::hostLeft() {
  if (party_) {
    party_->hostLeft();
    static_cast<void>(party_->takeClosed());
  }
  ownOut_.clear();
  hideDocument();
}

void CoopTrades::reset() {
  role_ = Role::None;
  broker_ = {};
  world_.clear();
  party_.reset();
  bag_.attach(nullptr);
  outbox_.clear();
  ownOut_.clear();
  invitedNotice_.reset();
  askPressed_ = false;
  hideDocument();
}

void CoopTrades::toBroker(coop::PlayerId from, const coop::Message& message) {
  deliver(broker_.receive(from, message, world_));
}

void CoopTrades::deliver(std::vector<coop::TradeMail> mails) {
  for (coop::TradeMail& mail : mails) {
    if (party_ && mail.to == party_->self()) {
      // The host's own side: its replies go straight back to the broker.
      for (const coop::Message& reply : party_->receive(mail.message, bag_)) {
        toBroker(mail.to, reply);
      }
    } else {
      outbox_.push_back(std::move(mail));
    }
  }
}

void CoopTrades::fromParty(std::optional<coop::Message> message) {
  if (message) {
    ownOut_.push_back(std::move(*message));
  }
}

void CoopTrades::flush(std::span<const CoopTradeLink> links) {
  const auto send = [&](coop::PlayerId to, const coop::Message& message) {
    const auto link =
        std::ranges::find_if(links, [&](const CoopTradeLink& each) { return each.player == to; });
    if (link == links.end()) {
      core::logWarning("coop: no link to player {} for a trade message", to);
    } else if (auto sent = link->bridge->send(message); !sent) {
      core::logWarning("coop: could not send a trade message: {}", sent.error().message);
    }
  };
  for (const coop::Message& message : std::exchange(ownOut_, {})) {
    if (role_ == Role::Host) {
      toBroker(party_->self(), message);
    } else {
      send(coop::HostPlayerId, message);
    }
  }
  for (const coop::TradeMail& mail : std::exchange(outbox_, {})) {
    send(mail.to, mail.message);
  }
}

void CoopTrades::noticeOf(const coop::TradeClosed& closed) {
  const bool self = party_ && closed.by == party_->self();
  core::logInfo("coop: trade {} closed: {} (player {})", closed.trade,
                noticeKey(closed.outcome, self), closed.by);
  // A notice names the other player of the trade; its text says when it is about this one.
  notice_ = Notice{.key = noticeKey(closed.outcome, self),
                   .player = self || closed.outcome == coop::TradeOutcome::Done
                                 ? lastOther_.value_or(closed.by)
                                 : closed.by};
}

void CoopTrades::publish(const PlaySession& play, const LabelOf& labelOf) {
  if (!party_ || (!party_->state() && !party_->asking())) {
    hideDocument();
    return;
  }
  const auto& state = party_->state();
  const auto side = party_->side();
  const coop::PlayerId other =
      state && side ? state->players[1 - *side] : party_->askingWhom().value_or(coop::HostPlayerId);
  lastOther_ = other;
  const bool free = !play.coopBattleBlocked() && !play.heldForCoopBattle() &&
                    (documentShown_ || !ui_->capturesInput());
  if (!free) {
    // An ask for this player waits until it can see it; the HUD tells it once.
    if (state && state->phase == coop::TradePhase::Asked && side == 1U &&
        invitedNotice_ != state->trade) {
      invitedNotice_ = state->trade;
      notice_ = Notice{.key = "coop_trade.notice.invited", .player = other};
    }
    hideDocument();
    return;
  }
  const session::InventoryAdapter* inventory = play.coopInventory();
  const screens::NameKeys names = play.coopNameKeys();
  screens::ScreenSettings settings{
      {.name = "coop_trade.shown", .value = true},
      {.name = "coop_trade.phase", .value = std::string{phaseName(*party_)}},
  };
  publishLabel(settings, labelOf ? labelOf(other) : CoopPeerLabel{});
  const std::vector<coop::TradeItem>& own = party_->ownOffer();
  std::size_t listed = 0;
  if (inventory != nullptr) {
    std::vector<std::string> seen;
    for (const auto& stack : inventory->inventory().slots()) {
      if (!stack || stack->count <= 0) {
        continue;
      }
      const assets::ItemEntry& entry = inventory->items().at(stack->item);
      if (std::ranges::contains(seen, entry.id)) {
        continue;
      }
      seen.push_back(entry.id);
      const std::int32_t total = inventory->inventory().totalOf(stack->item);
      const std::int32_t onTable = offered(own, entry.id);
      publishItem(settings, "coop_trade.bag", listed, &entry, entry.id, total - onTable, names);
      settings.push_back({.name = screens::listEntryName("coop_trade.bag", listed, "offered"),
                          .value = static_cast<double>(onTable)});
      settings.push_back({.name = screens::listEntryName("coop_trade.bag", listed, "tradeable"),
                          .value = tradeable(*inventory, entry.id)});
      ++listed;
    }
  }
  settings.push_back({.name = "coop_trade.bag_count", .value = static_cast<double>(listed)});
  const auto publishOffer = [&](std::string_view list, std::span<const coop::TradeItem> offer) {
    for (std::size_t index = 0; index < offer.size(); ++index) {
      publishItem(settings, list, index, entryOf(inventory, offer[index].item), offer[index].item,
                  offer[index].count, names);
    }
    settings.push_back(
        {.name = std::string{list} + "_count", .value = static_cast<double>(offer.size())});
  };
  publishOffer("coop_trade.own", own);
  const std::vector<coop::TradeItem> none;
  publishOffer("coop_trade.their", state && side ? state->offers[1 - *side] : none);
  settings.push_back(
      {.name = "coop_trade.own_confirmed", .value = state && side && state->confirmed[*side]});
  settings.push_back({.name = "coop_trade.their_confirmed",
                      .value = state && side && state->confirmed[1 - *side]});
  settings.push_back({.name = "coop_trade.can_confirm", .value = party_->canConfirm()});
  screens::publish(*ui_, settings, CoopTradeRevisionSetting);
  if (!documentShown_ || !ui_->isVisible(CoopTradeDocument)) {
    ui_->show(CoopTradeDocument, true);
    documentShown_ = true;
  }
}

void CoopTrades::hideDocument() {
  if (!documentShown_) {
    return;
  }
  documentShown_ = false;
  ui_->setSetting("coop_trade.shown", false);
  ui_->hide(CoopTradeDocument);
}

} // namespace dwe::app
