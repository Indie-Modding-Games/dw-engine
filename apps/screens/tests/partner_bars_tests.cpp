#include <dwe/screens/partner_bars.hpp>

#include <doctest/doctest.h>

#include <utility>
#include <vector>

namespace {

using dwe::screens::PartnerBarsMotion;

std::pair<int, int> edges(const PartnerBarsMotion& bars) {
  return {bars.hpX(), bars.mpX()};
}

} // namespace

TEST_CASE("the partner's bars come in from the right edge, the MP bar two frames behind") {
  PartnerBarsMotion bars;
  bars.reset();
  CHECK(edges(bars) == std::pair{160, 160});
  // battle_hud.c: BTL_tickPartnerStatusBars with BTL_STATUS_BAR_X.
  const std::vector<std::pair<int, int>> expected{
      {129, 160}, {104, 160}, {83, 129}, {66, 104}, {55, 83},
      {48, 66},   {46, 55},   {46, 48},  {46, 46},  {46, 46},
  };
  for (const auto& edge : expected) {
    bars.step(true);
    CHECK(edges(bars) == edge);
  }
}

TEST_CASE("once the strip goes up the bars leave, the MP bar first, gone on the 9th frame") {
  PartnerBarsMotion bars;
  bars.reset();
  for (int frame = 0; frame < 20; ++frame) {
    bars.step(true);
  }
  const std::vector<std::pair<int, int>> expected{
      {46, 48},  {46, 55},   {48, 66},   {55, 83},   {66, 104},
      {83, 129}, {104, 160}, {129, 160}, {160, 160}, {160, 160},
  };
  for (const auto& edge : expected) {
    bars.step(false);
    CHECK(edges(bars) == edge);
  }
  // A new battle starts them off the screen again.
  bars.step(true);
  bars.reset();
  CHECK(edges(bars) == std::pair{160, 160});
}
