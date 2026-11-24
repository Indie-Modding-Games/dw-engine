#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/randomizer/randomizer.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::randomizer {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SectionText = "the randomizer section";
constexpr std::string_view EnemiesKey = "enemies";
constexpr std::string_view KeptItemsKey = "kept_items";
constexpr std::string_view ExcludedItemsKey = "excluded_items";
constexpr std::string_view ExcludedSpeciesKey = "excluded_species";
constexpr std::string_view NotMapping = "{} must map map ids to lists of NPC slots";

std::vector<std::string> sortedIds(YamlReader& reader, const YamlNode& root, std::string_view key) {
  std::vector<std::string> ids;
  for (const YamlNode& item : reader.sequence(reader.field(root, key), key)) {
    ids.push_back(reader.text(item));
  }
  std::ranges::sort(ids);
  return ids;
}

} // namespace

core::Result<RandomizerRules> readRandomizerRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, SectionText,
                         {EnemiesKey, KeptItemsKey, ExcludedItemsKey, ExcludedSpeciesKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  RandomizerRules rules{.enemies = {},
                        .keptItems = sortedIds(reader, root, KeptItemsKey),
                        .excludedItems = sortedIds(reader, root, ExcludedItemsKey),
                        .excludedSpecies = sortedIds(reader, root, ExcludedSpeciesKey)};
  const YamlNode& maps = reader.field(root, EnemiesKey);
  if (!maps.isMapping()) {
    reader.fail(maps, core::formatMessage(NotMapping, EnemiesKey));
  }
  for (const core::YamlEntry& entry :
       maps.isMapping() ? maps.entries() : std::span<const core::YamlEntry>{}) {
    std::vector<std::size_t>& slots = rules.enemies[entry.key];
    for (const YamlNode& slot : reader.sequence(entry.value, EnemiesKey)) {
      slots.push_back(reader.integer<std::size_t>(slot));
    }
    std::ranges::sort(slots);
  }
  return reader.finish(rules, section.file);
}

ruleset::SectionChecks randomizerSectionChecks() {
  return {
      {std::string{RandomizerSection}, [](const ruleset::Section& section) -> core::Result<void> {
         if (auto read = readRandomizerRules(section); !read) {
           return std::unexpected{read.error()};
         }
         return {};
       }}};
}

} // namespace dwe::game::randomizer
