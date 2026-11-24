#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>

#include <array>
#include <string>
#include <string_view>

namespace dwe::app {

inline constexpr std::array<std::string_view, 4> CoopGuestOwnSections{"partner", "moves",
                                                                      "evolution", "inventory"};

// The guest's partner as its save holds it. The rest of what the guest brings into the host's
// world is CoopGuestOwnSections (hostWorldWithGuestPartner); the save's map, story and clock stay
// the host's.
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
