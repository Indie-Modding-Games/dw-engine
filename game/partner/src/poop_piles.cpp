#include <dwe/game/partner/poop_piles.hpp>

#include <algorithm>
#include <utility>

namespace dwe::game::partner {

namespace {

// The facing ranges of createPoopPile, in 4096ths of a turn: each moves the pile one tile; the
// ranges overlap so the diagonals move it two ways.
constexpr std::int32_t NorthUpTo = 0x300;
constexpr std::int32_t NorthFrom = 0xD00;
constexpr std::int32_t EastFrom = 0x100;
constexpr std::int32_t EastBefore = 0x700;
constexpr std::int32_t SouthFrom = 0x500;
constexpr std::int32_t SouthUpTo = 0xB00;
constexpr std::int32_t WestFrom = 0x900;
constexpr std::int32_t WestUpTo = 0xF00;
constexpr std::int32_t Directions = 4;

Tile inFront(Tile tile, std::int32_t rotation) {
  const std::int32_t angle =
      ((rotation % PoopPiles::TurnUnits) + PoopPiles::TurnUnits) % PoopPiles::TurnUnits;
  if (angle <= NorthUpTo || angle >= NorthFrom) {
    --tile.y;
  }
  if (angle >= EastFrom && angle < EastBefore) {
    ++tile.x;
  }
  if (angle >= SouthFrom && angle <= SouthUpTo) {
    ++tile.y;
  }
  if (angle >= WestFrom && angle <= WestUpTo) {
    --tile.x;
  }
  return tile;
}

Tile step(Tile tile, std::int32_t direction) {
  switch (direction) {
  case 0:
    --tile.x;
    break;
  case 1:
    ++tile.x;
    break;
  case 2:
    --tile.y;
    break;
  default:
    ++tile.y;
    break;
  }
  return tile;
}

} // namespace

PoopPiles::PoopPiles(const ToiletRules& rules)
    : perMap_(static_cast<std::size_t>(std::max(rules.pilesPerMap, 1))),
      slots_(static_cast<std::size_t>(std::max(rules.pilesTotal, 1))) {}

std::optional<std::size_t> PoopPiles::find(std::string_view map, Tile tile) const {
  const auto found = std::ranges::find_if(slots_, [&](const PoopPile& pile) {
    return pile.size != 0 && pile.map == map && pile.tile == tile;
  });
  if (found == slots_.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - slots_.begin());
}

void PoopPiles::fill(std::size_t slot, std::string_view map, Tile tile, std::int32_t size) {
  slots_[slot] = {.map = std::string{map}, .tile = tile, .size = size};
}

Tile PoopPiles::place(std::string_view map, Tile tile, std::int32_t rotation, std::int32_t size,
                      ruleset::Random& random) {
  tile = inFront(tile, rotation);
  while (find(map, tile)) {
    tile = step(tile, random.below(Directions));
  }
  std::size_t onThisMap = 0;
  for (std::size_t slot = 0; slot < slots_.size(); ++slot) {
    if (onThisMap >= perMap_) {
      reuseOnMap(map, tile, size);
      return tile;
    }
    if (slots_[slot].size == 0) {
      fill(slot, map, tile, size);
      return tile;
    }
    onThisMap += slots_[slot].map == map ? 1 : 0;
  }
  if (onThisMap >= perMap_) {
    reuseOnMap(map, tile, size);
    return tile;
  }
  // The world is full: the slot after the last one reused.
  fill(cursor_, map, tile, size);
  cursor_ = (cursor_ + 1) % slots_.size();
  return tile;
}

void PoopPiles::reuseOnMap(std::string_view map, Tile tile, std::int32_t size) {
  // The map is full: its next pile from the cursor on is replaced.
  for (std::size_t checked = 0; checked < slots_.size(); ++checked) {
    const std::size_t slot = (cursor_ + checked) % slots_.size();
    if (slots_[slot].map == map) {
      fill(slot, map, tile, size);
      cursor_ = (slot + 1) % slots_.size();
      return;
    }
  }
}

std::optional<PoopPile> PoopPiles::at(std::string_view map, Tile tile) const {
  const std::optional<std::size_t> slot = find(map, tile);
  if (!slot) {
    return std::nullopt;
  }
  return slots_[*slot];
}

std::optional<std::int32_t> PoopPiles::remove(std::string_view map, Tile tile) {
  const std::optional<std::size_t> slot = find(map, tile);
  if (!slot) {
    return std::nullopt;
  }
  const std::int32_t size = slots_[*slot].size;
  slots_[*slot] = {};
  return size;
}

std::vector<PoopPile> PoopPiles::onMap(std::string_view map) const {
  std::vector<PoopPile> piles;
  for (const PoopPile& pile : slots_) {
    if (pile.size != 0 && pile.map == map) {
      piles.push_back(pile);
    }
  }
  return piles;
}

void PoopPiles::restore(std::vector<PoopPile> slots, std::size_t cursor) {
  const std::size_t count = slots_.size();
  slots_ = std::move(slots);
  slots_.resize(count);
  cursor_ = cursor % count;
}

} // namespace dwe::game::partner
