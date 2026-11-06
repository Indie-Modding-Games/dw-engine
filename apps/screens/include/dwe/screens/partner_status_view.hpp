#pragma once

#include <dwe/game/battle/combatant.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/partner_moves.hpp>
#include <dwe/session/partner_view.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::screens {

// The settings of the partner's status page (ui/partner_menu/document), which refreshes on
// PartnerRevisionSetting.
inline constexpr std::string_view PartnerRevisionSetting = "partner.revision";
inline constexpr std::string_view PartnerNameSetting = "partner.name";
// The name the player gave the partner (PARTNER_ENTITY.name), empty when none.
inline constexpr std::string_view PartnerNicknameSetting = "partner.nickname";
inline constexpr std::string_view PartnerAgeSetting = "partner.age";
inline constexpr std::string_view PartnerWeightSetting = "partner.weight";
inline constexpr std::string_view PartnerLivesSetting = "partner.lives";
inline constexpr std::string_view PartnerHpSetting = "partner.hp";
inline constexpr std::string_view PartnerMaxHpSetting = "partner.max_hp";
inline constexpr std::string_view PartnerMpSetting = "partner.mp";
inline constexpr std::string_view PartnerMaxMpSetting = "partner.max_mp";
inline constexpr std::string_view PartnerPoolList = "partner.pool";
inline constexpr std::string_view PartnerStatList = "partner.stat";
inline constexpr std::string_view PartnerMeterList = "partner.meter";
inline constexpr std::string_view PartnerConditionList = "partner.condition";
// What the original keeps hidden, for the page's extras (the status_extras option): the tiredness
// bar (partner.tiredness.label, .value, .min, .max) and the count of care mistakes.
inline constexpr std::string_view PartnerTirednessSetting = "partner.tiredness";
inline constexpr std::string_view PartnerCareMistakesSetting = "partner.care_mistakes";
// More of the extras: the battles fought, the hours of life left, the evolution counter and the
// hours its level checks from (-1 for a level that never evolves on its own), and the hours it is
// awake today, from waking up to getting sleepy.
inline constexpr std::string_view PartnerBattlesSetting = "partner.battles";
inline constexpr std::string_view PartnerLifetimeSetting = "partner.lifetime_hours";
inline constexpr std::string_view PartnerEvolutionHoursSetting = "partner.evolution.hours";
inline constexpr std::string_view PartnerEvolutionCheckSetting = "partner.evolution.check_hours";
inline constexpr std::string_view PartnerWakeHourSetting = "partner.awake.wake_hour";
inline constexpr std::string_view PartnerWakeMinuteSetting = "partner.awake.wake_minute";
inline constexpr std::string_view PartnerSleepyHourSetting = "partner.awake.sleepy_hour";
inline constexpr std::string_view PartnerSleepyMinuteSetting = "partner.awake.sleepy_minute";

// A bar of the status page: its label, value and range. A two-tone bar fills its whole width once
// from the minimum to the middle of the range, then fills it again in a second colour from the
// middle to the maximum (game_menu.c: renderDigimonStatsView draws happiness and discipline so).
struct MeterView {
  std::string labelKey;
  std::int32_t value = 0;
  std::int32_t min = 0;
  std::int32_t max = 0;
  bool twoTone = false;

  friend bool operator==(const MeterView&, const MeterView&) = default;
};

// What the status page shows, in the original's order (game_menu.c: renderDigimonStatsView): the
// name, age and weight; HP and MP, now and at most, with a bar of the maximum; offense, defense,
// speed and brain; the lives; the conditions; and the bars of happiness, discipline and the virus.
struct TechniqueView {
  std::string nameKey;
  std::int32_t power = 0;
  std::int32_t mpCost = 0;
  std::int32_t range = 0;
  std::int32_t effect = 0;
};

struct TechniqueChoiceView {
  std::optional<TechniqueView> technique;
  bool available = false;
  bool equipped = false;
  bool known = false;
  std::int32_t speciesSlot = -1;
  std::int32_t special = -1;
};

struct PartnerStatusView {
  // The species' name key and the partner's own name (game_menu.c: drawDigimonStatsStrings draws
  // both).
  std::string nameKey;
  std::string nickname;
  std::array<std::optional<std::int32_t>, 3> specials{};
  std::int32_t type = 0;
  std::int32_t activePattern = -1;
  std::int32_t ageDays = 0;
  std::int32_t weight = 0;
  std::int32_t lives = 0;
  std::int32_t hp = 0;
  std::int32_t maxHp = 0;
  std::int32_t mp = 0;
  std::int32_t maxMp = 0;
  // The bars of HP and MP: their maximums over 9999, as the original draws them under the numbers.
  std::array<MeterView, 2> pools;
  // Offense, defense, speed and brain, with the original's labels (main.stat_labels).
  std::array<MeterView, 4> stats;
  std::vector<MeterView> meters;
  // Extras the original does not show (the page draws them with the status_extras option): the
  // tiredness, 0 to 100, as a plain bar, and the care mistakes made with this partner.
  MeterView tiredness;
  std::int32_t careMistakes = 0;
  // The rest of the extras: the battles fought (they count for evolution), the hours of life left
  // (remainingLifetime), the evolution counter (evoTimer) with the hours its level checks from,
  // and today's waking hours (wakeUpHour to sleepyHour).
  std::int32_t battles = 0;
  std::int32_t lifetimeHours = 0;
  std::int32_t evolutionHours = 0;
  std::optional<std::int32_t> evolutionCheckHours;
  std::int32_t wakeHour = 0;
  std::int32_t wakeMinute = 0;
  std::int32_t sleepyHour = 0;
  std::int32_t sleepyMinute = 0;
  // The keys of the conditions it has (partner_menu.condition.<name>).
  std::vector<std::string> conditionKeys;
  std::array<TechniqueChoiceView, 56> techniqueChoices{};
  bool canFinishTechniqueEdit = false;
  // Tres técnicas y el final: datos del catálogo que también utiliza el combate.
  std::array<std::optional<TechniqueView>, game::battle::EquippedSlots> techniques{};
};

[[nodiscard]] PartnerStatusView partnerStatusViewOf(const session::PartnerView& partner,
                                                    const NameKeys& names);

[[nodiscard]] std::array<std::optional<TechniqueView>, game::battle::EquippedSlots>
equippedTechniqueViewsOf(const game::battle::Combatant& partner,
                         const game::battle::MoveCatalog& catalog, const NameKeys& names);

void setProfileSpecies(PartnerStatusView& view, const assets::DigimonRow& species,
                       std::int32_t activePattern);

void setTechniqueChoices(PartnerStatusView& view, const assets::DigimonRow& species,
                         const session::PartnerMoves& moves,
                         const game::battle::MoveCatalog& catalog, const NameKeys& names);

[[nodiscard]] ScreenSettings settingsOf(const PartnerStatusView& view);

} // namespace dwe::screens
