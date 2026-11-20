#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>

#include <array>
#include <string>
#include <string_view>

namespace dwe::app {

inline constexpr std::array<std::string_view, 4> CoopGuestOwnSections{"partner", "moves",
                                                                      "evolution", "inventory"};

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

[[nodiscard]] core::Result<std::string> hostWorldWithGuestPartner(std::string_view hostSaveText,
                                                                  std::string_view guestSaveText);

[[nodiscard]] std::string coopSaveMode(std::string_view saveText);

} // namespace dwe::app
