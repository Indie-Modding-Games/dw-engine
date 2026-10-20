#include <dwe/assets/file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/game/save/text_keys.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

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
  on_evolution: false
  on_tournament_result: false
  on_recruit_join: false
  on_key_item: false
  on_dangerous_area: false
  on_story_progress: false
  on_partner_reborn: false
  coalesce_seconds: 2
)";

// The ruleset flag of each reason, in enum order.
constexpr std::array<std::string_view, AutosaveReasons.size()> ReasonFlags{
    "on_map_change",     "on_battle_end",        "on_partner_sleep", "on_new_day",
    "on_evolution",      "on_tournament_result", "on_recruit_join",  "on_key_item",
    "on_dangerous_area", "on_story_progress",    "on_partner_reborn"};

std::string replaced(std::string text, std::string_view from, std::string_view to) {
    text.replace(text.find(from), from.size(), to);
    return text;
}

// Rules with every reason on, as `original`, and a two-second window.
AutosaveRules everyReason() {
    AutosaveRules rules{.savingReasons = {}, .coalesceSeconds = 2.0F};
    rules.savingReasons.fill(true);
    return rules;
}

std::vector<std::string> reasonNamesOf(const std::optional<std::vector<AutosaveReason>>& reasons) {
    return reasons ? autosaveReasonNames(*reasons) : std::vector<std::string>{};
}

SaveFile autosaveAt(std::int64_t second, std::vector<AutosaveReason> reasons) {
    return {.header = {.gameVersion = "0.1.0",
                       .rulesetId = "original",
                       .created = std::chrono::sys_seconds{std::chrono::seconds{0}},
                       .updated = std::chrono::sys_seconds{std::chrono::seconds{second}},
                       .playSeconds = second,
                       .label = "File City",
                       .autosaveReasons = autosaveReasonNames(reasons)},
            .sections = {}};
}

} // namespace

TEST_CASE("every reason has a name and a flag") {
    for (const AutosaveReason reason : AutosaveReasons) {
        const std::string_view name = autosaveReasonName(reason);
        CHECK(isValidName(name));
        CHECK(findAutosaveReason(name) == reason);
    }
    CHECK(autosaveReasonName(AutosaveReason::KeyItem) == "key_item");
    CHECK_FALSE(findAutosaveReason("from_a_newer_game"));
    for (const std::string_view flag : ReasonFlags) {
        CAPTURE(flag);
        CHECK(MapChangeOnly.contains(flag));
        std::string withoutFlag{MapChangeOnly};
        const std::size_t lineStart = withoutFlag.rfind('\n', withoutFlag.find(flag)) + 1;
        withoutFlag.erase(lineStart, withoutFlag.find('\n', lineStart) - lineStart + 1);
        CHECK(errorOf(readAutosaveRules(saveSection(withoutFlag))).contains(std::format("autosave has no '{}'", flag)));
    }
}

TEST_CASE("the save section says which reasons autosave") {
    const auto rules = readAutosaveRules(saveSection(MapChangeOnly));
    REQUIRE_MESSAGE(rules, errorOf(rules));
    CHECK(rules->savesOn(AutosaveReason::MapChange));
    for (const AutosaveReason reason : AutosaveReasons) {
        CHECK(rules->savesOn(reason) == (reason == AutosaveReason::MapChange));
    }
    CHECK(rules->coalesceSeconds == doctest::Approx(2.0));

    const auto missing = readAutosaveRules(saveSection("autosave:\n  on_map_change: true\n"));
    CHECK(errorOf(missing).starts_with("rules/test/save.yaml: "));
    CHECK(errorOf(readAutosaveRules(
                      saveSection(replaced(std::string{MapChangeOnly}, "on_map_change: true", "on_map_change: yes"))))
              .starts_with("rules/test/save.yaml: "));
    CHECK(errorOf(readAutosaveRules(saveSection(
                      replaced(std::string{MapChangeOnly}, "coalesce_seconds: 2", "coalesce_seconds: 0.5"))))
              .contains("coalesce_seconds must be 1 or more"));
    CHECK_FALSE(readAutosaveRules(
        saveSection(replaced(std::string{MapChangeOnly}, "coalesce_seconds: 2", "coalesce_seconds: .nan"))));
    CHECK(saveSectionCheck()(saveSection(MapChangeOnly)));
    CHECK_FALSE(saveSectionCheck()(saveSection("autosave: {}\n")));
}

TEST_CASE("the original ruleset autosaves on every reason, as approved") {
    const auto bytes = dwe::assets::DiskFileSystem{}.readFile(std::filesystem::path{DWE_GAME_PACK} / "rules" /
                                                              "original" / "save.yaml");
    REQUIRE(bytes);
    const auto rules =
        readAutosaveRules(saveSection(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()}));
    REQUIRE_MESSAGE(rules, errorOf(rules));
    for (const AutosaveReason reason : AutosaveReasons) {
        CAPTURE(autosaveReasonName(reason));
        CHECK(rules->savesOn(reason));
    }
    CHECK(rules->coalesceSeconds >= MinCoalesceSeconds);
}

TEST_CASE("autosave requests count only the ruleset's reasons, and only with slots") {
    const AutosaveRules rules = readAutosaveRules(saveSection(MapChangeOnly)).value();
    AutosaveRequests requests{rules, 2};
    CHECK_FALSE(requests.takeRequest());
    CHECK_FALSE(requests.requestAutosave(AutosaveReason::PartnerSleep));
    requests.advance(5.0F);
    CHECK_FALSE(requests.takeRequest());
    CHECK(requests.requestAutosave(AutosaveReason::MapChange));
    requests.advance(2.0F);
    CHECK(reasonNamesOf(requests.takeRequest()) == std::vector<std::string>{"map_change"});
    CHECK_FALSE(requests.takeRequest());

    AutosaveRules battleOnly = rules;
    battleOnly.setSavesOn(AutosaveReason::MapChange, false);
    battleOnly.setSavesOn(AutosaveReason::BattleEnd, true);
    requests.setRules(battleOnly);
    CHECK_FALSE(requests.requestAutosave(AutosaveReason::MapChange));
    CHECK(requests.requestAutosave(AutosaveReason::BattleEnd));

    AutosaveRequests noSlots{rules, 0};
    CHECK_FALSE(noSlots.requestAutosave(AutosaveReason::MapChange));
    CHECK_FALSE(AutosaveRequests(AutosaveRules{}, 2).requestAutosave(AutosaveReason::MapChange));
}

TEST_CASE("requests in one tick make one autosave with every reason, once each, in order") {
    AutosaveRequests requests{everyReason(), 3};
    CHECK(requests.requestAutosave(AutosaveReason::PartnerSleep));
    CHECK(requests.requestAutosave(AutosaveReason::NewDay));
    CHECK(requests.requestAutosave(AutosaveReason::PartnerSleep));
    CHECK_FALSE(requests.takeRequest());
    requests.advance(2.0F);
    CHECK(reasonNamesOf(requests.takeRequest()) == std::vector<std::string>{"partner_sleep", "new_day"});
    CHECK_FALSE(requests.takeRequest());
}

TEST_CASE("requests within the window join it; requests further apart make two autosaves") {
    AutosaveRequests requests{everyReason(), 3};
    requests.requestAutosave(AutosaveReason::BattleEnd);
    requests.advance(1.5F);
    requests.requestAutosave(AutosaveReason::Evolution);
    CHECK_FALSE(requests.takeRequest());
    // The window counts from the first request, so a stream of requests cannot put the autosave off for ever.
    requests.advance(0.5F);
    CHECK(reasonNamesOf(requests.takeRequest()) == std::vector<std::string>{"battle_end", "evolution"});

    requests.requestAutosave(AutosaveReason::MapChange);
    requests.advance(2.0F);
    CHECK(reasonNamesOf(requests.takeRequest()) == std::vector<std::string>{"map_change"});
    requests.advance(1.0F);
    requests.requestAutosave(AutosaveReason::DangerousArea);
    requests.advance(1.0F);
    CHECK_FALSE(requests.takeRequest());
    requests.advance(1.0F);
    CHECK(reasonNamesOf(requests.takeRequest()) == std::vector<std::string>{"dangerous_area"});
}

TEST_CASE("a disabled reason is ignored, also inside a window") {
    AutosaveRules rules = everyReason();
    rules.setSavesOn(AutosaveReason::KeyItem, false);
    AutosaveRequests requests{rules, 3};
    CHECK_FALSE(requests.requestAutosave(AutosaveReason::KeyItem));
    requests.advance(10.0F);
    CHECK_FALSE(requests.takeRequest());
    CHECK(requests.requestAutosave(AutosaveReason::RecruitJoin));
    CHECK_FALSE(requests.requestAutosave(AutosaveReason::KeyItem));
    requests.advance(2.0F);
    CHECK(reasonNamesOf(requests.takeRequest()) == std::vector<std::string>{"recruit_join"});
}

TEST_CASE("coalesced autosaves rotate over the three slots and record their reasons") {
    MemorySaveStorage storage;
    SaveSlots slots{storage, {.manualSlots = 15, .autosaveSlots = 3}};
    AutosaveRequests requests{everyReason(), 3};
    std::int64_t second = 100;
    const std::array events{AutosaveReason::MapChange, AutosaveReason::TournamentResult, AutosaveReason::StoryProgress,
                            AutosaveReason::PartnerReborn};
    std::vector<std::int32_t> written;
    for (const AutosaveReason event : events) {
        requests.requestAutosave(event);
        requests.requestAutosave(AutosaveReason::KeyItem);
        requests.advance(2.0F);
        second += 2;
        const std::vector<AutosaveReason> reasons = requests.takeRequest().value_or(std::vector<AutosaveReason>{});
        REQUIRE_FALSE(reasons.empty());
        const auto slot = slots.autosave(autosaveAt(second, reasons));
        REQUIRE_MESSAGE(slot, errorOf(slot));
        written.push_back(slot->index);
    }
    CHECK(written == std::vector<std::int32_t>{1, 2, 3, 1});
    const auto first = slots.load({.kind = SlotKind::Autosave, .index = 1});
    REQUIRE_MESSAGE(first, errorOf(first));
    CHECK(first->header.autosaveReasons == std::vector<std::string>{"partner_reborn", "key_item"});
    const auto third = slots.load({.kind = SlotKind::Autosave, .index = 3});
    REQUIRE_MESSAGE(third, errorOf(third));
    CHECK(third->header.autosaveReasons == std::vector<std::string>{"story_progress", "key_item"});
}

TEST_CASE("each reason has a localisation key") {
    CHECK(text_key::autosaveReason(AutosaveReason::NewDay) == "save.autosave_reason.new_day");
    CHECK(text_key::Autosaved.starts_with("save."));
}
