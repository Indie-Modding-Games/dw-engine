#pragma once

namespace dwe::app {

// The partner's scenes that run instead of the field this tick (partner.c: partnerTickOverworld:
// the sleep 3, the care scenes 4 to 6 and 15, a pile 7, the evolution 13 and the death 8 and 14).
struct PartnerSceneHolds {
  bool sleep = false;
  bool pile = false;
  bool toilet = false;
  bool feed = false;
  bool praiseScold = false;
  bool evolution = false;
  bool death = false;

  [[nodiscard]] constexpr bool any() const {
    return sleep || pile || toilet || feed || praiseScold || evolution || death;
  }
};

// ui.c: an animated box grows and shrinks in its own render (renderUIBoxAnimated), whatever the
// partner does. With no course running, the field's tick closes the gym's last box
// (GymScreen::tick); while one of the partner's scenes holds the field instead, the box must close
// on its own, or its outline stays on screen, like an empty window, until the scene ends (an
// evolution right after a session: the stats box over the whole scene).
[[nodiscard]] constexpr bool closesGymBoxBesideScene(bool courseRunning,
                                                     const PartnerSceneHolds& holds) {
  return !courseRunning && holds.any();
}

} // namespace dwe::app
