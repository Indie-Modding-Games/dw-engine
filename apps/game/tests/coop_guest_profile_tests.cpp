#include "coop_guest_profile.hpp"

#include <dwe/game/partner/state_yaml.hpp>
#include <dwe/game/save/save_file.hpp>

#include <doctest/doctest.h>

using namespace dwe;

namespace {
std::string guestSave(std::int32_t partnerVersion = game::partner::PartnerStateVersion) {
  game::partner::PartnerState partner;
  partner.species = "guest_species";
  partner.stats.currentHp = 987;
  core::YamlNode partnerState = game::partner::toYaml(partner);
  if (partnerVersion == 1) {
    const core::YamlNode* backup = partnerState.find("virus_backup");
    if (backup == nullptr) return {};
    const core::YamlNode* stats = backup->find("stats");
    if (stats == nullptr) return {};
    partnerState.set("virus_backup", *stats);
    partnerState.set("version", core::YamlNode::scalar("1"));
  }
  game::save::SaveFile file;
  file.header.summary.nickname = "Guest pal";
  file.header.summary.map = "guest_map_must_not_be_applied";
  file.sections.emplace("partner", game::save::SavedSection{
                                        .version = partnerVersion,
                                        .state = std::move(partnerState)});
  file.sections.emplace("field", game::save::SavedSection{
                                     .version = 999,
                                     .state = core::YamlNode::mapping()});
  const auto text = game::save::writeSaveFile(file);
  REQUIRE(text);
  return *text;
}
} // namespace

TEST_CASE("cooperative guest profile extracts only partner identity and current state") {
  const auto profile = app::extractCoopGuestPartnerProfile(guestSave());
  REQUIRE(profile);
  CHECK(profile->species == "guest_species");
  CHECK(profile->nickname == "Guest pal");
  CHECK(profile->state.stats.currentHp == 987);
}

TEST_CASE("cooperative guest profile migrates partner but ignores guest world progress") {
  const auto profile = app::extractCoopGuestPartnerProfile(guestSave(1));
  REQUIRE(profile);
  CHECK(profile->species == "guest_species");
  CHECK(profile->state.stats.currentHp == 987);
}

TEST_CASE("cooperative guest profile rejects missing partner and oversized transfer") {
  CHECK_FALSE(app::extractCoopGuestPartnerProfile(""));
  CHECK_FALSE(app::extractCoopGuestPartnerProfile(std::string(128U * 1024U + 1U, 'x')));
}
