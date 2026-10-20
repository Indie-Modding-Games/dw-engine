#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/save/autosave.hpp>

#include <utility>

#include "keys.hpp"
#include "messages.hpp"

namespace dwe::game::save {

bool AutosaveRules::savesOn(AutosaveReason reason) const {
    switch (reason) {
    case AutosaveReason::MapChange:
        return onMapChange;
    case AutosaveReason::BattleEnd:
        return onBattleEnd;
    case AutosaveReason::PartnerSleep:
        return onPartnerSleep;
    case AutosaveReason::NewDay:
        break;
    }
    return onNewDay;
}

core::Result<AutosaveRules> readAutosaveRules(const ruleset::Section& section) {
    assets::YamlReader reader;
    if (!reader.expectKeys(section.node, messages::SaveSectionWhat, {section_key::Autosave})) {
        return std::unexpected{reader.error(section.file)};
    }
    const core::YamlNode& autosave = reader.field(section.node, section_key::Autosave);
    if (!reader.expectKeys(
            autosave, messages::AutosaveWhat,
            {section_key::OnMapChange, section_key::OnBattleEnd, section_key::OnPartnerSleep, section_key::OnNewDay})) {
        return std::unexpected{reader.error(section.file)};
    }
    const AutosaveRules rules{.onMapChange = reader.boolean(reader.field(autosave, section_key::OnMapChange)),
                              .onBattleEnd = reader.boolean(reader.field(autosave, section_key::OnBattleEnd)),
                              .onPartnerSleep = reader.boolean(reader.field(autosave, section_key::OnPartnerSleep)),
                              .onNewDay = reader.boolean(reader.field(autosave, section_key::OnNewDay))};
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
    pending_ = reason;
    return true;
}

std::optional<AutosaveReason> AutosaveRequests::takeRequest() {
    return std::exchange(pending_, std::nullopt);
}

} // namespace dwe::game::save
