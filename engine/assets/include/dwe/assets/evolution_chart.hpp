#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
struct ChartCell {
  std::string digimon;
  std::int32_t index = 0;
  std::int32_t x = 0, y = 0, u = 0, v = 0, palette = 0;
};
struct EvolutionChart {
  std::string image;
  std::vector<ChartCell> entries;
};
struct ChartDetailLayout {
  std::array<std::array<std::int32_t, 8>, 20> lines{};
  std::array<std::array<std::int32_t, 3>, 12> colors{};
  std::array<std::array<std::int32_t, 2>, 20> sprites{};
  std::array<std::array<std::int32_t, 4>, 6> insets{};
};
core::Result<ChartDetailLayout> parseChartDetailLayout(std::string_view text);
core::Result<ChartDetailLayout> loadChartDetailLayout(const VirtualFileSystem& files,
                                                      const AssetId& id);
core::Result<EvolutionChart> parseEvolutionChart(std::string_view text);
core::Result<EvolutionChart> loadEvolutionChart(const VirtualFileSystem& files, const AssetId& id);
} // namespace dwe::assets
