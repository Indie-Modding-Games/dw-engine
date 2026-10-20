#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::game::save {

// The ruleset section with the save rules: rules/<id>/save.yaml (content/schemas/ruleset_save.schema.json).
inline constexpr std::string_view SaveSectionName = "save";

// The events the game asks for an autosave on. Which of them save is up to the ruleset (AutosaveRules).
enum class AutosaveReason : std::uint8_t { MapChange, BattleEnd, PartnerSleep, NewDay };

// When the game saves by itself. The original never did: every flag off is the original's behaviour, and the
// `original` ruleset turns some on as an approved deviation (docs/architecture/save.md).
struct AutosaveRules {
    bool onMapChange = false;
    // After every battle, won or lost.
    bool onBattleEnd = false;
    // When the partner goes to sleep.
    bool onPartnerSleep = false;
    // On every new day of the game clock.
    bool onNewDay = false;

    // Whether a reason saves.
    [[nodiscard]] bool savesOn(AutosaveReason reason) const;
};

// Reads the autosave rules of the save section; errors name the file, line and column.
core::Result<AutosaveRules> readAutosaveRules(const ruleset::Section& section);

// Reads the autosave rules of a ruleset; a ruleset without a save section never autosaves, as the original.
core::Result<AutosaveRules> readAutosaveRules(const ruleset::Ruleset& ruleset);

// The check of the save section for the ruleset store.
ruleset::SectionCheck saveSectionCheck();

// The autosave requests of the game: events ask, the ruleset decides which reasons count.
class AutosaveRequests {
public:
    // With no autosave slots no reason counts.
    AutosaveRequests(const AutosaveRules& rules, std::int32_t autosaveSlots);

    // Uses new rules, after the ruleset is reloaded or tuned.
    void setRules(const AutosaveRules& rules);

    // Asks for an autosave; returns whether the request was taken.
    bool requestAutosave(AutosaveReason reason);

    // The reason of the pending request, once; the game then builds its SaveFile and calls SaveSlots::autosave.
    std::optional<AutosaveReason> takeRequest();

private:
    AutosaveRules rules_;
    bool hasSlots_;
    std::optional<AutosaveReason> pending_;
};

} // namespace dwe::game::save
