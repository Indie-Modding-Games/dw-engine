#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The Folder battle system's own state (docs/architecture/folder-battle.md, combat-design.md): the
// trainer's orders, how each fighter executes them, its stance and its defense bar.
namespace dwe::game::battle {

using FighterId = std::size_t;

// How a fighter positions itself between and during orders (combat-design.md §2.9).
enum class Stance : std::uint8_t { Aggressive, Hold, Distance, Guard };
inline constexpr std::size_t StanceCount = 4;

// combat-design.md §2.8: at most two orders wait.
inline constexpr std::size_t OrderQueueSize = 2;

// A technique ordered against a target (none: the move needs none or aims at the fighter itself).
struct Order {
  std::size_t slot = 0;
  std::optional<FighterId> target;
  // Numbered from 1 in the order the battle received them, for the HUD to follow one.
  std::uint32_t id = 0;

  friend bool operator==(const Order&, const Order&) = default;
};

// combat-design.md §4.3: where the active order is.
enum class OrderPhase : std::uint8_t {
  Idle,
  Processing,
  Positioning,
  Preparing,
  Executing,
  Recovering,
};

// How an order left the fighter (§4.5, §4.6).
enum class OrderEnd : std::uint8_t { Done, Interrupted, Invalid, Cancelled, Replaced };

// Why an order could not go on.
enum class OrderProblem : std::uint8_t {
  None,
  EmptySlot,
  NotEnoughMp,
  FinisherNotReady,
  NoTarget,
  TargetGone,
  Unreachable,
  ByTrainer,
};

struct FolderFighter {
  Stance stance = Stance::Aggressive;
  std::vector<Order> queue;
  std::optional<Order> active;
  OrderPhase phase = OrderPhase::Idle;
  // The frames left of a timed phase (processing, preparing, recovering), or spent positioning.
  std::int32_t phaseFrames = 0;
  // The frames of the timed phase in full, for the HUD's progress.
  std::int32_t phaseLength = 0;
  // §2.9: the damage taken lately; it breaks the defense at its capacity.
  std::int32_t defenseBar = 0;
  // Frames since the last hit, before the bar drains.
  std::int32_t restFrames = 0;
  // While above 0 the defense is broken.
  std::int32_t brokenFrames = 0;
  std::int32_t brokenLength = 0;
  // The last order that ended, how and why, for the HUD.
  std::optional<Order> lastOrder;
  OrderEnd lastEnd = OrderEnd::Done;
  OrderProblem lastProblem = OrderProblem::None;
  // A status effect (stun, confusion, flattening) holds it: the original's behaviour for the
  // status runs meanwhile.
  bool held = false;

  [[nodiscard]] bool broken() const { return brokenFrames > 0; }
  // The stance it moves by now: the break keeps it holding (§2.9).
  [[nodiscard]] Stance effectiveStance() const { return broken() ? Stance::Hold : stance; }
};

struct FolderState {
  // One per fighter, in the fighters' order.
  std::vector<FolderFighter> fighters;
  std::uint32_t nextOrder = 1;
};

} // namespace dwe::game::battle
