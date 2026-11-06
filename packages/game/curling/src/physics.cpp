#include <dwe/game/curling/curling.hpp>

#include <array>
#include <cstdlib>
#include <utility>

#include "kar_math.hpp"

// The stones on the ice: sliding, friction, walls and knocks (kar.c: KAR_tickStones,
// KAR_updateCollisions, KAR_bounceOffWall and the KAR_collide* family).
namespace dwe::game::curling {

namespace {

using kar::alongX;
using kar::alongZ;
using kar::distance;
using kar::ratan2;

// The stone states (KarStone.state).
constexpr std::int32_t Sliding = 1;
constexpr std::int32_t Pegged = 2;
constexpr std::int32_t Shaking = 3;
constexpr std::int32_t Out = -101;
constexpr std::int32_t PeggedType = 3;
constexpr std::int32_t Rolling = 0x0B;
constexpr std::int32_t RingsState = 0x0C;
constexpr std::int32_t AimSelectState = 5;

// Two stones touch closer than this; they are pushed apart to at least the next unit.
constexpr std::int32_t Touch = 0x96;
constexpr std::int32_t Apart = 0x97;
// KAR_updateCollisions: the frame is cut in ten steps (eleven tests).
constexpr std::int32_t Substeps = 10;
// KAR_resolveStoneCollision: a knock this hard (both speeds) sounds louder.
constexpr std::int32_t HardKnock = 0xBB8;
// KAR_checkStonesStopped: a stone at rest this far up the rink is out.
constexpr std::int32_t OutLine = 0xBE;
// KAR_getWallZone: the rink's walls. The sides at x -724 and 725 down to z -2000; below it the
// corner diagonals (z +- 2x < -3524) and the back wall (z < -2524); the front line at z 1125.
constexpr std::int32_t FrontLine = 0x465;
constexpr std::int32_t SideEnd = -0x7D0;
constexpr std::int32_t LeftWall = -0x2D4;
constexpr std::int32_t RightWall = 0x2D5;
constexpr std::int32_t Diagonal = -0xDC4;
constexpr std::int32_t DiagonalLeftEnd = 0x1AA;
constexpr std::int32_t DiagonalRightStart = 0x1A9;
constexpr std::int32_t BackWall = -0x9DC;
// KAR_bounceOffWall: the mirror lines of the side and back walls.
constexpr std::int32_t SideMirror = 0x2D5;
constexpr std::int32_t BackMirror = -0x9DD;
// KAR_findWallContact: the diagonals' line (z + 3525 +- 2x = 0).
constexpr std::int32_t DiagonalLine = 0xDC5;
constexpr std::int32_t HalfTurn = 0x800;
constexpr std::int32_t AngleMask = 0xFFF;
constexpr std::int32_t QuarterTurn = 0x400;
constexpr std::int32_t ThreeQuarters = 0xC00;
// The sounds of bank 8.
constexpr std::int32_t Bank = 8;
constexpr std::int32_t HardKnockSound = 1;
constexpr std::int32_t SoftKnockSound = 2;
constexpr std::int32_t WallSound = 3;
constexpr std::int32_t PegSound = 4;
// KAR_tickStones: a shaking pegged stone moves a tenth of its move.
constexpr std::int32_t ShakeDivisor = 10;
constexpr std::int32_t SpinStep = 0x14;
// KAR_collideMovingStones: two stones that meet head on with this little left stop.
constexpr std::int32_t StallMove = 5;

std::int32_t shortOf(std::int32_t value) {
  return static_cast<std::int16_t>(value);
}

// KAR_getWallZone: 0 inside, 1 left, 2 right, 3 and 4 the diagonals, 5 the back, 6 past the front.
std::int32_t wallZone(std::int32_t x, std::int32_t z) {
  x = shortOf(x);
  z = shortOf(z);
  if (z >= FrontLine) {
    return 6;
  }
  if (z >= SideEnd) {
    if (x < LeftWall) {
      return 1;
    }
    if (x >= RightWall) {
      return 2;
    }
  } else {
    if (z + (x * 2) < Diagonal && x < DiagonalLeftEnd) {
      return 3;
    }
    if (z - (x * 2) < Diagonal && x >= DiagonalRightStart) {
      return 4;
    }
    if (z < BackWall) {
      return 5;
    }
  }
  return 0;
}

// KAR_findWallContact: back along the stone's way until it is off the diagonal.
Point wallContact(const Stone& stone, bool right) {
  const std::int32_t back = (stone.angle + HalfTurn) & AngleMask;
  Point at;
  std::int32_t length = 0;
  std::int32_t side = 0;
  do {
    at.x = stone.position.x + alongX(length, back);
    at.z = stone.position.z + alongZ(length, back);
    ++length;
    side = right ? (at.z + DiagonalLine) - (at.x * 2) : (at.z + DiagonalLine) + (at.x * 2);
  } while (side <= 0);
  return at;
}

// KAR_reflectOffDiagonal.
void reflectOffDiagonal(Stone& stone, bool right) {
  const std::int32_t reversed = (stone.angle + HalfTurn) & AngleMask;
  const std::int32_t wall = right ? ratan2(1, -2) : ratan2(1, 2);
  stone.angle = (wall * 2) - reversed;
}

// KAR_placeAtContact: the rest of the frame's way from the contact, along the new angle.
void placeAtContact(Stone& stone, Point contact) {
  std::int32_t rest = distance(contact.x - stone.target.x, contact.z - stone.target.z);
  rest = distance(stone.target.z - stone.position.z, stone.target.x - stone.position.x) - rest;
  stone.position.x = contact.x + alongX(rest, stone.angle);
  stone.position.z = contact.z + alongZ(rest, stone.angle);
}

// KAR_computeImpactShare: how much of a stone's speed goes into what it hits at `b`.
std::int32_t impactShare(const Stone& stone, Point a, Point b) {
  const std::int32_t heading = stone.angle & AngleMask;
  const std::int32_t toward = ratan2(b.z - a.z, b.x - a.x) & AngleMask;
  std::int32_t off = (heading - toward) & AngleMask;
  if (off < QuarterTurn || off > ThreeQuarters) {
    if (off > ThreeQuarters) {
      off = (kar::Turn - off) & AngleMask;
    }
    off = QuarterTurn - (off % QuarterTurn);
    return off * stone.speed / QuarterTurn;
  }
  return 0;
}

// KAR_computeSeparation: the push that parts a stone from what it hits at `b`.
Point separation(const Stone& stone, Point a, Point b) {
  const std::int32_t heading = stone.angle & AngleMask;
  const std::int32_t toward = ratan2(b.z - a.z, b.x - a.x) & AngleMask;
  std::int32_t off = std::abs(heading - toward);
  Point push;
  if (off < QuarterTurn || off > ThreeQuarters) {
    off %= QuarterTurn;
    const std::int32_t gap = distance(stone.position.z - a.z, stone.position.x - a.x);
    std::int32_t length = (QuarterTurn - off) * gap / QuarterTurn;
    std::int32_t size = 0;
    do {
      push.x = alongX(length, toward);
      push.z = alongZ(length, toward);
      size = distance(push.x, push.z);
      ++length;
    } while (size == 0);
  }
  return push;
}

} // namespace

void Match::tickStones() {
  for (std::size_t team = 0; team < Teams; ++team) {
    for (std::size_t index = 0; index < StonesPerTeam; ++index) {
      Stone& stone = teams_.at(team).stones.at(index);
      if (stone.state != Shaking && stone.state != Pegged) {
        stone.target = stone.position;
      }
      if (state_ == AimSelectState) {
        if (std::cmp_equal(index, selected_) && std::cmp_equal(team, team_)) {
          stone.spin += SpinStep;
        } else {
          stone.spin = 0;
        }
      }
      if (stone.state <= 0 || stone.speed < 0) {
        continue;
      }
      const std::int32_t move = shortOf(stone.speed / stone.weight);
      stone.speed = shortOf(
          static_cast<std::int32_t>(static_cast<std::uint32_t>(stone.speed * rules_->frictionKeep) /
                                    static_cast<std::uint32_t>(rules_->frictionOf)));
      if (move < rules_->minMove) {
        stone.speed = 0;
      }
      if (stone.state < Shaking) {
        stone.position.x += alongX(move, stone.angle);
        stone.position.z += alongZ(move, stone.angle);
      }
      if (stone.state == Shaking) {
        if (stone.speed > 0) {
          const std::int32_t step = shortOf(move / ShakeDivisor);
          stone.angle -= HalfTurn;
          if (stone.angle < 0) {
            stone.angle += kar::Turn;
          }
          stone.position.x = stone.target.x + alongX(step, stone.angle);
          stone.position.z = stone.target.z + alongZ(step, stone.angle);
          if (step == 0) {
            stone.speed = 0;
          }
        } else {
          stone.speed = 0;
          stone.state = Pegged;
        }
      }
      if (stone.state == Sliding && move <= 0 && stone.type == PeggedType) {
        stone.state = Pegged;
        sound(Bank, PegSound);
      }
    }
  }
  updateCollisions();
}

void Match::checkStonesStopped() {
  if (state_ != Rolling) {
    return;
  }
  bool moving = false;
  for (Team& team : teams_) {
    for (Stone& stone : team.stones) {
      if (stone.state > 0) {
        if (stone.speed > 0) {
          moving = true;
          break;
        }
        stone.speed = 0;
      }
    }
  }
  if (!moving) {
    state_ = RingsState;
  }
  for (Team& team : teams_) {
    for (Stone& stone : team.stones) {
      if (stone.state > 0 && stone.position.z >= OutLine && stone.speed <= 0) {
        stone.state = Out;
      }
    }
  }
}

void Match::updateCollisions() {
  bounceOffWall();
  if (state_ != Rolling) {
    return;
  }
  constexpr std::size_t All = Teams * StonesPerTeam;
  std::array<Point, All> now{};
  std::array<Point, All> before{};
  const auto at = [this](std::size_t slot) -> Stone& {
    return teams_.at(slot / StonesPerTeam).stones.at(slot % StonesPerTeam);
  };
  bool collided = false;
  for (std::int32_t step = 0; step <= Substeps; ++step) {
    for (std::size_t slot = 0; slot < All; ++slot) {
      const Stone& stone = at(slot);
      if (stone.state > 0) {
        now.at(slot).x =
            ((stone.target.x * (Substeps - step)) + (stone.position.x * step)) / Substeps;
        now.at(slot).z =
            ((stone.target.z * (Substeps - step)) + (stone.position.z * step)) / Substeps;
      }
    }
    for (std::size_t a = 0; a < All; ++a) {
      if (at(a).state <= 0) {
        continue;
      }
      for (std::size_t b = a + 1; b < All; ++b) {
        if (at(b).state <= 0) {
          continue;
        }
        if (distance(now.at(a).x - now.at(b).x, now.at(a).z - now.at(b).z) < Touch) {
          collided = true;
          if (step == 0) {
            resolveStoneCollision(at(b), now.at(b), at(a), now.at(a));
          } else {
            resolveStoneCollision(at(b), before.at(b), at(a), before.at(a));
          }
        }
      }
    }
    before = now;
    // A knock on the last step runs that step again.
    if (collided && step == Substeps) {
      --step;
    }
    collided = false;
  }
}

void Match::bounceOffWall() {
  for (Team& team : teams_) {
    for (Stone& stone : team.stones) {
      if (stone.state <= 0 || stone.speed == 0) {
        continue;
      }
      const std::int32_t zone = wallZone(stone.position.x, stone.position.z);
      if (zone > 0 && zone < 6) {
        sound(Bank, WallSound);
      }
      switch (zone) {
      case 1: {
        const std::int32_t past = stone.position.x + SideMirror;
        stone.position.x -= past * 2;
        stone.angle = HalfTurn - stone.angle;
        break;
      }
      case 2: {
        stone.angle = (kar::Turn + HalfTurn) - stone.angle;
        const std::int32_t past = stone.position.x - SideMirror;
        stone.position.x -= past * 2;
        break;
      }
      case 3:
      case 4: {
        const bool right = zone == 4;
        const Point contact = wallContact(stone, right);
        reflectOffDiagonal(stone, right);
        placeAtContact(stone, contact);
        break;
      }
      case 5: {
        stone.angle = -stone.angle;
        const std::int32_t past = BackMirror - stone.position.z;
        stone.position.z += past * 2;
        break;
      }
      case 6:
        if (stone.target.z < FrontLine) {
          sound(Bank, WallSound);
          stone.angle = -stone.angle;
          const std::int32_t past = stone.position.z - FrontLine;
          stone.position.z -= past * 2;
        }
        break;
      default:
        break;
      }
    }
  }
}

void Match::resolveStoneCollision(Stone& a, Point atA, Stone& b, Point atB) {
  const std::int32_t speed = shortOf(a.speed + b.speed);
  sound(Bank, speed >= HardKnock ? HardKnockSound : SoftKnockSound);
  if (a.pegged() || b.pegged()) {
    if (a.pegged()) {
      collidePeggedStone(a, atA, b, atB);
    } else {
      collidePeggedStone(b, atB, a, atA);
    }
  } else if (a.speed <= 0 || b.speed <= 0) {
    if (a.speed <= 0) {
      collideRestingStone(a, atA, b, atB);
    } else {
      collideRestingStone(b, atB, a, atA);
    }
  } else {
    collideMovingStones(b, atB, a, atA);
  }
}

// KAR_collidePeggedStone: the mover glances off a fixed stone, which shakes with half its speed
// scaled by how square the knock was.
void Match::collidePeggedStone(Stone& pegged, Point a, Stone& mover, Point b) {
  const std::int32_t heading =
      ratan2(mover.target.z - mover.position.z, mover.target.x - mover.position.x) & AngleMask;
  const std::int32_t normal = ratan2(b.z - a.z, b.x - a.x) & AngleMask;
  std::int32_t bounce = (normal * 2) - heading;
  if (bounce >= kar::Turn) {
    bounce %= kar::Turn;
  }
  std::int32_t share = std::abs(normal - heading) % QuarterTurn;
  share = QuarterTurn - share;
  std::int32_t length = distance(mover.position.x - b.x, mover.position.z - b.z);
  length = share * length / QuarterTurn;
  std::int32_t gap = 0;
  do {
    mover.position.x = b.x + alongX(length, bounce);
    mover.position.z = b.z + alongZ(length, bounce);
    ++length;
    gap = distance(mover.position.x - pegged.position.x, mover.position.z - pegged.position.z);
  } while (gap < Apart);
  const std::int32_t given = (mover.speed >> 1) * share / QuarterTurn;
  pegged.speed = shortOf(given);
  mover.speed = shortOf(mover.speed - given);
  pegged.angle = ratan2(mover.target.z - b.z, mover.target.x - b.x);
  mover.angle = bounce;
  pegged.state = Shaking;
  mover.target = b;
}

// KAR_collideRestingStone: the mover pushes a stopped stone off with its impact share and both are
// parted until they no longer touch.
void Match::collideRestingStone(Stone& resting, Point a, Stone& mover, Point b) {
  if (resting.speed == 0 && mover.speed == 0) {
    resting.position = a;
    mover.position = b;
    return;
  }
  const std::int32_t share = impactShare(mover, b, a);
  const Point push = separation(mover, b, a);
  resting.position.x += push.x;
  resting.position.z += push.z;
  mover.position.x -= push.x;
  mover.position.z -= push.z;
  std::int32_t gap =
      distance(resting.position.x - mover.position.x, resting.position.z - mover.position.z);
  const std::int32_t angle = ratan2(push.z, push.x);
  const std::int32_t size = distance(push.x, push.z);
  std::int32_t length = 0;
  const Point restingFrom = resting.position;
  const Point moverFrom = mover.position;
  if (size != 0) {
    while (gap < Apart) {
      resting.position.x = restingFrom.x + alongX(length, angle);
      resting.position.z = restingFrom.z + alongZ(length, angle);
      mover.position.x = moverFrom.x - alongX(length, angle);
      mover.position.z = moverFrom.z - alongZ(length, angle);
      gap = distance(resting.position.x - mover.position.x, resting.position.z - mover.position.z);
      ++length;
    }
  } else {
    const std::int32_t away =
        ratan2(mover.position.z - resting.position.z, mover.position.x - resting.position.x);
    while (gap < Apart) {
      mover.position.x = moverFrom.x + alongX(length, away);
      mover.position.z = moverFrom.z + alongZ(length, away);
      gap = distance(resting.position.x - mover.position.x, resting.position.z - mover.position.z);
      ++length;
    }
  }
  resting.target = a;
  mover.target = b;
  mover.speed = shortOf(mover.speed - share);
  resting.speed = shortOf(share);
  resting.angle = ratan2(resting.position.z - a.z, resting.position.x - a.x) & AngleMask;
  mover.angle = ratan2(mover.position.z - b.z, mover.position.x - b.x) & AngleMask;
}

void Match::collideMovingStones(Stone& stoneA, Point a, Stone& stoneB, Point b) {
  const std::int32_t shareA = impactShare(stoneA, a, b);
  const std::int32_t shareB = impactShare(stoneB, b, a);
  const Point pushB = separation(stoneB, b, a);
  const Point pushA = separation(stoneA, a, b);
  const Point delta{.x = pushA.x - pushB.x, .z = pushA.z - pushB.z};
  if (distance(delta.x, delta.z) != 0) {
    const std::int32_t angle = ratan2(delta.z, delta.x);
    stoneB.position.x += delta.x;
    stoneB.position.z += delta.z;
    stoneA.position.x -= delta.x;
    stoneA.position.z -= delta.z;
    std::int32_t gap =
        distance(stoneA.position.x - stoneB.position.x, stoneA.position.z - stoneB.position.z);
    std::int32_t length = 0;
    const Point fromA = stoneA.position;
    const Point fromB = stoneB.position;
    stoneA.speed = shortOf(stoneA.speed + shortOf(shareB - shareA));
    stoneB.speed = shortOf(stoneB.speed + shortOf(shareA - shareB));
    if (stoneA.speed > stoneB.speed) {
      while (gap < Apart) {
        stoneA.position.x = fromA.x + alongX(length, angle);
        stoneA.position.z = fromA.z + alongZ(length, angle);
        gap =
            distance(stoneB.position.x - stoneA.position.x, stoneB.position.z - stoneA.position.z);
        ++length;
      }
    } else {
      while (gap < Apart) {
        stoneB.position.x = fromB.x + alongX(length, angle);
        stoneB.position.z = fromB.z + alongZ(length, angle);
        gap =
            distance(stoneB.position.x - stoneA.position.x, stoneB.position.z - stoneA.position.z);
        ++length;
      }
    }
    stoneA.target = a;
    stoneB.target = b;
    stoneA.angle = ratan2(stoneA.position.z - a.z, stoneA.position.x - a.x);
    stoneB.angle = ratan2(stoneB.position.z - b.z, stoneB.position.x - b.x);
  } else if (stoneB.speed / stoneA.weight < StallMove || stoneA.speed / stoneB.weight < StallMove) {
    stoneB.speed = 0;
    stoneB.position = stoneB.target;
    stoneA.position = stoneA.target;
  } else {
    stoneB.position = b;
    stoneA.position = a;
  }
}

} // namespace dwe::game::curling
