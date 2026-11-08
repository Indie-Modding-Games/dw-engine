#pragma once

#include <dwe/game/story/story_ports.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dwe::app {

// The sounds a field scene (the evolution, the death and the rebirth) plays from its own bank in
// slot 8 (sound.c: playSound2), each kept until the scene stops them all (sound.c: stopSound).
// Some of those banks' samples loop until their voice is stopped, so a scene that lets go of a
// sound without stopping it leaves it playing for good.
class HeldSceneSounds {
public:
  // Slot 8 plays the scene's bank (playSound2(8, n)).
  static constexpr std::int64_t Slot = 8;

  // Plays a sound of the scene's bank and keeps it; nothing without a port.
  void play(game::story::SoundPort* port, std::int64_t sound);
  // Stops every sound kept since the last stop (sound.c: stopSound).
  void stopAll(game::story::SoundPort* port);
  // How many sounds are kept.
  [[nodiscard]] std::size_t held() const { return held_.size(); }

private:
  std::vector<std::int64_t> held_;
};

} // namespace dwe::app
