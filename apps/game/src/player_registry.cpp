#include "player_registry.hpp"

#include <utility>

namespace dwe::app {

PlayerRegistry::PlayerRegistry(std::string hostName) {
  slots_[HostPlayerId].emplace(PlayerSlot{.id = HostPlayerId,
                                          .name = std::move(hostName),
                                          .local = true,
                                          .link = PlayerLink::Local,
                                          .controller = std::make_unique<LocalController>()});
}

bool PlayerRegistry::addGuest(std::string name, std::unique_ptr<PlayerController> controller,
                              PlayerLink link) {
  if (slots_[GuestPlayerId] || name.empty() || controller == nullptr || link == PlayerLink::Local) {
    return false;
  }
  slots_[GuestPlayerId].emplace(PlayerSlot{.id = GuestPlayerId,
                                           .name = std::move(name),
                                           .local = false,
                                           .link = link,
                                           .controller = std::move(controller)});
  return true;
}

void PlayerRegistry::setGuestLink(PlayerLink link) {
  if (slots_[GuestPlayerId] && link != PlayerLink::Local) {
    slots_[GuestPlayerId]->link = link;
  }
}

LocalController& PlayerRegistry::localController() {
  return static_cast<LocalController&>(*slots_[HostPlayerId]->controller);
}

std::optional<PlayerCommands> PlayerRegistry::next(PlayerId id, std::uint64_t tick) {
  if (id >= MaxPlayerCount || !slots_[id] || slots_[id]->controller == nullptr) {
    return std::nullopt;
  }
  if (!slots_[id]->local && slots_[id]->link != PlayerLink::Connected) {
    return std::nullopt;
  }
  return slots_[id]->controller->next(tick);
}

const PlayerSlot* PlayerRegistry::find(PlayerId id) const {
  if (id >= MaxPlayerCount || !slots_[id]) {
    return nullptr;
  }
  return &*slots_[id];
}

std::size_t PlayerRegistry::size() const {
  return static_cast<std::size_t>(slots_[HostPlayerId].has_value()) +
         static_cast<std::size_t>(slots_[GuestPlayerId].has_value());
}

} // namespace dwe::app
