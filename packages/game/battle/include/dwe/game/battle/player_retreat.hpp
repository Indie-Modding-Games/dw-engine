#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/ground.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/game/battle/state.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::game::battle {

// The player's run back as a battle opens (battle_setup.c: BTL_handleBattleIntro): while the
// banner gathers it runs along its way back (the cells the field kept, newest first), then toward
// the cell it entered the map on, and stops on that cell or where its next step would leave the
// combat area, turning to the partner. The player is no fighter; the screen moves it.
class PlayerRetreat {
public:
  PlayerRetreat(std::vector<assets::CollisionCell> way, assets::CollisionCell entry, Point from,
                Facing facing, const TimingRules& timing);

  // One frame of the opening, `partner` where the partner stands. True on the frame it stops.
  bool step(const BattleGround* ground, Point partner);
  // The opening ends with the player still running: it stops and turns to the partner.
  void stop(Point partner);

  [[nodiscard]] bool running() const { return running_; }
  [[nodiscard]] Point position() const { return position_; }
  [[nodiscard]] Facing facing() const { return facing_; }
  // How many cells of the way back it has not reached (TAMER_WAYPOINT_COUNT afterwards).
  [[nodiscard]] std::size_t wayLeft() const { return wayLeft_; }

private:
  void lookAt(Point point);

  std::vector<assets::CollisionCell> way_;
  assets::CollisionCell entry_;
  Point position_;
  Facing facing_;
  // The run's fraction of a unit (the fighters' rootRemainder): its animation starts with none.
  Point remainder_;
  const TimingRules* timing_ = nullptr;
  std::size_t wayLeft_ = 0;
  bool running_ = true;
};

// The player's run when it calls the partner back (battle_tick.c: handleCommands, command 1, and
// handleFleeing): it runs (animation 3) along what is left of its way back, newest cell first, then
// toward the cell it entered the map on, and from that cell toward `away`, the way out of the map
// it came in by, running on the spot there. Nothing else stops it: the screen fades out while it
// runs and the battle ends.
class PlayerFlight {
public:
  PlayerFlight(std::vector<assets::CollisionCell> way, assets::CollisionCell entry,
               std::optional<Point> away, Point from, Facing facing, const TimingRules& timing);

  // One frame of the run: handleFleeing's turn, then the run's step (its root motion).
  void step();

  [[nodiscard]] Point position() const { return position_; }
  [[nodiscard]] Facing facing() const { return facing_; }
  // How many cells of the way back it has not reached (TAMER_WAYPOINT_COUNT).
  [[nodiscard]] std::size_t wayLeft() const { return wayLeft_; }
  // Whether it runs on the spot on the entry cell, facing the way out.
  [[nodiscard]] bool held() const { return held_; }

private:
  void turn();

  std::vector<assets::CollisionCell> way_;
  assets::CollisionCell entry_;
  std::optional<Point> away_;
  Point position_;
  Facing facing_;
  Point remainder_;
  const TimingRules* timing_ = nullptr;
  std::size_t wayLeft_ = 0;
  bool held_ = false;
};

// handleFleeing's way out: `distance` past `spawn`, the point the map's exit puts the player on,
// against `spawnFacing`, the way the player faces as it arrives there (MAP_WARPS.spawnX/Z and
// rotation + 0x800 turning (0, 0, -3000)).
[[nodiscard]] Point wayOut(Point spawn, Facing spawnFacing, std::int32_t distance);

} // namespace dwe::game::battle
