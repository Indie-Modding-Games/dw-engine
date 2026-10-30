#include "pause_box.hpp"

namespace dwe::app {

PauseStep PauseBox::tick(bool pressed, bool allowed) {
  if (!pressed || (!allowed && !paused_)) {
    return {.paused = paused_};
  }
  // handlePause: PAUSE_STATE = (PAUSE_STATE + 1) & 1 on Start's edge.
  paused_ = !paused_;
  return {.paused = paused_, .opened = paused_, .closed = !paused_};
}

} // namespace dwe::app
