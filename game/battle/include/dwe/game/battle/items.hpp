#pragma once

#include <cstdint>
#include <optional>

// What an item the tamer throws does to the partner when it lands (item.c: ITEM_FUNCTIONS, called
// by main.c: tickThrownItem). The battle declares it; the session builds it from the inventory's
// item effect.
namespace dwe::game::battle {

// Battle stats raised by a disk (main.c: handleBuffDisks): each up to limitPercent of its value
// when the battle started and never past limitCap; with ignoresLimit only limitCap holds.
struct StatBoost {
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t limitPercent = 0;
  std::int32_t limitCap = 0;
  bool ignoresLimit = false;
};

struct ThrownItem {
  // HP and MP healed, each up to its maximum.
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  // A revive (item.c: handleRestore): this share of the maximum HP, which also gets a fallen
  // partner up.
  std::optional<std::int32_t> reviveHpPercent;
  // Cures the partner's status effects (battle_main.c: BTL_healStatusEffect).
  bool curesStatus = false;
  // Keeps new status effects off the partner (FIGHTER_FLAG_PROTECTED).
  bool protects = false;
  std::optional<StatBoost> boost;
  // Whether it does nothing while the partner is down (the handlers check currentHP != 0).
  bool consciousOnly = false;
};

} // namespace dwe::game::battle
