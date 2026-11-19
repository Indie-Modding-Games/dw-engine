#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>

#include <string>
#include <string_view>

namespace dwe::app {

// The only guest-save data admitted into a host session. The save's map, story, inventory,
// clock, and other sections are intentionally not represented here.
struct CoopGuestPartnerProfile {
  std::string species;
  std::string nickname;
  game::partner::PartnerState state;
};

// Parses the bounded guest save, migrates only its partner section, and extracts the companion.
// No session/world restore is performed.
[[nodiscard]] core::Result<CoopGuestPartnerProfile>
extractCoopGuestPartnerProfile(std::string_view saveText);

// Builds the guest's local presentation save from the host's world/save, replacing only the
// companion section with the guest's companion. Neither guest world progress nor host files enter
// this result, and it names no save slot: the guest's game is played in memory and never saved.
[[nodiscard]] core::Result<std::string> hostWorldWithGuestPartner(std::string_view hostSaveText,
                                                                  std::string_view guestSaveText);

} // namespace dwe::app
