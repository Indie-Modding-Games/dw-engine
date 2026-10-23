#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/state_yaml.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <string_view>

#include "partner_fixture.hpp"

namespace dwe::game::partner {

TEST_CASE("the partner state goes to YAML and back unchanged, with its version") {
  testing::OriginalRuleset original;
  testing::Raising raising(original.rules());
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Sick);
    state.conditions.set(Condition::Sulking);
    state.mood.scoldBoost = ScoldBoost::Used;
    state.life.protectedHour = 7;
    state.life.phase = LifePhase::DiedOtherwise;
    state.training.boost = {.stats = {Stat::Hp, Stat::Brain}, .value = 15, .frames = 300};
    state.training.youngSessions[Stat::Speed] = 4;
    state.virusBackup.species = "kept";
    state.virusBackup.stats[Stat::Mp] = 77;
    state.hunger.refusedFavorite = true;
    state.toilet.wildPoopDue = true;
  });
  const core::YamlNode node = toYaml(raising.state());
  const core::YamlNode* version = node.find("version");
  REQUIRE(version != nullptr);
  CHECK(version->text() == std::to_string(PartnerStateVersion));
  const auto restored = fromYaml(node);
  REQUIRE_MESSAGE(restored.has_value(), testing::errorOf(restored));
  CHECK(*restored == raising.state());
}

TEST_CASE("a partner state of an unknown version or with a bad value is refused") {
  testing::OriginalRuleset original;
  testing::Raising raising(original.rules());
  core::YamlNode newer = toYaml(raising.state());
  newer.set("version", core::YamlNode::scalar("99"));
  CHECK_FALSE(fromYaml(newer).has_value());
  core::YamlNode broken = toYaml(raising.state());
  broken.set("weight", core::YamlNode::scalar("heavy"));
  CHECK_FALSE(fromYaml(broken).has_value());
  core::YamlNode missing = core::YamlNode::mapping();
  missing.set("version", core::YamlNode::scalar("1"));
  CHECK_FALSE(fromYaml(missing).has_value());
}

TEST_CASE("a partner state from before the pile scenes has no pile waiting for its scene") {
  testing::OriginalRuleset original;
  testing::Raising raising(original.rules());
  raising.edit([](PartnerState& state) { state.toilet.wildPoopDue = true; });
  core::YamlNode old = toYaml(raising.state());
  const core::YamlNode* toilet = old.find("toilet");
  REQUIRE(toilet != nullptr);
  core::YamlNode before = core::YamlNode::mapping();
  for (const std::string_view key : {"poop_level", "urge_frames", "virus_bar"}) {
    const core::YamlNode* value = toilet->find(key);
    REQUIRE(value != nullptr);
    before.set(std::string{key}, *value);
  }
  old.set("toilet", before);
  const auto restored = fromYaml(old);
  REQUIRE_MESSAGE(restored.has_value(), testing::errorOf(restored));
  CHECK_FALSE(restored->toilet.wildPoopDue);
  CHECK(restored->toilet.urgeFrames == raising.state().toilet.urgeFrames);
}

TEST_CASE("a partner state of version 1 gains an empty virus backup species and keeps its stats") {
  testing::OriginalRuleset original;
  testing::Raising raising(original.rules());
  raising.edit([](PartnerState& state) {
    state.virusBackup.species = "kept";
    state.virusBackup.stats[Stat::Offense] = 321;
  });
  // Version 1 kept only the stats, straight under virus_backup.
  core::YamlNode old = toYaml(raising.state());
  const core::YamlNode* backup = old.find("virus_backup");
  REQUIRE(backup != nullptr);
  const core::YamlNode* stats = backup->find("stats");
  REQUIRE(stats != nullptr);
  old.set("virus_backup", *stats);
  old.set("version", core::YamlNode::scalar("1"));
  CHECK_FALSE(fromYaml(old).has_value());

  const auto migrated = addVirusBackupSpecies(old);
  REQUIRE_MESSAGE(migrated.has_value(), testing::errorOf(migrated));
  const auto restored = fromYaml(*migrated);
  REQUIRE_MESSAGE(restored.has_value(), testing::errorOf(restored));
  CHECK(restored->virusBackup.species.empty());
  CHECK(restored->virusBackup.stats[Stat::Offense] == 321);
  CHECK(restored->species == raising.state().species);

  CHECK_FALSE(addVirusBackupSpecies(core::YamlNode::mapping()).has_value());
}

} // namespace dwe::game::partner
