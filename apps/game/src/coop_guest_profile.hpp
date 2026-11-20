#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>

#include <array>
#include <string>
#include <string_view>

namespace dwe::app {

// The save sections that are the guest's own in the host's world (multiplayer.md, decisions 1 and
// 4): its partner, the partner's moves, the species it raised, and its bag with its bits. The
// guest's game plays with them and takes them home; everything else is the host's world.
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

// Builds the guest's game from the host's world/save, with the guest's own sections
// (CoopGuestOwnSections) in place of the host's. Neither the guest's world progress nor host files
// enter this result, and it names no save slot: the guest's game is played in memory, and only
// what is its own goes home (coop_guest_return.hpp).
[[nodiscard]] core::Result<std::string> hostWorldWithGuestPartner(std::string_view hostSaveText,
                                                                  std::string_view guestSaveText);

} // namespace dwe::app
