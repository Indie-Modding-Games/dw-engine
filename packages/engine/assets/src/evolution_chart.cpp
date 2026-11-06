#include <dwe/assets/evolution_chart.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <set>
#include <utility>

namespace dwe::assets {
core::Result<ChartDetailLayout> parseChartDetailLayout(std::string_view text) {
  auto root = core::parseYaml(text);
  if (!root)
    return std::unexpected{root.error()};
  YamlReader reader;
  ChartDetailLayout detail;
  if (!reader.expectKeys(*root, "Chart detail", {"lines", "colors", "sprites", "insets"}))
    return reader.finish(std::move(detail), "Chart detail");
  const auto readMatrix = [&](std::string_view key, auto& matrix, int minimum, int maximum) {
    const auto rows = reader.sequence(reader.field(*root, key), key, matrix.size());
    for (std::size_t i = 0; i < rows.size() && i < matrix.size(); ++i) {
      const auto values = reader.sequence(rows[i], key, matrix[i].size());
      for (std::size_t j = 0; j < values.size() && j < matrix[i].size(); ++j) {
        const auto value = reader.integer<std::int32_t>(values[j]);
        if (value < minimum || value > maximum)
          reader.fail(values[j], "coordinate or color out of range");
        matrix[i][j] = value;
      }
    }
  };
  readMatrix("lines", detail.lines, -320, 320);
  readMatrix("colors", detail.colors, 0, 255);
  readMatrix("sprites", detail.sprites, -320, 320);
  readMatrix("insets", detail.insets, 0, 320);
  for (const auto& inset : detail.insets)
    if (inset[2] <= 0 || inset[3] <= 0 || inset[0] + inset[2] > 320 || inset[1] + inset[3] > 240)
      return core::fail("Chart detail inset is outside its canvas");
  return reader.finish(std::move(detail), "Chart detail");
}
core::Result<ChartDetailLayout> loadChartDetailLayout(const VirtualFileSystem& files,
                                                      const AssetId& id) {
  auto bytes = files.read(id);
  if (!bytes)
    return std::unexpected{bytes.error()};
  return parseChartDetailLayout(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

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
