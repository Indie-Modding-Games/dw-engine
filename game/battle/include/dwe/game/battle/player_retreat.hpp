#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/ground.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/game/battle/state.hpp>

#include <cstddef>
#include <cstdint>
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

} // namespace dwe::game::battle
