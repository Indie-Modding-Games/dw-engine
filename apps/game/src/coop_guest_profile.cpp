#include "coop_guest_profile.hpp"

#include <dwe/game/partner/state_yaml.hpp>
#include <dwe/game/save/save_file.hpp>

#include <format>

namespace dwe::app {
namespace {
constexpr std::string_view PartnerSection = "partner";
constexpr std::size_t MaxGuestSaveBytes = 128U * 1024U;
} // namespace

core::Result<CoopGuestPartnerProfile>
extractCoopGuestPartnerProfile(std::string_view saveText) {
  if (saveText.empty() || saveText.size() > MaxGuestSaveBytes) {
    return core::fail("cooperative guest save is empty or exceeds 128 KiB");
  }
  auto parsed = game::save::parseSaveFile(saveText);
  if (!parsed) return std::unexpected{parsed.error()};

  const auto section = parsed->sections.find(PartnerSection);
  if (section == parsed->sections.end()) {
    return core::fail("cooperative guest save has no partner section");
  }
  game::save::MigrationChain migrations;
  migrations.add(game::partner::addVirusBackupSpecies);
  auto migrated = migrations.migrate(section->second.state, section->second.version,
                                    "section 'partner'");
  if (!migrated) return std::unexpected{migrated.error()};

  auto state = game::partner::fromYaml(*migrated);
  if (!state) return std::unexpected{state.error()};
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
  if (!host) return std::unexpected{host.error()};
  auto guest = extractCoopGuestPartnerProfile(guestSaveText);
  if (!guest) return std::unexpected{guest.error()};

  host->sections.insert_or_assign(
      std::string{PartnerSection},
      game::save::SavedSection{.version = game::partner::PartnerStateVersion,
                               .state = game::partner::toYaml(guest->state)});
  host->header.summary.species = guest->species;
  host->header.summary.nickname = guest->nickname;
  return game::save::writeSaveFile(*host);
}

} // namespace dwe::app
