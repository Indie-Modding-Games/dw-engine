#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/save/autosave.hpp>

#include <algorithm>
#include <format>
#include <utility>

#include "keys.hpp"
#include "messages.hpp"

namespace dwe::game::save {

namespace {

// How a reason is named in save headers and which ruleset flag turns it on.
struct ReasonKeys {
    AutosaveReason reason;
    std::string_view name;
    std::string_view flag;
};

constexpr std::array ReasonTable{
    ReasonKeys{.reason = AutosaveReason::MapChange, .name = reason_name::MapChange, .flag = section_key::OnMapChange},
    ReasonKeys{.reason = AutosaveReason::BattleEnd, .name = reason_name::BattleEnd, .flag = section_key::OnBattleEnd},
    ReasonKeys{
        .reason = AutosaveReason::PartnerSleep, .name = reason_name::PartnerSleep, .flag = section_key::OnPartnerSleep},
    ReasonKeys{.reason = AutosaveReason::NewDay, .name = reason_name::NewDay, .flag = section_key::OnNewDay},
    ReasonKeys{.reason = AutosaveReason::Evolution, .name = reason_name::Evolution, .flag = section_key::OnEvolution},
    ReasonKeys{.reason = AutosaveReason::TournamentResult,
               .name = reason_name::TournamentResult,
               .flag = section_key::OnTournamentResult},
    ReasonKeys{
        .reason = AutosaveReason::RecruitJoin, .name = reason_name::RecruitJoin, .flag = section_key::OnRecruitJoin},
    ReasonKeys{.reason = AutosaveReason::KeyItem, .name = reason_name::KeyItem, .flag = section_key::OnKeyItem},
    ReasonKeys{.reason = AutosaveReason::DangerousArea,
               .name = reason_name::DangerousArea,
               .flag = section_key::OnDangerousArea},
    ReasonKeys{.reason = AutosaveReason::StoryProgress,
               .name = reason_name::StoryProgress,
               .flag = section_key::OnStoryProgress},
    ReasonKeys{.reason = AutosaveReason::PartnerReborn,
               .name = reason_name::PartnerReborn,
               .flag = section_key::OnPartnerReborn},
};

constexpr bool tableFollowsEnum() {
    return std::ranges::equal(ReasonTable, AutosaveReasons,
                              [](const ReasonKeys& keys, AutosaveReason reason) { return keys.reason == reason; });
}
static_assert(tableFollowsEnum(), "ReasonTable lists every AutosaveReason in enum order");

const ReasonKeys& keysOf(AutosaveReason reason) {
    return ReasonTable.at(std::to_underlying(reason));
}

AutosaveRules readReasons(assets::YamlReader& reader, const core::YamlNode& autosave) {
    AutosaveRules rules{.savingReasons = {},
                        .coalesceSeconds = reader.number(reader.field(autosave, section_key::CoalesceSeconds))};
    for (const ReasonKeys& keys : ReasonTable) {
        rules.setSavesOn(keys.reason, reader.boolean(reader.field(autosave, keys.flag)));
    }
    // Written as "not at least" so that NaN fails too.
    if (reader.ok() && !(rules.coalesceSeconds >= MinCoalesceSeconds)) {
        reader.fail(reader.field(autosave, section_key::CoalesceSeconds),
                    std::format(messages::CoalesceTooShort, MinCoalesceSeconds));
    }
    return rules;
}

} // namespace

std::string_view autosaveReasonName(AutosaveReason reason) {
    return keysOf(reason).name;
}

std::optional<AutosaveReason> findAutosaveReason(std::string_view name) {
    const auto found = std::ranges::find(ReasonTable, name, &ReasonKeys::name);
    return found == ReasonTable.end() ? std::nullopt : std::optional{found->reason};
}

std::vector<std::string> autosaveReasonNames(std::span<const AutosaveReason> reasons) {
    std::vector<std::string> names;
    names.reserve(reasons.size());
    for (const AutosaveReason reason : reasons) {
        names.emplace_back(autosaveReasonName(reason));
    }
    return names;
}

bool AutosaveRules::savesOn(AutosaveReason reason) const {
    return savingReasons.at(std::to_underlying(reason));
}

void AutosaveRules::setSavesOn(AutosaveReason reason, bool saves) {
    savingReasons.at(std::to_underlying(reason)) = saves;
}

core::Result<AutosaveRules> readAutosaveRules(const ruleset::Section& section) {
    assets::YamlReader reader;
    if (!reader.expectKeys(section.node, messages::SaveSectionWhat, {section_key::Autosave})) {
        return std::unexpected{reader.error(section.file)};
    }
    const core::YamlNode& autosave = reader.field(section.node, section_key::Autosave);
    if (!reader.expectKeys(autosave, messages::AutosaveWhat,
                           {section_key::OnMapChange, section_key::OnBattleEnd, section_key::OnPartnerSleep,
                            section_key::OnNewDay, section_key::OnEvolution, section_key::OnTournamentResult,
                            section_key::OnRecruitJoin, section_key::OnKeyItem, section_key::OnDangerousArea,
                            section_key::OnStoryProgress, section_key::OnPartnerReborn,
                            section_key::CoalesceSeconds})) {
        return std::unexpected{reader.error(section.file)};
    }
    const AutosaveRules rules = readReasons(reader, autosave);
    return reader.finish(rules, section.file);
}

core::Result<AutosaveRules> readAutosaveRules(const ruleset::Ruleset& ruleset) {
    const ruleset::Section* section = ruleset.section(SaveSectionName);
    if (section == nullptr) {
        return AutosaveRules{};
    }
    return readAutosaveRules(*section);
}

ruleset::SectionCheck saveSectionCheck() {
    return [](const ruleset::Section& section) -> core::Result<void> {
        return readAutosaveRules(section).transform([](const AutosaveRules& /*rules*/) {});
    };
}

AutosaveRequests::AutosaveRequests(const AutosaveRules& rules, std::int32_t autosaveSlots)
    : rules_{rules}, hasSlots_{autosaveSlots > 0} {}

void AutosaveRequests::setRules(const AutosaveRules& rules) {
    rules_ = rules;
}

bool AutosaveRequests::requestAutosave(AutosaveReason reason) {
    if (!hasSlots_ || !rules_.savesOn(reason)) {
        return false;
    }
    if (pendingReasons_.empty()) {
        windowLeftSeconds_ = rules_.coalesceSeconds;
    }
    if (std::ranges::find(pendingReasons_, reason) == pendingReasons_.end()) {
        pendingReasons_.push_back(reason);
    }
    return true;
}

void AutosaveRequests::advance(float seconds) {
    if (!pendingReasons_.empty()) {
        windowLeftSeconds_ -= seconds;
    }
}

std::optional<std::vector<AutosaveReason>> AutosaveRequests::takeRequest() {
    if (pendingReasons_.empty() || windowLeftSeconds_ > 0.0F) {
        return std::nullopt;
    }
    return std::exchange(pendingReasons_, {});
}

} // namespace dwe::game::save
