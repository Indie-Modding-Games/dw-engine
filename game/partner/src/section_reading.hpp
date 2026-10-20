#pragma once

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/partner/state.hpp>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

// Helpers the readers of the partner's sections share.
namespace dwe::game::partner::reading {

using assets::YamlReader;
using core::YamlNode;

// The keys of a MoodChange mapping.
inline constexpr std::string_view HappinessKey = "happiness";
inline constexpr std::string_view DisciplineKey = "discipline";
inline constexpr std::string_view TirednessKey = "tiredness";

// A mapping field with exactly these keys; false (and the reader failed) otherwise.
bool group(YamlReader& reader, const YamlNode& parent, std::string_view key,
           std::initializer_list<std::string_view> keys, const YamlNode*& node);

std::int32_t integer(YamlReader& reader, const YamlNode& parent, std::string_view key);
std::int32_t nonNegative(YamlReader& reader, const YamlNode& parent, std::string_view key);
// For periods, divisors and counts: a zero would divide by zero.
std::int32_t positive(YamlReader& reader, const YamlNode& parent, std::string_view key);
bool flag(YamlReader& reader, const YamlNode& parent, std::string_view key);

// A mapping of any of happiness, discipline and tiredness; the missing ones are 0.
MoodChange moodChange(YamlReader& reader, const YamlNode& parent, std::string_view key);

// A stat name (hp, mp, offense, defense, speed, brain).
std::optional<Stat> stat(YamlReader& reader, const YamlNode& node);

} // namespace dwe::game::partner::reading
