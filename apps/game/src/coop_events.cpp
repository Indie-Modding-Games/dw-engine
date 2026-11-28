#include "coop_events.hpp"

#include <dwe/core/log.hpp>

#include <variant>

namespace dwe::app {

namespace {

std::optional<CoopEvent> eventOf(const game::coop::EventStatus& status) {
  if (status.mapId.empty()) {
    return std::nullopt;
  }
  return CoopEvent{.mapId = status.mapId, .npc = status.npc};
}

game::coop::EventStatus statusOf(game::coop::PlayerId player,
                                 const std::optional<CoopEvent>& event) {
  if (!event || event->mapId.size() > game::coop::MaxEventMapText) {
    return {.player = player, .mapId = {}, .npc = 0};
  }
  return {.player = player, .mapId = event->mapId, .npc = event->npc};
}

} // namespace

void CoopEvents::hostTick(const std::optional<CoopEvent>& own,
                          std::span<const CoopEventPeer> guests) {
  events_.at(game::coop::HostPlayerId) = own;
  for (const CoopEventPeer& guest : guests) {
    for (const game::coop::Message& message : guest.bridge->takeEventMessages()) {
      // A guest speaks for its own game only: the link's player, whatever it named.
      if (const auto* status = std::get_if<game::coop::EventStatus>(&message)) {
        events_.at(guest.id) = eventOf(*status);
      }
    }
  }
  for (const CoopEventPeer& guest : guests) {
    Events& told = told_.at(guest.id);
    for (game::coop::PlayerId player = 0; player < game::coop::MaxPlayers; ++player) {
      if (player == guest.id || told.at(player) == events_.at(player)) {
        continue;
      }
      if (auto sent = guest.bridge->send(statusOf(player, events_.at(player))); sent) {
        told.at(player) = events_.at(player);
      } else {
        core::logWarning("coop: could not tell player {} about player {}'s event: {}", guest.id,
                         player, sent.error().message);
      }
    }
  }
}

void CoopEvents::guestTick(const std::optional<CoopEvent>& own, game::coop::PlayerId self,
                           CoopGameplayBridge& host) {
  if (!game::coop::validPlayer(self)) {
    return;
  }
  for (const game::coop::Message& message : host.takeEventMessages()) {
    if (const auto* status = std::get_if<game::coop::EventStatus>(&message);
        status != nullptr && status->player != self) {
      events_.at(status->player) = eventOf(*status);
    }
  }
  events_.at(self) = own;
  if (sent_ == own) {
    return;
  }
  if (auto sent = host.send(statusOf(self, own)); sent) {
    sent_ = own;
  }
}

std::vector<std::int32_t> CoopEvents::heldByOthers(game::coop::PlayerId self,
                                                   std::string_view mapId) const {
  std::vector<std::int32_t> held;
  for (game::coop::PlayerId player = 0; player < game::coop::MaxPlayers; ++player) {
    const std::optional<CoopEvent>& event = events_.at(player);
    if (player != self && event && event->mapId == mapId) {
      held.push_back(event->npc);
    }
  }
  return held;
}

void CoopEvents::playerLeft(game::coop::PlayerId id) {
  if (!game::coop::validPlayer(id)) {
    return;
  }
  events_.at(id).reset();
  told_.at(id) = {};
}

void CoopEvents::reset() {
  *this = CoopEvents{};
}

} // namespace dwe::app
