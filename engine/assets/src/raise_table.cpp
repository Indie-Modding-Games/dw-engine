#include <dwe/assets/messages.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <span>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view TableKey = "raise";
constexpr std::string_view DigimonKey = "digimon";
constexpr std::string_view HungerTimesKey = "hunger_times";
constexpr std::string_view EnergyCapKey = "energy_cap";
constexpr std::string_view EnergyThresholdKey = "energy_threshold";
constexpr std::string_view EnergyUsageKey = "energy_usage";
constexpr std::string_view PoopTimerKey = "poop_timer";
constexpr std::string_view Unknown0eKey = "unknown_0e";
constexpr std::string_view PoopSizeKey = "poop_size";
constexpr std::string_view FavoriteFoodKey = "favorite_food";
constexpr std::string_view SleepCycleKey = "sleep_cycle";
constexpr std::string_view FavoredRegionKey = "favored_region";
constexpr std::string_view TrainingTypeKey = "training_type";
constexpr std::string_view DefaultWeightKey = "default_weight";
constexpr std::string_view ViewKey = "view";
constexpr std::string_view XKey = "x";
constexpr std::string_view YKey = "y";
constexpr std::string_view ZKey = "z";

constexpr std::size_t HungerTimeCount = 8;

std::array<std::int32_t, 3> readView(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::ViewText, {XKey, YKey, ZKey})) {
    return {};
  }
  return {reader.integer<std::int32_t>(reader.field(node, XKey)),
          reader.integer<std::int32_t>(reader.field(node, YKey)),
          reader.integer<std::int32_t>(reader.field(node, ZKey))};
}

std::array<std::int32_t, HungerTimeCount> readHungerTimes(YamlReader& reader,
                                                          const YamlNode& node) {
  std::array<std::int32_t, HungerTimeCount> hours{};
  const std::span<const YamlNode> items =
      reader.sequence(node, messages::HungerTimesText, HungerTimeCount);
  for (std::size_t i = 0; i < items.size(); ++i) {
    hours.at(i) = reader.integer<std::int32_t>(items[i]);
  }
  return hours;
}

RaiseRow readRow(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::RaiseRowText,
                         {DigimonKey, HungerTimesKey, EnergyCapKey, EnergyThresholdKey,
                          EnergyUsageKey, PoopTimerKey, Unknown0eKey, PoopSizeKey, FavoriteFoodKey,
                          SleepCycleKey, FavoredRegionKey, TrainingTypeKey, DefaultWeightKey,
                          ViewKey},
                         {EntryIndexKey})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.digimon = reader.optionalText(reader.field(node, DigimonKey)),
          .index = reader.optionalInteger<std::int32_t>(node, EntryIndexKey),
          .hungerTimes = readHungerTimes(reader, reader.field(node, HungerTimesKey)),
          .energyCap = integer(EnergyCapKey),
          .energyThreshold = integer(EnergyThresholdKey),
          .energyUsage = integer(EnergyUsageKey),
          .poopTimer = integer(PoopTimerKey),
          .unknown0e = integer(Unknown0eKey),
          .poopSize = integer(PoopSizeKey),
          .favoriteFood = reader.optionalText(reader.field(node, FavoriteFoodKey)),
          .sleepCycle = integer(SleepCycleKey),
          .favoredRegion = integer(FavoredRegionKey),
          .trainingType = integer(TrainingTypeKey),
          .defaultWeight = integer(DefaultWeightKey),
          .view = readView(reader, reader.field(node, ViewKey))};
}

} // namespace

const RaiseRow* RaiseTable::find(std::string_view digimon) const {
  const auto found =
      std::ranges::find_if(raise, [&](const RaiseRow& row) { return row.digimon == digimon; });
  return found == raise.end() ? nullptr : &*found;
}

core::Result<RaiseTable> parseRaiseTable(std::string_view yaml) {
  auto root = core::parseYaml(yaml);
  if (!root) {
    return std::unexpected{root.error()};
  }
  YamlReader reader;
  RaiseTable table;
  if (reader.expectKeys(*root, messages::RaiseTableText, {TableKey})) {
    for (const YamlNode& row : reader.sequence(reader.field(*root, TableKey), TableKey)) {
      table.raise.push_back(readRow(reader, row));
    }
  }
  return reader.finish(std::move(table), messages::RaiseTableText);
}

core::Result<RaiseTable> loadRaiseTable(const VirtualFileSystem& vfs, const AssetId& id) {
  auto text = readComposedTable(vfs, id, RaiseComposition);
  if (!text) {
    return std::unexpected{text.error()};
  }
  return parseRaiseTable(*text);
}

} // namespace dwe::assets
