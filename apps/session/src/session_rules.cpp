#include <dwe/assets/yaml_reader.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/session_rules.hpp>

#include <array>
#include <cstddef>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
using game::partner::Stat;
using messages::formatMessage;

constexpr std::string_view PartnerKey = "partner";
constexpr std::string_view StatsKey = "stats";
constexpr std::string_view TamerLevelKey = "tamer_level";
constexpr std::string_view LevelMinKey = "level_min";
constexpr std::string_view LevelMaxKey = "level_max";

// The keys of the stats, in the order of game::partner::AllStats.
constexpr std::array<std::string_view, game::partner::StatCount> StatKeys{"hp",      "mp",    "offense",
                                                                          "defense", "speed", "brain"};

std::int32_t nonNegative(YamlReader& reader, const YamlNode& parent, std::string_view key) {
    const YamlNode& node = reader.field(parent, key);
    const auto value = reader.integer<std::int32_t>(node);
    if (reader.ok() && value < 0) {
        reader.fail(node, formatMessage(messages::NegativeValue, key));
    }
    return value;
}

game::partner::StatBlock readStats(YamlReader& reader, const YamlNode& node) {
    game::partner::StatBlock stats;
    if (!reader.expectKeys(node, messages::StatsText,
                           {StatKeys[0], StatKeys[1], StatKeys[2], StatKeys[3], StatKeys[4], StatKeys[5]})) {
        return stats;
    }
    for (std::size_t index = 0; index < StatKeys.size(); ++index) {
        stats[game::partner::AllStats[index]] = nonNegative(reader, node, StatKeys[index]);
    }
    return stats;
}

core::Result<void> checkStartLevel(const SessionRules& rules) {
    if (rules.newGame.tamerLevel < rules.tamer.levelMin || rules.newGame.tamerLevel > rules.tamer.levelMax) {
        return core::fail(formatMessage(messages::StartLevelOutside, rules.newGame.tamerLevel, rules.tamer.levelMin,
                                        rules.tamer.levelMax));
    }
    return {};
}

} // namespace

core::Result<NewGameRules> readNewGameRules(const ruleset::Section& section) {
    YamlReader reader;
    const YamlNode& root = section.node;
    if (!reader.expectKeys(root, messages::NewGameSectionText, {PartnerKey, StatsKey, TamerLevelKey})) {
        return std::unexpected{reader.error(section.file)};
    }
    NewGameRules rules;
    const YamlNode& partner = reader.field(root, PartnerKey);
    rules.partner = reader.ok() ? reader.text(partner) : std::string{};
    rules.stats = readStats(reader, reader.field(root, StatsKey));
    rules.tamerLevel = nonNegative(reader, root, TamerLevelKey);
    return reader.finish(std::move(rules), section.file);
}

core::Result<TamerRules> readTamerRules(const ruleset::Section& section) {
    YamlReader reader;
    const YamlNode& root = section.node;
    if (!reader.expectKeys(root, messages::TamerSectionText, {LevelMinKey, LevelMaxKey})) {
        return std::unexpected{reader.error(section.file)};
    }
    const TamerRules rules{.levelMin = nonNegative(reader, root, LevelMinKey),
                           .levelMax = nonNegative(reader, root, LevelMaxKey)};
    if (reader.ok() && rules.levelMin > rules.levelMax) {
        reader.fail(root, formatMessage(messages::LevelLimitsReversed, rules.levelMin, rules.levelMax));
    }
    return reader.finish(rules, section.file);
}

core::Result<SessionRules> readSessionRules(const ruleset::Ruleset& ruleset) {
    auto newGameSection = ruleset.requireSection(NewGameSection);
    if (!newGameSection) {
        return std::unexpected{newGameSection.error()};
    }
    auto tamerSection = ruleset.requireSection(TamerSection);
    if (!tamerSection) {
        return std::unexpected{tamerSection.error()};
    }
    auto newGame = readNewGameRules(**newGameSection);
    if (!newGame) {
        return std::unexpected{newGame.error()};
    }
    auto tamer = readTamerRules(**tamerSection);
    if (!tamer) {
        return std::unexpected{tamer.error()};
    }
    SessionRules rules{.newGame = std::move(*newGame), .tamer = *tamer};
    if (auto checked = checkStartLevel(rules); !checked) {
        return core::fail(formatMessage(messages::InFile, (*newGameSection)->file, checked.error().message));
    }
    return rules;
}

ruleset::SectionChecks sessionSectionChecks() {
    return {
        {std::string{NewGameSection},
         [](const ruleset::Section& section) -> core::Result<void> {
             if (auto read = readNewGameRules(section); !read) {
                 return std::unexpected{read.error()};
             }
             return {};
         }},
        {std::string{TamerSection},
         [](const ruleset::Section& section) -> core::Result<void> {
             if (auto read = readTamerRules(section); !read) {
                 return std::unexpected{read.error()};
             }
             return {};
         }},
    };
}

} // namespace dwe::session
