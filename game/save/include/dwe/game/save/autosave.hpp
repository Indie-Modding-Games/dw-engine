#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::save {

// The ruleset section with the save rules: rules/<id>/save.yaml
// (content/schemas/ruleset_save.schema.json).
inline constexpr std::string_view SaveSectionName = "save";

// The events the game asks for an autosave on. Which of them save is up to the ruleset
// (AutosaveRules); the system event behind each one is in docs/architecture/save.md.
enum class AutosaveReason : std::uint8_t {
  MapChange,
  BattleEnd,
  PartnerSleep,
  NewDay,
  Evolution,
  TournamentResult,
  RecruitJoin,
  KeyItem,
  DangerousArea,
  StoryProgress,
  PartnerReborn,
};

// Every autosave reason, in the order of the enum.
inline constexpr std::array AutosaveReasons{
    AutosaveReason::MapChange,     AutosaveReason::BattleEnd,     AutosaveReason::PartnerSleep,
    AutosaveReason::NewDay,        AutosaveReason::Evolution,     AutosaveReason::TournamentResult,
    AutosaveReason::RecruitJoin,   AutosaveReason::KeyItem,       AutosaveReason::DangerousArea,
    AutosaveReason::StoryProgress, AutosaveReason::PartnerReborn,
};
static_assert(std::to_underlying(AutosaveReasons.back()) + 1U == AutosaveReasons.size());

// The shortest coalescing window. Autosave slots rotate by their `updated` time, which has whole
// seconds: autosaves at least a second apart never tie.
inline constexpr float MinCoalesceSeconds = 1.0F;

// The name of a reason, as the save header lists it (`map_change`).
[[nodiscard]] std::string_view autosaveReasonName(AutosaveReason reason);

// The reason with a name, if this game knows it.
[[nodiscard]] std::optional<AutosaveReason> findAutosaveReason(std::string_view name);

// The names of some reasons, in their order, for SaveHeader::autosaveReasons.
[[nodiscard]] std::vector<std::string> autosaveReasonNames(std::span<const AutosaveReason> reasons);

// When the game saves by itself. The original never did: every reason off is the original's
// behaviour, and the `original` ruleset turns them on as an approved deviation
// (docs/architecture/save.md).
struct AutosaveRules {
  // Whether each reason saves, indexed by reason.
  std::array<bool, AutosaveReasons.size()> savingReasons{};
  // Requests closer than this many real seconds make one autosave.
  float coalesceSeconds = MinCoalesceSeconds;

  // Whether a reason saves.
  [[nodiscard]] bool savesOn(AutosaveReason reason) const;

  // Turns a reason on or off.
  void setSavesOn(AutosaveReason reason, bool saves);
};

// Reads the autosave rules of the save section; errors name the file, line and column.
core::Result<AutosaveRules> readAutosaveRules(const ruleset::Section& section);

// Reads the autosave rules of a ruleset; a ruleset without a save section never autosaves, as the
// original.
core::Result<AutosaveRules> readAutosaveRules(const ruleset::Ruleset& ruleset);

// The check of the save section for the ruleset store.
ruleset::SectionCheck saveSectionCheck();

// The autosave requests of the game: events ask, the ruleset decides which reasons count, and
// requests within the coalescing window make one autosave.
class AutosaveRequests {
public:
  // With no autosave slots no reason counts.
  AutosaveRequests(const AutosaveRules& rules, std::int32_t autosaveSlots);

  // Uses new rules, after the ruleset is reloaded or tuned. A pending autosave stays pending.
  void setRules(const AutosaveRules& rules);

  // Asks for an autosave; returns whether the request was taken. The first request opens the
  // coalescing window and later ones join it.
  bool requestAutosave(AutosaveReason reason);

  // Moves the coalescing window on by the real seconds of a game loop tick.
  void advance(float seconds);

  // Once the window of the pending autosave is over, its reasons in the order they were first
  // asked, each once. The game then builds its SaveFile, records them (autosaveReasonNames) and
  // calls SaveSlots::autosave.
  std::optional<std::vector<AutosaveReason>> takeRequest();

private:
  AutosaveRules rules_;
  bool hasSlots_;
  std::vector<AutosaveReason> pendingReasons_;
  float windowLeftSeconds_ = 0.0F;
};

} // namespace dwe::game::save
