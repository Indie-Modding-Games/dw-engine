#include "section_reading.hpp"

#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/training_rules.hpp>

#include <algorithm>

namespace dwe::game::partner::reading {

using messages::formatMessage;

bool group(YamlReader& reader, const YamlNode& parent, std::string_view key,
           std::initializer_list<std::string_view> keys, const YamlNode*& node) {
  node = &reader.field(parent, key);
  return reader.ok() && reader.expectKeys(*node, key, keys);
}

std::int32_t integer(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  if (!reader.ok()) {
    return 0;
  }
  return reader.integer<std::int32_t>(node);
}

std::int32_t nonNegative(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  if (!reader.ok()) {
    return 0;
  }
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && value < 0) {
    reader.fail(node, formatMessage(messages::Negative, key));
  }
  return value;
}

std::int32_t positive(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  if (!reader.ok()) {
    return 0;
  }
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && value <= 0) {
    reader.fail(node, formatMessage(messages::NotPositive, key));
  }
  return value;
}

bool flag(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  if (!reader.ok()) {
    return false;
  }
  return reader.boolean(node);
}

MoodChange moodChange(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  if (!reader.ok() ||
      !reader.expectKeys(node, key, {}, {HappinessKey, DisciplineKey, TirednessKey})) {
    return {};
  }
  const auto optional = [&](std::string_view name) {
    const YamlNode* value = node.find(name);
    return value == nullptr ? 0 : reader.integer<std::int32_t>(*value);
  };
  return {.happiness = optional(HappinessKey),
          .discipline = optional(DisciplineKey),
          .tiredness = optional(TirednessKey)};
}

std::optional<Stat> stat(YamlReader& reader, const YamlNode& node) {
  const std::string name = reader.text(node);
  if (!reader.ok()) {
    return std::nullopt;
  }
  const auto found =
      std::ranges::find_if(AllStats, [&](Stat candidate) { return statName(candidate) == name; });
  if (found == AllStats.end()) {
    reader.fail(node, formatMessage(messages::UnknownStat, name));
    return std::nullopt;
  }
  return *found;
}

} // namespace dwe::game::partner::reading
