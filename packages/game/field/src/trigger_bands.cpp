#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/field/trigger_bands.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

#include "messages.hpp"

namespace dwe::game::field {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view TriggersId = "field/triggers";

TriggerBand readBand(YamlReader& reader, const YamlNode& node, std::string_view what) {
  if (!reader.expectKeys(node, what, {"first", "last"})) {
    return {};
  }
  const TriggerBand band{.first = reader.integer<std::int32_t>(reader.field(node, "first")),
                         .last = reader.integer<std::int32_t>(reader.field(node, "last"))};
  if (reader.ok() && band.first > band.last) {
    reader.fail(node, core::formatMessage(messages::TriggerBandBackwards, what));
  }
  return band;
}

// Every band is a cell value of a trigger: above 0 (free) and below the blocked bit; no two share a
// value; the exits fit a map's warps.
void checkBands(YamlReader& reader, const YamlNode& root, const TriggerBands& bands) {
  if (std::cmp_not_equal(bands.blockedBit, assets::MapCollision::BlockedBit)) {
    reader.fail(root, core::formatMessage(messages::TriggerBlockedBitFixed,
                                          assets::MapCollision::BlockedBit));
    return;
  }
  const std::array named{
      std::pair{std::string_view{"step"}, bands.step},
      std::pair{std::string_view{"confirm"}, bands.confirm},
      std::pair{std::string_view{"exits"}, bands.exits},
      std::pair{std::string_view{"toilet"},
                TriggerBand{.first = bands.toilet, .last = bands.toilet}},
  };
  for (const auto& [name, band] : named) {
    if (band.first < 1 || band.last >= bands.blockedBit) {
      reader.fail(root,
                  core::formatMessage(messages::TriggerBandOutside, name, bands.blockedBit - 1));
      return;
    }
  }
  for (std::size_t one = 0; one < named.size(); ++one) {
    for (std::size_t other = one + 1; other < named.size(); ++other) {
      const TriggerBand& a = named.at(one).second;
      const TriggerBand& b = named.at(other).second;
      if (a.first <= b.last && b.first <= a.last) {
        reader.fail(root, core::formatMessage(messages::TriggerBandsOverlap, named.at(one).first,
                                              named.at(other).first));
        return;
      }
    }
  }
  if (bands.exits.last - bands.exits.first + 1 > MostExits) {
    reader.fail(root, core::formatMessage(messages::TriggerTooManyExits, MostExits));
  }
}

} // namespace

std::optional<std::int32_t> TriggerBands::exitOf(std::int32_t value) const {
  if (!exits.contains(value)) {
    return std::nullopt;
  }
  return value - exits.first;
}

core::Result<TriggerBands> parseTriggerBands(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("field triggers: {}", tree.error().message)}};
  }
  YamlReader reader;
  if (!reader.expectKeys(*tree, "the field triggers",
                         {"step", "confirm", "exits", "toilet", "blocked_bit"})) {
    return std::unexpected{reader.error("field triggers")};
  }
  TriggerBands bands{.step = readBand(reader, reader.field(*tree, "step"), "step"),
                     .confirm = readBand(reader, reader.field(*tree, "confirm"), "confirm"),
                     .exits = readBand(reader, reader.field(*tree, "exits"), "exits"),
                     .toilet = reader.integer<std::int32_t>(reader.field(*tree, "toilet")),
                     .blockedBit =
                         reader.integer<std::int32_t>(reader.field(*tree, "blocked_bit"))};
  if (reader.ok()) {
    checkBands(reader, *tree, bands);
  }
  return reader.finish(bands, "field triggers");
}

core::Result<TriggerBands> loadTriggerBands(const assets::VirtualFileSystem& vfs) {
  const auto id = assets::AssetId::parse(TriggersId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  if (vfs.resolve(*id) == nullptr) {
    return TriggerBands{};
  }
  const auto bytes = vfs.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return parseTriggerBands(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

} // namespace dwe::game::field
