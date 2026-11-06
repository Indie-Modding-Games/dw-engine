#include <dwe/game/partner/area_response.hpp>

#include <cstddef>
#include <limits>

namespace dwe::game::partner {
namespace {

// The low byte of a value, 0 to 255, as the original keeps regions in bytes.
std::int32_t lowByte(std::int32_t value) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) &
                                   std::numeric_limits<std::uint8_t>::max());
}

// map.c: loadMapSetup compares an int8_t region with the uint8_t favored region: a region byte over
// 127 is negative there, and never equals a favored region.
bool sameRegion(std::int32_t region, std::int32_t favoredRegion) {
  const std::int32_t regionByte = lowByte(region);
  return regionByte <= std::numeric_limits<std::int8_t>::max() &&
         regionByte == lowByte(favoredRegion);
}

} // namespace

AreaResponse areaResponseOf(const assets::MapRegions& regions, std::int32_t favoredRegion) {
  for (std::size_t slot = 0; slot < regions.a.size(); ++slot) {
    if (sameRegion(regions.a[slot], favoredRegion)) {
      return AreaResponse::Liked;
    }
    if (sameRegion(regions.b[slot], favoredRegion)) {
      return AreaResponse::Disliked;
    }
  }
  return AreaResponse::Neutral;
}

} // namespace dwe::game::partner
