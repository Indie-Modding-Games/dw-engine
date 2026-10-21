#include <dwe/formats/script/new_game_stats.hpp>

#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::formats::script {

namespace {

using core::YamlNode;

// include/dw/script.h.
constexpr std::uint8_t StoreDigimonTypeOp = 0x22;
constexpr std::uint8_t IfOp = 0x19;
constexpr std::uint8_t SetStatOp = 0x34;
constexpr std::uint8_t JumpToOp = 0x16;
// script_value.c: scriptCompare*: the low 3 bits of an IF entry's code are the comparison; 0 is equality.
constexpr unsigned CompareMask = 0x07U;
constexpr unsigned CompareEqual = 0;

constexpr std::array<std::string_view, StartingStatCount> StatKeys = {"offense", "defense", "speed", "brains",
                                                                      "max_hp",  "max_mp",  "hp",    "mp"};

std::optional<std::int32_t> argumentOf(std::span<const Argument> arguments, std::string_view name) {
    const auto found = std::ranges::find(arguments, name, &Argument::name);
    return found == arguments.end() ? std::nullopt : std::optional{found->value};
}

// Reads a run of SET_STAT from `index` that sets every stat once; returns the index after it.
std::optional<std::size_t> readRun(std::span<const Instruction> code, std::size_t index, StartingStats& stats) {
    std::array<bool, StartingStatCount> seen{};
    for (std::size_t count = 0; count < StartingStatCount; ++count, ++index) {
        if (index >= code.size() || code[index].opcode != SetStatOp) {
            return std::nullopt;
        }
        const auto stat = argumentOf(code[index].arguments, "stat");
        const auto value = argumentOf(code[index].arguments, "value");
        if (!stat || !value || *stat < 0 || std::cmp_greater_equal(*stat, StartingStatCount)) {
            return std::nullopt;
        }
        const auto slot = static_cast<std::size_t>(*stat);
        if (seen.at(slot)) {
            return std::nullopt;
        }
        seen.at(slot) = true;
        stats.at(slot) = *value;
    }
    return index;
}

std::optional<std::size_t> indexAt(std::span<const Instruction> code, std::int32_t offset) {
    const auto found = std::ranges::find(code, static_cast<std::uint32_t>(offset), &Instruction::offset);
    return found == code.end() ? std::nullopt : std::optional{static_cast<std::size_t>(found - code.begin())};
}

// The block that starts with the STORE_DIGIMON_TYPE at `index`, if it is one.
std::optional<NewGameStats> blockAt(std::span<const Instruction> code, std::size_t index) {
    if (index + 1 >= code.size() || code[index].opcode != StoreDigimonTypeOp || code[index + 1].opcode != IfOp) {
        return std::nullopt;
    }
    const auto variable = argumentOf(code[index].arguments, "pstat");
    const Instruction& branch = code[index + 1];
    if (!variable || branch.conditions.size() != 2) {
        return std::nullopt;
    }
    const Condition& test = branch.conditions[0];
    const Condition& orElse = branch.conditions[1];
    const auto tested = argumentOf(test.arguments, "pstat");
    const auto type = argumentOf(test.arguments, "value");
    const auto elseAt = argumentOf(orElse.arguments, "target");
    if (test.test != ConditionTest::Pstat || test.combine != Combine::Replace ||
        (test.code & CompareMask) != CompareEqual || tested != variable || !type ||
        orElse.test != ConditionTest::GotoIfFalse || !elseAt) {
        return std::nullopt;
    }
    NewGameStats stats{.at = branch.offset, .partnerType = *type, .partner = {}, .others = {}};
    const auto jump = readRun(code, index + 2, stats.partner);
    if (!jump || *jump >= code.size() || code[*jump].opcode != JumpToOp || indexAt(code, *elseAt) != *jump + 1) {
        return std::nullopt;
    }
    const auto after = readRun(code, *jump + 1, stats.others);
    const auto over = argumentOf(code[*jump].arguments, "target");
    if (!after || !over || indexAt(code, *over) != *after) {
        return std::nullopt;
    }
    return stats;
}

YamlNode statsToYaml(const StartingStats& stats) {
    YamlNode node = YamlNode::mapping();
    for (std::size_t stat = 0; stat < StartingStatCount; ++stat) {
        node.set(std::string{StatKeys.at(stat)}, yaml::number(stats.at(stat)));
    }
    return node;
}

} // namespace

core::Result<NewGameStats> findNewGameStats(const Disassembly& disassembly) {
    const std::span<const Instruction> code = disassembly.instructions;
    for (std::size_t index = 0; index < code.size(); ++index) {
        if (auto stats = blockAt(code, index)) {
            return *stats;
        }
    }
    return core::fail("no IF on the partner's type that sets every stat in both branches");
}

YamlNode newGameStatsToYaml(const NewGameStats& stats, const std::string& partnerId) {
    YamlNode partner = YamlNode::mapping();
    partner.set("digimon", YamlNode::scalar(partnerId));
    partner.set("stats", statsToYaml(stats.partner));
    YamlNode partners = YamlNode::sequence();
    partners.append(std::move(partner));
    YamlNode node = YamlNode::mapping();
    node.set("script", yaml::number(NewGameScript));
    node.set("at", yaml::number(stats.at));
    node.set("partners", std::move(partners));
    node.set("others", statsToYaml(stats.others));
    return node;
}

} // namespace dwe::formats::script
