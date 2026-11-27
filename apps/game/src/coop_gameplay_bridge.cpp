#include "coop_gameplay_bridge.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace dwe::app {

namespace {

bool isBattleMessage(const game::coop::Message& message) {
  return std::holds_alternative<game::coop::BattleAsk>(message) ||
         std::holds_alternative<game::coop::BattleBegin>(message) ||
         std::holds_alternative<game::coop::BattleRefused>(message) ||
         std::holds_alternative<game::coop::BattleFrame>(message) ||
         std::holds_alternative<game::coop::BattleAction>(message) ||
         std::holds_alternative<game::coop::BattleOffer>(message) ||
         std::holds_alternative<game::coop::BattleEnd>(message);
}

bool isTradeMessage(const game::coop::Message& message) {
  return std::visit(
      [](const auto& body) {
        using T = std::decay_t<decltype(body)>;
        return std::is_same_v<T, game::coop::TradeAsk> ||
               std::is_same_v<T, game::coop::TradeAnswer> ||
               std::is_same_v<T, game::coop::TradeOffer> ||
               std::is_same_v<T, game::coop::TradeConfirm> ||
               std::is_same_v<T, game::coop::TradeCancel> ||
               std::is_same_v<T, game::coop::TradeState> ||
               std::is_same_v<T, game::coop::TradePrepare> ||
               std::is_same_v<T, game::coop::TradeCommit> ||
               std::is_same_v<T, game::coop::TradeRevert> ||
               std::is_same_v<T, game::coop::TradeReply> ||
               std::is_same_v<T, game::coop::TradeClosed>;
      },
      message);
}

} // namespace

void CoopGameplayBridge::poll(net::Millis now) {
  if (!firstPolled_) {
    firstPolled_ = now;
  }
  link_->poll(now);
  remoteController_->setNow(now);
  while (auto message = link_->receive()) {
    if (const auto* commands = std::get_if<game::coop::Commands>(&*message)) {
      (void)commandWatch_.observe(commands->sequence);
      (void)remoteController_->receive(*commands, now);
    } else if (const auto* snapshot = std::get_if<game::coop::Snapshot>(&*message)) {
      (void)snapshotWatch_.observe(snapshot->worldTick);
      lastSnapshot_ = now;
      (void)snapshots_.push(*snapshot, now);
    } else if (std::holds_alternative<game::coop::SyncWatch>(*message) ||
               std::holds_alternative<game::coop::SyncReport>(*message)) {
      sync_.push_back(std::move(*message));
    } else if (std::holds_alternative<game::coop::WorldClock>(*message) ||
               std::holds_alternative<game::coop::TrainingStatus>(*message)) {
      training_.push_back(std::move(*message));
    } else if (std::holds_alternative<game::coop::PlayerState>(*message) ||
               std::holds_alternative<game::coop::PlayerCorrection>(*message) ||
               std::holds_alternative<game::coop::Roster>(*message) ||
               std::holds_alternative<game::coop::PartnerForm>(*message) ||
               std::holds_alternative<game::coop::WorldAsk>(*message) ||
               std::holds_alternative<game::coop::WorldChange>(*message) ||
               std::holds_alternative<game::coop::WorldSync>(*message) ||
               std::holds_alternative<game::coop::GroundItems>(*message) ||
               std::holds_alternative<game::coop::ItemClaim>(*message) ||
               std::holds_alternative<game::coop::ClaimAnswer>(*message)) {
      world_.push_back(std::move(*message));
    } else if (isBattleMessage(*message)) {
      battles_.push_back(std::move(*message));
    } else if (isTradeMessage(*message)) {
      trades_.push_back(std::move(*message));
    } else {
      ++ignored_;
    }
  }
}

core::Result<void> CoopGameplayBridge::sendLocalField(const PlayerCommands& commands) {
  auto networkCommand = toCoopCommand(commands);
  if (!networkCommand)
    return std::unexpected{networkCommand.error()};
  return link_->send(game::coop::Message{std::move(*networkCommand)});
}

core::Result<void> CoopGameplayBridge::sendHostSnapshot(const game::coop::Snapshot& snapshot) {
  return link_->send(game::coop::Message{snapshot});
}

std::optional<PresentedCoopWorld> CoopGameplayBridge::presentedWorld(net::Millis now) const {
  return snapshots_.sample(now);
}

std::vector<game::coop::Message> CoopGameplayBridge::takeSyncMessages() {
  return std::exchange(sync_, {});
}

std::vector<game::coop::Message> CoopGameplayBridge::takeTrainingMessages() {
  return std::exchange(training_, {});
}

std::vector<game::coop::Message> CoopGameplayBridge::takeWorldMessages() {
  return std::exchange(world_, {});
}

core::Result<void> CoopGameplayBridge::send(const game::coop::Message& message) {
  return link_->send(message);
}

std::optional<game::coop::Message> CoopGameplayBridge::receiveBattle() {
  if (battles_.empty())
    return std::nullopt;
  game::coop::Message message = std::move(battles_.front());
  battles_.pop_front();
  return message;
}

std::optional<game::coop::Message> CoopGameplayBridge::receiveTrade() {
  if (trades_.empty()) {
    return std::nullopt;
  }
  game::coop::Message message = std::move(trades_.front());
  trades_.pop_front();
  return message;
}

core::Result<void> CoopGameplayBridge::sendBattle(game::coop::Message message) {
  return link_->send(message);
}

net::Millis CoopGameplayBridge::worldQuietFor(net::Millis now) const {
  const std::optional<net::Millis> since = lastSnapshot_ ? lastSnapshot_ : firstPolled_;
  return since ? std::max(net::Millis{0}, now - *since) : net::Millis{0};
}

} // namespace dwe::app
