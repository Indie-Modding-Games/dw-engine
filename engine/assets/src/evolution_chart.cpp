#include <dwe/assets/evolution_chart.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <set>
#include <utility>

namespace dwe::assets {
core::Result<EvolutionChart> parseEvolutionChart(std::string_view text) {
  auto root = core::parseYaml(text);
  if (!root)
    return std::unexpected{root.error()};
  YamlReader reader;
  EvolutionChart chart;
  if (!reader.expectKeys(*root, "evolution chart", {"image", "entries"}))
    return reader.finish(std::move(chart), "evolution chart");
  chart.image = reader.text(reader.field(*root, "image"));
  if (auto id = AssetId::parse(chart.image); !id)
    return std::unexpected{id.error()};
  std::set<std::int32_t> indices;
  std::set<std::pair<int, int>> positions;
  const auto rows = reader.sequence(reader.field(*root, "entries"), "chart cells");
  if (rows.empty() || rows.size() > 61)
    return core::fail("Chart requires 1 to 61 cells");
  for (const auto& row : rows) {
    if (!reader.expectKeys(row, "chart cell", {"digimon", "index", "x", "y", "u", "v", "palette"}))
      continue;
    const auto value = [&](std::string_view key) {
      return reader.integer<std::int32_t>(reader.field(row, key));
    };
    ChartCell cell{.digimon = reader.text(reader.field(row, "digimon")),
                   .index = value("index"),
                   .x = value("x"),
                   .y = value("y"),
                   .u = value("u"),
                   .v = value("v"),
                   .palette = value("palette")};
    if (cell.digimon.empty() || cell.index < 1 || cell.x < 0 || cell.x > 304 || cell.y < 0 ||
        cell.y > 224 || cell.u < 0 || cell.u > 224 || cell.v < 0 || cell.v > 240 ||
        cell.palette < 0 || cell.palette > 15 || !indices.insert(cell.index).second ||
        !positions.emplace(cell.x, cell.y).second)
      return core::fail("invalid or duplicate Chart cell");
    chart.entries.push_back(std::move(cell));
  }
  return reader.finish(std::move(chart), "evolution chart");
}
core::Result<EvolutionChart> loadEvolutionChart(const VirtualFileSystem& files, const AssetId& id) {
  auto bytes = files.read(id);
  if (!bytes)
    return std::unexpected{bytes.error()};
  return parseEvolutionChart(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}
} // namespace dwe::assets
