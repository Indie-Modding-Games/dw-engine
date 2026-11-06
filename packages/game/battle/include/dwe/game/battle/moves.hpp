#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/rules.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class MoveTable;
} // namespace dwe::assets

namespace dwe::game::battle {

// Where a move reaches (MOVE_DATA range).
enum class MoveRange : std::uint8_t { None = 0, Melee = 1, Ranged = 2, Area = 3, Self = 4 };

// What a move can leave on whoever it hits (MOVE_DATA status).
enum class StatusEffect : std::uint8_t {
  None = 0,
  Poison = 1,
  Confusion = 2,
  Stun = 3,
  Flatten = 4
};

// The bits of MOVE_DATA's flags byte (move.h: unk3).
inline constexpr std::int32_t NeedsTargetFlag = 1;
inline constexpr std::int32_t SharedCooldownFlag = 2;

// The percentages a support move raises (battle_main.c: BTL_applyBuffMove).
struct BuffPercents {
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
};

// A move as the battle uses it: the table's numbers with the battle section's reading of them.
struct BattleMove {
  std::string id;
  std::int32_t index = 0;
  std::int32_t power = 0;
  // The MP it costs, before the partner's discounts.
  std::int32_t mpCost = 0;
  // The squared distance a ranged move is used from.
  std::int64_t distanceSquared = 0;
  MoveRange range = MoveRange::None;
  std::int32_t special = 0;
  StatusEffect status = StatusEffect::None;
  std::int32_t accuracy = 0;
  std::int32_t statusChance = 0;
  std::int32_t iframes = 0;
  bool needsTarget = false;
  bool sharedCooldown = false;
  bool finisher = false;
  bool counter = false;
  bool learnable = false;
  std::optional<std::array<std::int32_t, 3>> learnChances;
  std::optional<BuffPercents> buff;
};

// Which move of the catalog: an index into it.
using MoveRef = std::size_t;

// The moves table read for battle.
class MoveCatalog {
public:
  // Builds the catalog; every move the battle section names must be in the table.
  static core::Result<MoveCatalog> build(const assets::MoveTable& table, const BattleRules& rules);

  [[nodiscard]] const BattleMove& at(MoveRef move) const { return moves_.at(move); }
  [[nodiscard]] std::optional<MoveRef> find(std::string_view id) const;
  [[nodiscard]] std::size_t size() const { return moves_.size(); }
  // The move a flattened fighter shoots.
  [[nodiscard]] MoveRef flattenedShot() const { return flattenedShot_; }
  // The moves learned along with another one (utils2.c: learnMove).
  [[nodiscard]] std::vector<MoveRef> learnedWith(MoveRef move) const;
  // A copy of a move with its own power and invulnerable frames, for one fighter (a ruleset's
  // species patches): the same id and number, after every move of the table, so find() and the
  // lookups by number keep returning the table's.
  MoveRef addVariant(MoveRef move, std::optional<std::int32_t> power,
                     std::optional<std::int32_t> iframes);

private:
  std::vector<BattleMove> moves_;
  MoveRef flattenedShot_ = 0;
  std::vector<std::pair<MoveRef, MoveRef>> learnedTogether_;
};

} // namespace dwe::game::battle
