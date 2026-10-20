#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/state_yaml.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string>

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
        state.virusBackup[Stat::Mp] = 77;
        state.hunger.refusedFavorite = true;
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

} // namespace dwe::game::partner
