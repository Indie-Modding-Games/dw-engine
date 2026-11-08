#include "scene_sounds.hpp"

#include <optional>

namespace dwe::app {

void HeldSceneSounds::play(game::story::SoundPort* port, std::int64_t sound) {
  if (port == nullptr) {
    return;
  }
  if (const std::optional<std::int64_t> held = port->playHeldSound(Slot, sound)) {
    held_.push_back(*held);
  }
}

void HeldSceneSounds::stopAll(game::story::SoundPort* port) {
  if (port != nullptr) {
    for (const std::int64_t held : held_) {
      port->stopHeldSound(held);
    }
  }
  held_.clear();
}

} // namespace dwe::app
