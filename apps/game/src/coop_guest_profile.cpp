#include "coop_guest_profile.hpp"

#include <dwe/game/partner/state_yaml.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>

#include <format>

#include "coop_guest_return.hpp"

namespace dwe::app {
namespace {
constexpr std::string_view PartnerSection = "partner";
constexpr std::size_t MaxGuestSaveBytes = 128U * 1024U;
static_assert(CoopGuestOwnSections[0] == PartnerSection);
static_assert(CoopGuestOwnSections[1] == session::MovesSaveName);
static_assert(CoopGuestOwnSections[2] == session::EvolutionSaveName);
static_assert(CoopGuestOwnSections[3] == session::InventorySaveName);
} // namespace

core::Result<CoopGuestPartnerProfile> extractCoopGuestPartnerProfile(std::string_view saveText) {
  if (saveText.empty() || saveText.size() > MaxGuestSaveBytes) {
    return core::fail("cooperative guest save is empty or exceeds 128 KiB");
  }
  auto parsed = game::save::parseSaveFile(saveText);
  if (!parsed)
    return std::unexpected{parsed.error()};

  const auto section = parsed->sections.find(PartnerSection);
  if (section == parsed->sections.end()) {
    return core::fail("cooperative guest save has no partner section");
  }
  game::save::MigrationChain migrations;
  migrations.add(game::partner::addVirusBackupSpecies);
  auto migrated =
      migrations.migrate(section->second.state, section->second.version, "section 'partner'");
  if (!migrated)
    return std::unexpected{migrated.error()};

  auto state = game::partner::fromYaml(*migrated);
  if (!state)
    return std::unexpected{state.error()};
  if (state->species.empty()) {
    return core::fail("cooperative guest partner has no species");
  }
  return CoopGuestPartnerProfile{.species = state->species,
                                 .nickname = parsed->header.summary.nickname,
                                 .state = std::move(*state)};
}

core::Result<std::string> hostWorldWithGuestPartner(std::string_view hostSaveText,
                                                    std::string_view guestSaveText) {
  auto host = game::save::parseSaveFile(hostSaveText);
  if (!host)
    return std::unexpected{host.error()};
  auto guest = extractCoopGuestPartnerProfile(guestSaveText);
  if (!guest)
    return std::unexpected{guest.error()};

  host->sections.insert_or_assign(
      std::string{PartnerSection},
      game::save::SavedSection{.version = game::partner::PartnerStateVersion,
                               .state = game::partner::toYaml(guest->state)});
  // The rest of what the guest brings is its own too: the partner's moves (known and equipped),
  // which its battles on the host fight with (coop_battles.hpp), the species it raised, and its bag
  // with its bits. It plays with them in the host's world and takes them home when it leaves
  // (coop_guest_return.hpp); the host's bag and money stay the host's.
  if (auto parsedGuest = game::save::parseSaveFile(guestSaveText)) {
    for (const std::string_view name : CoopGuestOwnSections) {
      if (name == PartnerSection) {
        continue;
      }
      if (const auto own = parsedGuest->sections.find(name); own != parsedGuest->sections.end()) {
        host->sections.insert_or_assign(std::string{name}, own->second);
      }
    }
  }
  // A note the host's own save brought back from another co-op game is the host's to read.
  host->sections.erase(std::string{CoopReturnSection});
  host->header.summary.species = guest->species;
  host->header.summary.nickname = guest->nickname;
  // The copy is the guest's to play, never to save: it names none of the host's slots, so nothing
  // in the guest's game takes a slot of the guest's disk as its own (multiplayer.md, "Saves in
  // co-op").
  host->header.summary.slot.clear();
  host->header.autosaveReasons.clear();
  return game::save::writeSaveFile(*host);
}

} // namespace dwe::app
