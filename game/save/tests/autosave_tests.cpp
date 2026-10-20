#include <dwe/assets/file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>

namespace {

using namespace dwe::game::save;

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

dwe::ruleset::Section saveSection(std::string_view yaml) {
    return {.name = std::string{SaveSectionName},
            .owner = "test",
            .file = "rules/test/save.yaml",
            .node = dwe::core::parseYaml(yaml).value()};
}

constexpr std::string_view MapChangeOnly = R"(autosave:
  on_map_change: true
  on_battle_end: false
  on_partner_sleep: false
  on_new_day: false
)";

} // namespace

TEST_CASE("the save section says which reasons autosave") {
    const auto rules = readAutosaveRules(saveSection(MapChangeOnly));
    REQUIRE_MESSAGE(rules, errorOf(rules));
    CHECK(rules->savesOn(AutosaveReason::MapChange));
    CHECK_FALSE(rules->savesOn(AutosaveReason::BattleEnd));
    CHECK_FALSE(rules->savesOn(AutosaveReason::PartnerSleep));
    CHECK_FALSE(rules->savesOn(AutosaveReason::NewDay));

    const auto missing = readAutosaveRules(saveSection("autosave:\n  on_map_change: true\n"));
    CHECK(errorOf(missing).starts_with("rules/test/save.yaml: "));
    CHECK(errorOf(readAutosaveRules(saveSection("autosave: {on_map_change: yes, on_battle_end: false, "
                                                "on_partner_sleep: false, on_new_day: false}\n")))
              .starts_with("rules/test/save.yaml: "));
    CHECK(saveSectionCheck()(saveSection(MapChangeOnly)));
    CHECK_FALSE(saveSectionCheck()(saveSection("autosave: {}\n")));
}

TEST_CASE("the original ruleset autosaves on the approved reasons") {
    const auto bytes = dwe::assets::DiskFileSystem{}.readFile(std::filesystem::path{DWE_GAME_PACK} / "rules" /
                                                              "original" / "save.yaml");
    REQUIRE(bytes);
    const auto rules =
        readAutosaveRules(saveSection(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()}));
    REQUIRE_MESSAGE(rules, errorOf(rules));
    CHECK(rules->onMapChange);
    CHECK(rules->onBattleEnd);
    CHECK(rules->onPartnerSleep);
    CHECK_FALSE(rules->onNewDay);
}

TEST_CASE("autosave requests count only the ruleset's reasons, and only with slots") {
    const AutosaveRules rules = readAutosaveRules(saveSection(MapChangeOnly)).value();
    AutosaveRequests requests{rules, 2};
    CHECK_FALSE(requests.takeRequest());
    CHECK_FALSE(requests.requestAutosave(AutosaveReason::PartnerSleep));
    CHECK_FALSE(requests.takeRequest());
    CHECK(requests.requestAutosave(AutosaveReason::MapChange));
    CHECK(requests.takeRequest() == AutosaveReason::MapChange);
    CHECK_FALSE(requests.takeRequest());

    requests.setRules({.onMapChange = false, .onBattleEnd = true, .onPartnerSleep = false, .onNewDay = false});
    CHECK_FALSE(requests.requestAutosave(AutosaveReason::MapChange));
    CHECK(requests.requestAutosave(AutosaveReason::BattleEnd));

    AutosaveRequests noSlots{rules, 0};
    CHECK_FALSE(noSlots.requestAutosave(AutosaveReason::MapChange));
    CHECK_FALSE(AutosaveRequests(AutosaveRules{}, 2).requestAutosave(AutosaveReason::MapChange));
}
