#include "player_registry.hpp"

#include <algorithm>
#include <utility>

namespace dwe::app {

PlayerRegistry::PlayerRegistry(std::string hostName) {
  slots_[HostPlayerId].emplace(PlayerSlot{.id = HostPlayerId,
                                          .name = std::move(hostName),
                                          .local = true,
                                          .link = PlayerLink::Local,
                                          .controller = std::make_unique<LocalController>()});
}

bool PlayerRegistry::addGuest(PlayerId id, std::string name,
                              std::unique_ptr<PlayerController> controller, PlayerLink link) {
  if (id == HostPlayerId || id >= MaxPlayerCount || slots_[id] || name.empty() ||
      controller == nullptr || link == PlayerLink::Local) {
    return false;
  }
  slots_[id].emplace(PlayerSlot{.id = id,
                                .name = std::move(name),
                                .local = false,
                                .link = link,
                                .controller = std::move(controller)});
  return true;
}

bool PlayerRegistry::removeGuest(PlayerId id) {
  if (id == HostPlayerId || id >= MaxPlayerCount || !slots_[id]) {
    return false;
  }
  slots_[id].reset();
  return true;
}

void PlayerRegistry::setLink(PlayerId id, PlayerLink link) {
  if (id != HostPlayerId && id < MaxPlayerCount && slots_[id] && link != PlayerLink::Local) {
    slots_[id]->link = link;
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
  return static_cast<std::size_t>(
      std::ranges::count_if(slots_, [](const auto& slot) { return slot.has_value(); }));
}

} // namespace dwe::app
