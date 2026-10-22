#include <dwe/assets/new_game_stats.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <format>
#include <utility>

#include "document_text.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view NewGameStatsId = "data/new_game_stats";
constexpr std::string_view Context = "new game stats";

StartingStats readStats(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(
          node, "the stats",
          {"offense", "defense", "speed", "brains", "max_hp", "max_mp", "hp", "mp"})) {
    return {};
  }
  const auto value = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.offense = value("offense"),
          .defense = value("defense"),
          .speed = value("speed"),
          .brains = value("brains"),
          .maxHp = value("max_hp"),
          .maxMp = value("max_mp"),
          .hp = value("hp"),
          .mp = value("mp")};
}

} // namespace

const StartingStats& NewGameStatsTable::statsOf(std::string_view digimon) const {
  const auto found = std::ranges::find(partners, digimon, &PartnerStartingStats::digimon);
  return found == partners.end() ? others : found->stats;
}

core::Result<NewGameStatsTable> parseNewGameStats(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  const YamlNode& root = *tree;
  if (!reader.expectKeys(root, "the new game stats", {"script", "at", "partners", "others"})) {
    return std::unexpected{reader.error(Context)};
  }
  NewGameStatsTable table{.script = reader.integer<std::int32_t>(reader.field(root, "script")),
                          .at = reader.integer<std::int32_t>(reader.field(root, "at")),
                          .partners = {},
                          .others = readStats(reader, reader.field(root, "others"))};
  for (const YamlNode& partner : reader.sequence(reader.field(root, "partners"), "partners")) {
    if (reader.expectKeys(partner, "a partner", {"digimon", "stats"})) {
      table.partners.push_back({.digimon = reader.text(reader.field(partner, "digimon")),
                                .stats = readStats(reader, reader.field(partner, "stats"))});
    }
  }
  return reader.finish(std::move(table), Context);
}

core::Result<std::optional<NewGameStatsTable>> loadNewGameStats(const VirtualFileSystem& vfs) {
  auto id = AssetId::parse(NewGameStatsId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  if (vfs.resolve(*id) == nullptr) {
    return std::optional<NewGameStatsTable>{};
  }
  auto text = readDocumentText(vfs, NewGameStatsId);
  if (!text) {
    return std::unexpected{text.error()};
  }
  auto table = parseNewGameStats(*text);
  if (!table) {
    return std::unexpected{table.error()};
  }
  return std::optional{std::move(*table)};
}

} // namespace dwe::assets
