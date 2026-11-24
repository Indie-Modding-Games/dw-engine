#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/battle/boosted_moves.hpp>
#include <dwe/game/battle/messages.hpp>

#include <algorithm>
#include <utility>

namespace dwe::game::battle {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::int32_t Hundred = 100;
constexpr std::string_view PowerPercentKey = "power_percent";
constexpr std::string_view MpCostPercentKey = "mp_cost_percent";
constexpr std::string_view SpeciesKey = "species";
constexpr std::string_view SectionText = "the boosted_moves section";

std::int32_t percentOf(YamlReader& reader, const YamlNode& root, std::string_view key) {
  const YamlNode& value = reader.field(root, key);
  const auto percent = reader.integer<std::int32_t>(value);
  if (reader.ok() && percent <= 0) {
    reader.fail(value, messages::formatMessage(messages::NotPositive, key));
  }
  return percent;
}

} // namespace

core::Result<BoostedMoveRules> readBoostedMoveRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, SectionText, {PowerPercentKey, MpCostPercentKey, SpeciesKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  BoostedMoveRules rules{.powerPercent = percentOf(reader, root, PowerPercentKey),
                         .mpCostPercent = percentOf(reader, root, MpCostPercentKey),
                         .species = {}};
  const YamlNode& species = reader.field(root, SpeciesKey);
  if (reader.ok() && !species.isMapping()) {
    reader.fail(species, "species must map each species id to a move id");
  }
  if (reader.ok()) {
    for (const core::YamlEntry& entry : species.entries()) {
      rules.species.emplace(entry.key, reader.text(entry.value));
    }
  }
  return reader.finish(std::move(rules), section.file);
}

std::int32_t boostedPower(const BoostedMoveRules& rules, std::int32_t power) {
  return power * rules.powerPercent / Hundred;
}

std::int32_t boostedMpCost(const BoostedMoveRules& rules, std::int32_t cost) {
  if (cost <= 0) {
    return cost;
  }
  return std::max(1, ((cost * rules.mpCostPercent) + Hundred - 1) / Hundred);
}

} // namespace dwe::game::battle
