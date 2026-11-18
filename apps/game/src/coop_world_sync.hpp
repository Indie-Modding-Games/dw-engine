#pragma once

#include "coop_snapshot_interpolation.hpp"

#include <dwe/game/field/field.hpp>

#include <optional>
#include <string_view>

namespace dwe::app {

// The three bodies a joining client must place from one authoritative sample. `hostPlayer` and
// `hostPartner` drive the presented model + shadow pair; `guestAuthoritative` carries the host's
// truth for the guest's own predicted body and is the base the reconcile compares against.
// A `std::nullopt` for the host pair means the model is WITHHELD while the shadow still falls back
// to the field's empty host-slot transform: that is exactly the "shadow without character" state the
// co-op presentation has to detect, and it happens whenever `world.mapId != fieldMapId`.
struct CoopPresentedRoles {
  std::optional<game::field::CharacterState> hostPlayer;
  std::optional<game::field::CharacterState> hostPartner;
  std::optional<game::field::CharacterState> guestAuthoritative;
};

// Whether the client draws the host's bodies at all — the MODEL and its SHADOW together, so they
// can never drift apart again (the bug was: shadow drawn from the empty host slot while the model
// was withheld, leaving a lone shadow when the host crossed to a map the guest cannot follow yet).
// In the guest's local view both are drawn only while an authoritative host pose exists for the
// same map the field shows; on a map transition the guest cannot follow, neither is drawn, so the
// guest sees a clean field instead of a stray shadow. Outside the guest local view (single player,
// or the host's own screen) the bodies are the field's real players and always draw.
[[nodiscard]] inline bool presentsHostBodies(bool guestLocalView, bool hostBodyPresented) {
  return !guestLocalView || hostBodyPresented;
}

// Decides what the client presents from one authoritative snapshot. Kept as a pure helper (header
// inline, no state) so a headless test can reproduce the presentation decision without a renderer
// or a second machine: the same function drives both `main()` and the loopback test.
[[nodiscard]] inline CoopPresentedRoles presentedRoles(const PresentedCoopWorld& world,
                                                       std::string_view fieldMapId) {
  CoopPresentedRoles roles;
  if (world.mapId != fieldMapId) {
    return roles;
  }
  const auto toState = [](const PresentedCoopEntity& entity) {
    const scene::Motion motion = entity.motion == game::coop::Motion::Run
                                     ? scene::Motion::Run
                                 : entity.motion == game::coop::Motion::Walk
                                     ? scene::Motion::Walk
                                     : scene::Motion::Idle;
    return game::field::CharacterState{.position = entity.position,
                                       .yaw = entity.yaw,
                                       .motion = motion};
  };
  for (const PresentedCoopEntity& entity : world.entities) {
    if (entity.role == game::coop::EntityRole::Player) {
      if (entity.owner == game::coop::HostPlayerId) {
        roles.hostPlayer = toState(entity);
      } else if (entity.owner == game::coop::GuestPlayerId) {
        roles.guestAuthoritative = toState(entity);
      }
    } else if (entity.role == game::coop::EntityRole::Partner &&
               entity.owner == game::coop::HostPlayerId) {
      roles.hostPartner = toState(entity);
    }
  }
  return roles;
}

} // namespace dwe::app
