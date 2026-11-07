#pragma once

#include <glm/vec2.hpp>

#include <optional>

namespace dwe::app {

// Where the field's camera points, in background pixels (the pixel the visible window centres on
// before the clamp to the background). map.c: tickCameraFollowPlayer moves cameraX/cameraY by the
// player's projected movement of the frame, from wherever the camera was: nothing centres it on
// the player again. A battle (btl.c: startBattle calls unsetCameraFollowPlayer) leaves the camera
// where it was while the player runs back, and the camera then follows by differences with the
// player off the centre; a script's camera (script_interp.c: scriptPauseGame, tickCameraMovement)
// points where the script says, and the field follows on from there. The focus is not clamped:
// map.c: updateDrawingOffsets lets the player move on screen at an edge and the camera moves again
// once the player is back where it was on screen.
class FieldCameraFollow {
public:
  // The focus for this frame. `player` is the player's background pixel; `following` is false
  // while the camera is held (a battle, a map change's walk-out); `script` is the background pixel
  // a script's camera points at, which wins.
  [[nodiscard]] glm::vec2 update(glm::vec2 player, bool following,
                                 std::optional<glm::vec2> script) {
    if (script) {
      focus_ = *script;
    } else if (!focus_) {
      // map.c: setupMap: a map starts with the player in the centre.
      focus_ = player;
    } else if (following && lastPlayer_) {
      *focus_ += player - *lastPlayer_;
    }
    lastPlayer_ = player;
    return *focus_;
  }

  // A new map: the next frame centres on the player.
  void reset() {
    focus_.reset();
    lastPlayer_.reset();
  }

  [[nodiscard]] std::optional<glm::vec2> focus() const { return focus_; }

private:
  std::optional<glm::vec2> focus_;
  std::optional<glm::vec2> lastPlayer_;
};

} // namespace dwe::app
