#pragma once

#include <dwe/game/partner/care_rules.hpp>
#include <dwe/ruleset/random.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::partner {

// A tile of a map.
struct Tile {
  std::int32_t x = 0;
  std::int32_t y = 0;

  friend bool operator==(const Tile&, const Tile&) = default;
};

// A poop pile on a map (partner_impl.c: WORLD_POOP).
struct PoopPile {
  std::string map;
  Tile tile;
  std::int32_t size = 0;

  friend bool operator==(const PoopPile&, const PoopPile&) = default;
};

// The piles the partner left around the world, in slots as the original keeps them: a map holds so
// many, and the world so many in all; past that the oldest are reused. The field draws them and the
// partner eats them.
class PoopPiles {
public:
  // The rotation of the partner in the original's units: a turn is 4096.
  static constexpr std::int32_t TurnUnits = 4096;

  explicit PoopPiles(const ToiletRules& rules);

  // Places a pile in front of the partner, standing on `tile` and facing `rotation`
  // (partner_impl.c: createPoopPile). A tile with a pile already sends it a random step away.
  // Returns where it went.
  Tile place(std::string_view map, Tile tile, std::int32_t rotation, std::int32_t size,
             ruleset::Random& random);

  // The pile on a tile, if any.
  [[nodiscard]] std::optional<PoopPile> at(std::string_view map, Tile tile) const;

  // Takes the pile off a tile (eaten or cleaned); its size, if there was one.
  std::optional<std::int32_t> remove(std::string_view map, Tile tile);

  // The piles of a map.
  [[nodiscard]] std::vector<PoopPile> onMap(std::string_view map) const;

  // Every slot, empty ones with size 0, and the slot reused next, for the save.
  [[nodiscard]] std::span<const PoopPile> slots() const { return slots_; }
  [[nodiscard]] std::size_t cursor() const { return cursor_; }
  void restore(std::vector<PoopPile> slots, std::size_t cursor);

private:
  [[nodiscard]] std::optional<std::size_t> find(std::string_view map, Tile tile) const;
  void fill(std::size_t slot, std::string_view map, Tile tile, std::int32_t size);
  void reuseOnMap(std::string_view map, Tile tile, std::int32_t size);

  std::size_t perMap_;
  std::vector<PoopPile> slots_;
  std::size_t cursor_ = 0;
};

} // namespace dwe::game::partner
