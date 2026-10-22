#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/save/slots.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace dwe::app {

// What a game starts from: a new game, or the save in a slot.
struct PlayStart {
  std::optional<game::save::SlotId> load;
};

// The app's states (docs/architecture/guardar-y-cargar.md, 3.2): the title screen, with no game,
// and a game being played. A game is built whole and torn down whole, so a second game never sees
// what the first left behind (queued scripts, a battle, the clock).
template <class Play>
class AppFlow {
public:
  using Starter = std::function<core::Result<std::unique_ptr<Play>>(const PlayStart&)>;

  explicit AppFlow(Starter starter) : starter_(std::move(starter)) {}

  // The game being played; nothing at the title screen.
  [[nodiscard]] Play* playing() const { return play_.get(); }

  // Leaves the game being played, if any, and starts another. The new game is installed only once
  // it started, so a load that fails leaves the app at the title screen.
  core::Result<void> play(const PlayStart& start) {
    // The old game goes first: two games would share the field's GPU memory, the HUD and the
    // tuning panel.
    toTitle();
    auto started = starter_(start);
    if (!started) {
      return std::unexpected{started.error()};
    }
    play_ = std::move(*started);
    return {};
  }

  // Ends the game being played without saving it.
  void toTitle() { play_.reset(); }

private:
  Starter starter_;
  std::unique_ptr<Play> play_;
};

} // namespace dwe::app
