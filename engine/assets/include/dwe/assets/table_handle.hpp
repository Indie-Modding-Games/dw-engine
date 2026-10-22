#pragma once

#include <cstdint>

// The handle of an entry of a composed table: what the running game holds instead of the entry's id
// (README.md#ids-en-la-sesión). It is the entry's place in one composition of the packs' tables, so
// it only means something to that table: only the table makes one, and it refuses one of another
// composition. A handle never leaves the session: saves, rulesets, data files, scripts, text keys
// and logs name the entry by its id.
namespace dwe::assets {

// A composition of the packs' tables. Each table read from the packs gets a new one; copies of the
// table share it. The session composes its tables once, when it starts: mounting other packs starts
// a new session from the ids.
class CompositionGeneration {
public:
  // A generation no table has had before.
  static CompositionGeneration next();

  friend bool operator==(CompositionGeneration, CompositionGeneration) = default;

private:
  explicit constexpr CompositionGeneration(std::uint32_t value) : value_{value} {}

  std::uint32_t value_ = 0;
};

// The handle of an entry of `Table`: distinct for each table, so an item's handle is no move's.
template <typename Table>
class TableHandle {
public:
  friend bool operator==(const TableHandle&, const TableHandle&) = default;

private:
  friend Table;

  constexpr TableHandle(CompositionGeneration generation, std::uint32_t position)
      : generation_{generation}, position_{position} {}

  CompositionGeneration generation_;
  std::uint32_t position_ = 0;
};

} // namespace dwe::assets
