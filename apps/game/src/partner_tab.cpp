#include "partner_tab.hpp"

#include <dwe/game/partner/species.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/session/partner_view.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::app {
namespace {

// The developer's names of the tab: the panel is a tool, not the game's UI, so they are not localised.
constexpr std::string_view TabName = "Partner";
constexpr std::string_view YesText = "yes";
constexpr std::string_view NoText = "no";
constexpr std::string_view NoneText = "-";
constexpr std::string_view ListSeparator = ", ";
constexpr std::string_view MeterFormat = "{} / {}";

constexpr std::array<std::string_view, 6> LevelNames{"none", "baby", "in training", "rookie", "champion", "ultimate"};
constexpr std::array<std::string_view, game::partner::ConditionCount> ConditionNames{
    "sleepy", "tired", "hungry", "needs toilet", "sulking", "injured", "sick"};
constexpr std::array<std::string_view, game::partner::StatCount> StatNames{"HP",      "MP",    "offense",
                                                                           "defense", "speed", "brain"};

std::string conditionsText(const game::partner::Conditions& conditions) {
    std::string text;
    for (std::size_t index = 0; index < ConditionNames.size(); ++index) {
        if (conditions.has(static_cast<game::partner::Condition>(index))) {
            text += text.empty() ? "" : ListSeparator;
            text += ConditionNames[index];
        }
    }
    return text.empty() ? std::string{NoneText} : text;
}

debug::TuningRow meterRow(std::string_view label, const session::CareMeter& meter) {
    return {
        .label = std::string{label}, .value = std::format(MeterFormat, meter.value, meter.max), .meter = meter.fill()};
}

debug::TuningRow textRow(std::string_view label, std::string value) {
    return {.label = std::string{label}, .value = std::move(value), .meter = std::nullopt};
}

std::vector<debug::TuningRow> rowsOf(const session::PartnerView& view) {
    std::vector<debug::TuningRow> rows{
        textRow("species", view.species),
        textRow("level", std::string{LevelNames.at(static_cast<std::size_t>(view.level))}),
        textRow("alive", std::string{view.alive ? YesText : NoText}),
        textRow("age (days)", std::to_string(view.ageDays)),
        textRow("lives", std::to_string(view.lives)),
        textRow("lifetime left (h)", std::to_string(view.lifetimeHoursLeft)),
        textRow("conditions", conditionsText(view.conditions)),
        meterRow("happiness", view.happiness),
        meterRow("discipline", view.discipline),
        meterRow("tiredness", view.tiredness),
        meterRow("energy", view.energy),
        meterRow("weight", view.weight),
        meterRow("virus bar", view.virusBar),
        textRow("next meal (min)", std::to_string(view.minutesToMeal)),
        textRow("toilet in (steps)", std::to_string(view.stepsToToilet)),
        textRow("care mistakes", std::to_string(view.careMistakes)),
        textRow("battles", std::to_string(view.battles)),
        textRow("HP left", std::format(MeterFormat, view.currentHp, view.stats[game::partner::Stat::Hp])),
        textRow("MP left", std::format(MeterFormat, view.currentMp, view.stats[game::partner::Stat::Mp])),
    };
    for (std::size_t index = 0; index < StatNames.size(); ++index) {
        rows.push_back(textRow(StatNames[index], std::to_string(view.stats[game::partner::AllStats[index]])));
    }
    return rows;
}

} // namespace

debug::TuningTab partnerTab(const session::GameSession& session) {
    return {.name = std::string{TabName}, .rows = [&session] { return rowsOf(session.partnerView()); }};
}

} // namespace dwe::app
