#include "coop_guest_profile.hpp"

#include <dwe/game/partner/state_yaml.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/session/battle_adapter.hpp>

#include <format>

namespace dwe::app {
namespace {
constexpr std::string_view PartnerSection = "partner";
constexpr std::size_t MaxGuestSaveBytes = 128U * 1024U;
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
  // The partner's moves (known and equipped) are the guest's too: its battles on the host fight
  // with them (coop_battles.hpp).
  if (auto parsedGuest = game::save::parseSaveFile(guestSaveText)) {
    if (const auto moves = parsedGuest->sections.find(session::MovesSaveName);
        moves != parsedGuest->sections.end()) {
      host->sections.insert_or_assign(std::string{session::MovesSaveName}, moves->second);
    }
  }
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
