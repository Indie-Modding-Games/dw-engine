#include <dwe/game/curling/curling.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

#include "kar_math.hpp"

// The match (kar.c: KAR_start, KAR_setupMatch, KAR_tick, KAR_tickMatchState and what it calls):
// the turns, the prompts, the hint pages, the score tally and the opponent's shots.
namespace dwe::game::curling {

namespace {

using kar::alongX;
using kar::alongZ;
using kar::distance;
using kar::ratan2;

// The stone states (KarStone.state).
constexpr std::int32_t InHand = 0;
constexpr std::int32_t Sliding = 1;
constexpr std::int32_t Pegged = 2;
constexpr std::int32_t Waiting = -1;
constexpr std::int32_t Out = -101;
constexpr std::int32_t Blinking = 100;
constexpr std::int32_t PeggedType = 3;
constexpr std::int32_t LastType = 3;

// KAR_setupMatch: the obstacles' x (KAR_D_8005AB98) less rand() % 319, their z -(rand() % 1000 +
// 1000); the racks' stones at x -+700 and heights i * 170 - 80.
constexpr std::array<std::int32_t, 3> ObstacleX{0x2C1, 0xEB, -0xEB};
constexpr std::int32_t ObstacleSpread = 0x13F;
constexpr std::int32_t ObstacleDepth = 0x3E8;
constexpr std::int32_t ObstacleCount = 3;
constexpr std::int32_t RackX = 0x2BC;
constexpr std::int32_t RackStep = 0xAA;
constexpr std::int32_t RackBase = 0x50;
constexpr std::int32_t TypeRoll = 10;

// KAR_tickMatchState.
constexpr std::int32_t ReadyBlinks = 0x30;
constexpr std::int32_t ReadyTop = 0x31;
constexpr std::int32_t IntroStep = 6;
constexpr std::int32_t IntroEnd = 0x169;
constexpr std::int32_t AiStepFrames = 0x10;
constexpr std::int32_t AiStepBack = 0xF;
constexpr std::int32_t AimStart = -0x400;
constexpr std::int32_t AimRightLimit = -0x18F;
constexpr std::int32_t AimLeftLimit = -0x5B6;
constexpr std::int32_t AiTurn = 0xA;
constexpr std::int32_t ThrowFrames = 3;
constexpr std::int32_t DemoPower = 0x7D0;
constexpr Point DemoAim{.x = 0x190, .z = -0xC1C};
constexpr Point TargetCentre{.x = 0, .z = -0x6A4};
constexpr std::int32_t RackShowStep = 0x3C;
constexpr std::int32_t HeldOffset = 0x50;
// KAR_classifyStoneRings: only stones past this line count.
constexpr std::int32_t ScoreLine = -0x564;
constexpr std::int32_t OpponentRing = 0x10;
constexpr std::int32_t RingMask = 0xF;
// KAR_updateRingMarkers: the rings with map objects (MAIN_D_80134A48).
constexpr std::size_t MarkedRings = 4;
// KAR_handleAimScroll: the turn step past which Left and Right also scroll, and the pixels a frame
// across and along the rink.
constexpr std::int32_t ScrollTurnStep = 10;
constexpr std::int32_t ScrollAcross = 8;
constexpr std::int32_t ScrollAlong = 10;
constexpr std::int32_t MinusRing = 3;
// KAR_tickScoreTally: a scoring stone blinks 8 times, 10 frames apart after 12.
constexpr std::int32_t BlinkFirst = 0xB;
constexpr std::int32_t BlinkGap = 10;
constexpr std::int32_t BlinkCount = 7;

// The hint groups (KAR_tickHintBox's n).
constexpr std::int32_t SelectHint = 0;
constexpr std::int32_t AimHint = 1;
constexpr std::int32_t PowerHint = 2;
constexpr std::int32_t DemoEndHint = 3;
constexpr std::int32_t BehindHint = 4;
constexpr std::int32_t AheadHint = 5;
constexpr std::int32_t LevelHint = 6;
constexpr std::int32_t LostHint = 7;
constexpr std::int32_t WonHint = 8;

// The sounds: bank 0 the menus', bank 8 the minigame's.
constexpr std::int32_t MenuBank = 0;
constexpr std::int32_t CursorSound = 2;
constexpr std::int32_t PageSound = 3;
constexpr std::int32_t Bank = 8;
constexpr std::int32_t ReleaseSound = 0;
constexpr std::int32_t RingSound = 5;
constexpr std::int32_t MinusRingSound = 6;
constexpr std::int32_t WinSound = 7;
constexpr std::int32_t LoseSound = 8;

// The animations (startAnimation) by thrower.
constexpr std::int32_t Player = 0;
constexpr std::int32_t Rival = 1;
constexpr std::int32_t Partner = 2;
constexpr std::int32_t StandAnimation = 0;
constexpr std::int32_t PoseAnimation = 2;
// KAR_renderAimArrow: the dots 200 apart, mirrored off the walls at 0x2EE, the shade's wave over
// 0xBF on 0x40, the gap three frames on each dot, the head turned a quarter less.
constexpr std::int32_t ArrowStep = 200;
constexpr std::int32_t ArrowWall = 0x2EE;
constexpr std::int32_t ArrowShadeSpan = 0xBF;
constexpr std::int32_t ArrowShadeBase = 0x40;
constexpr std::int32_t ArrowGapTop = 0x28;
constexpr std::int32_t ArrowGapFrames = 3;
constexpr std::int32_t ArrowHeadTurn = 0xC00;
// KAR_finishMatch: the throwers and the partner turn to face the camera.
constexpr std::int32_t FinishFacing = 0x800;
constexpr std::int32_t PlayerAim = 0x23;
constexpr std::int32_t PlayerThrow = 0x24;
constexpr std::int32_t PlayerRelease = 0x25;
constexpr std::int32_t FirstAim = 0x1C;
constexpr std::int32_t FirstThrow = 0x1D;
constexpr std::int32_t FirstRelease = 0x1E;
constexpr std::int32_t SecondThrow = 0x1F;
constexpr std::int32_t SecondRelease = 0x20;

// The opponent's shots (KAR_chooseOpponentShot): the spots it aims at.
constexpr Point LeftRing{.x = -0x190, .z = -0x8FC};
constexpr Point RightRing{.x = 0x190, .z = -0x8FC};
constexpr Point DeepCentre{.x = 0, .z = -0x960};
constexpr Point DeepLeft{.x = -0x190, .z = -0x960};
constexpr Point FarBack{.x = -0x190, .z = -0xC1C};
constexpr std::int32_t BankPower = 0x960;
constexpr std::int32_t NoStone = 6;
constexpr std::int32_t AimJitter = 100;
// KAR_D_8005B418: the stone types each shot plan tries, in order.
constexpr std::array<std::array<std::int32_t, 4>, 2> ShotPlans{{{1, 0, 2, 3}, {3, 2, 0, 1}}};
// KAR_findClearShotAngle: the first opponent looks along its line 30 units at a time, the second
// 10; a stone closer than 151 blocks it. Suposición: a line that never gets past the target stops
// after this many steps (the original would loop for ever; its targets are always past it).
constexpr std::int32_t FirstLookStep = 30;
constexpr std::int32_t SecondLookStep = 10;
constexpr std::int32_t Blocked = 0x97;
constexpr std::int32_t LookLimit = 4096;
// KAR_aimBankShot: the stone's edge and the left wall's mirror.
constexpr std::int32_t StoneRadius = 150;
constexpr std::int32_t WallMirror = 0x2D5;
constexpr std::int32_t Hundred = 100;
constexpr std::uint32_t Keep = 98;

std::int32_t shortOf(std::int32_t value) {
  return static_cast<std::int16_t>(value);
}

// libc rand() % limit, as the overlay rolls.
std::int32_t roll(ruleset::Random& random, std::int32_t limit) {
  return static_cast<std::int32_t>(random.next() % static_cast<std::uint32_t>(limit));
}

} // namespace

Match::Match(const CurlingRules& rules, Opponent opponent, ruleset::Random& random)
    : rules_{&rules}, opponent_{opponent}, opponentHand_{OpponentMark} {
  // KAR_D_80063914 starts with the overlay's leftover bytes: a positive state, type 0 and ring
  // 0x52.
  lastPlayerStone_.state = Sliding;
  lastPlayerStone_.ring = 0x52;
  setupMatch(false, random);
}

void Match::setHands(Point player, Point opponent) {
  playerHand_ = player;
  opponentHand_ = opponent;
}

void Match::setScene(const std::array<Team, Teams>& teams, Phase phase, std::int32_t team,
                     std::size_t stone) {
  teams_ = teams;
  state_ = static_cast<std::int32_t>(phase);
  team_ = team;
  selected_ = static_cast<std::int32_t>(stone);
  // The stones out of their racks count as thrown before this one, the player's first.
  std::size_t order = 0;
  for (std::size_t line = 0; line < ObstacleTeam; ++line) {
    for (std::size_t index = 0; index < StonesPerTeam; ++index) {
      const bool throwing = std::cmp_equal(line, team) && index == stone;
      if (!throwing && teams_.at(line).stones.at(index).state != Waiting &&
          order < thrownOrder_.size()) {
        thrownOrder_.at(order++) = {.team = line, .index = index};
      }
    }
  }
  counter_ = 0;
  power_ = 0;
  box_ = 0;
}

std::optional<std::int32_t> Match::promptOption() const {
  if (state_ != static_cast<std::int32_t>(Phase::Prompt) || box_ < 3) {
    return std::nullopt;
  }
  return box_ - 3;
}

std::optional<std::pair<std::int32_t, std::int32_t>> Match::hint() const {
  if (state_ == static_cast<std::int32_t>(Phase::Prompt) || hintGroup_ < 0 || box_ < 1 ||
      (boxState_ != Box::Open && boxState_ != Box::Opening)) {
    return std::nullopt;
  }
  return std::pair{hintGroup_, box_};
}

void Match::emit(CurlingEvent::Kind kind, std::int32_t a, std::int32_t b) {
  events_.push_back({.kind = kind, .a = a, .b = b});
}

void Match::setupMatch(bool demonstration, ruleset::Random& random) {
  const std::int32_t obstacles = roll(random, ObstacleCount);
  selected_ = 0;
  team_ = 0;
  power_ = 0;
  for (std::size_t team = 0; team < Teams; ++team) {
    Team& line = teams_.at(team);
    static_cast<void>(random.next());
    line.score = 0;
    line.thrown = 0;
    line.plannedPower = 0;
    line.plannedStone = 0;
    for (std::size_t index = 0; index < StonesPerTeam; ++index) {
      Stone& stone = line.stones.at(index);
      const std::int32_t pick = roll(random, TypeRoll);
      stone.speed = 0;
      stone.angle = 0;
      stone.ring = 0;
      stone.previousRing = 0;
      stone.spin = 0;
      if (!demonstration) {
        stone.type =
            rules_->stoneTypes.at(static_cast<std::size_t>(pick) % rules_->stoneTypes.size());
      } else {
        stone.type = std::min(static_cast<std::int32_t>(index), LastType);
      }
      const auto i = static_cast<std::int32_t>(index);
      if (team < ObstacleTeam) {
        stone.height = (i * RackStep) - RackBase;
        stone.position = {.x = team != 0 ? RackX : -RackX, .z = 0};
        stone.state = Waiting;
      } else if (i < obstacles) {
        stone.height = 0;
        stone.position.x = ObstacleX.at(index) - roll(random, ObstacleSpread);
        stone.position.z = -(roll(random, ObstacleDepth) + ObstacleDepth);
        stone.state = Sliding;
      } else {
        stone.height = 0;
        stone.position = {};
        stone.state = Out;
      }
      stone.target = stone.position;
      stone.weight = rules_->weights.at(static_cast<std::size_t>(stone.type));
    }
  }
}

std::span<const CurlingEvent> Match::tick(const CurlingButtons& buttons, ruleset::Random& random) {
  events_.clear();
  // The UI box's open and close animations end on the next frame.
  if (boxState_ == Box::Opening) {
    boxState_ = Box::Open;
  } else if (boxState_ == Box::Closing) {
    boxState_ = Box::Closed;
  }
  // KAR_tick.
  tickMatchState(buttons, random);
  renderAimArrow();
  handleAimScroll(buttons);
  updateRingMarkers();
  // KAR_renderReadyPrompt's counter.
  if (state_ == static_cast<std::int32_t>(Phase::Ready)) {
    if (blink_++ >= ReadyTop) {
      blink_ = ReadyBlinks;
    }
  } else {
    blink_ = 0;
  }
  checkStonesStopped();
  // The stones' object tick (KAR_tickStones), gone once the match is over (KAR_finishMatch).
  if (!finished()) {
    tickStones();
  }
  placeHeldStone();
  return events_;
}

void Match::handleAimScroll(const CurlingButtons& buttons) {
  // KAR_handleAimScroll: while the player aims, Up and Down scroll the view; Left and Right too
  // once the aim turns fast (ten frames held) or a hint is open.
  if (state_ != static_cast<std::int32_t>(Phase::Aim) || team_ != 0) {
    return;
  }
  std::int32_t dx = 0;
  std::int32_t dy = 0;
  if (turnStep_ >= ScrollTurnStep || turnStep_ <= -ScrollTurnStep || box_ != 0) {
    dx += (buttons.left ? -ScrollAcross : 0) + (buttons.right ? ScrollAcross : 0);
  }
  dy += (buttons.up ? -ScrollAlong : 0) + (buttons.down ? ScrollAlong : 0);
  if (dx != 0 || dy != 0) {
    emit(CurlingEvent::Kind::Scroll, dx, dy);
  }
}

void Match::updateRingMarkers() {
  // KAR_updateRingMarkers: a ring lights while a sliding stone of either team is over it (each
  // stone lights the first ring not yet lit that holds it), and in the tally while the counted
  // stone shows in the ring it newly scored in.
  litRings_ = 0;
  const std::size_t rings = std::min(rules_->rings.size(), MarkedRings);
  if (state_ == static_cast<std::int32_t>(Phase::Rolling)) {
    for (std::size_t team = 0; team < ObstacleTeam; ++team) {
      for (const Stone& stone : teams_.at(team).stones) {
        if (stone.state <= 0 || stone.speed <= 0) {
          continue;
        }
        for (std::size_t zone = 0; zone < rings; ++zone) {
          const Ring& ring = rules_->rings.at(zone);
          const std::uint32_t bit = 1U << zone;
          if ((litRings_ & bit) == 0 &&
              distance(stone.position.x - ring.x, stone.position.z - ring.z) < ring.radius) {
            litRings_ |= bit;
            break;
          }
        }
      }
    }
  } else if (state_ == static_cast<std::int32_t>(Phase::Tally) && tallyStone_ >= 0) {
    // The original reads the slot before the order's first when none is left (-1); here nothing.
    const Stone& stone = stoneAt(thrownOrder_.at(static_cast<std::size_t>(tallyStone_)));
    const std::int32_t ring = stone.ring & RingMask;
    if (stone.ring != stone.previousRing && stone.state <= Blinking && ring != 0 &&
        std::cmp_less_equal(ring, rings)) {
      litRings_ = 1U << static_cast<std::uint32_t>(ring - 1);
    }
  }
}

bool Match::shownThisFrame(std::size_t team, std::size_t index) const {
  // KAR_renderScene's tests before it places a stone in a hand.
  if (team == OpponentTeam && state_ != static_cast<std::int32_t>(Phase::Ready) &&
      state_ < static_cast<std::int32_t>(Phase::Turn) &&
      (box_ != 0 || state_ < static_cast<std::int32_t>(Phase::Select))) {
    return false;
  }
  return state_ >= static_cast<std::int32_t>(Phase::DemoThrow) ||
         counter_ >= (static_cast<std::int32_t>(index) * RackShowStep) + RackShowStep;
}

bool Match::stoneDrawn(std::size_t team, std::size_t index) const {
  if (team >= Teams || index >= StonesPerTeam || !shownThisFrame(team, index)) {
    return false;
  }
  // GsSortObject4J skips a stone below -100 (out) or above 100 (blinking off).
  const std::int32_t state = teams_.at(team).stones.at(index).state;
  return state >= -Blinking && state <= Blinking;
}

std::size_t stoneModelObject(std::size_t team, const Stone& stone) {
  constexpr std::array<std::size_t, Teams> TeamModels{2, 1, 0};
  constexpr std::int32_t Spiked = 3;
  constexpr std::size_t Looks = 3;
  const std::size_t model = TeamModels.at(std::min(team, Teams - 1));
  const bool takenInHand = team != ObstacleTeam && stone.type == Spiked &&
                           (stone.state == InHand || stone.state == Sliding);
  if (takenInHand || stone.type < 0 || stone.type > Spiked) {
    return model;
  }
  return (static_cast<std::size_t>(stone.type) * Looks) + model;
}

void Match::placeHeldStone() {
  // KAR_renderScene: a stone in a hand rides it (the stones it does not draw are not moved).
  for (std::size_t team = 0; team < Teams; ++team) {
    for (std::size_t index = 0; index < StonesPerTeam; ++index) {
      if (!shownThisFrame(team, index)) {
        continue;
      }
      Stone& stone = teams_.at(team).stones.at(index);
      if (stone.state != InHand) {
        continue;
      }
      // A stone in a hand keeps no spin, but beside the first opponent: a thrown stone slides
      // turned as that one faced when it let go, the others straight.
      if (team_ == 0) {
        stone.position = playerHand_;
        stone.spin = 0;
      } else if (opponent_ == Opponent::Penguinmon) {
        stone.position = {.x = OpponentMark.x - HeldOffset, .z = OpponentMark.z};
      } else {
        stone.position = opponentHand_;
        stone.spin = 0;
      }
    }
  }
}

std::int8_t Match::tickYesNoPrompt(const CurlingButtons& buttons) {
  // The two lines are drawn one a frame, then the box opens.
  if (box_ < 2) {
    ++box_;
  }
  if (boxState_ == Box::Closed) {
    if (box_ == 2) {
      boxState_ = Box::Opening;
      box_ = 3;
      emit(CurlingEvent::Kind::Prompt, 0);
    } else if (box_ == 3 || box_ == 4) {
      return static_cast<std::int8_t>(box_ - 2);
    }
  }
  if (boxState_ == Box::Open) {
    if (buttons.confirmPressed) {
      boxState_ = Box::Closing;
    } else if (buttons.upPressed || buttons.downPressed) {
      box_ = box_ == 3 ? 4 : 3;
      sound(MenuBank, CursorSound);
      emit(CurlingEvent::Kind::Prompt, box_ - 3);
    }
  }
  return 0;
}

bool Match::tickHintBox(std::int32_t group, const CurlingButtons& buttons) {
  if (box_ == 0) {
    return false;
  }
  if (boxState_ == Box::Closed) {
    if (box_ == 1) {
      boxState_ = Box::Opening;
      hintGroup_ = group;
      emit(CurlingEvent::Kind::Hint, group, 1);
      return true;
    }
    return false;
  }
  hintGroup_ = group;
  const std::int32_t pages = HintPages.at(static_cast<std::size_t>(group));
  if (boxState_ == Box::Open && buttons.confirmPressed) {
    ++box_;
    sound(MenuBank, PageSound);
    if (pages < box_) {
      boxState_ = Box::Closing;
      box_ = -1;
      emit(CurlingEvent::Kind::Hint, group, 0);
    } else {
      emit(CurlingEvent::Kind::Hint, group, box_);
    }
  }
  return true;
}

void Match::beginAiming() {
  state_ = static_cast<std::int32_t>(Phase::Aim);
  if (team_ == 0) {
    animate(Player, PlayerAim);
  } else if (opponent_ == Opponent::Penguinmon) {
    animate(Rival, FirstAim);
  }
  current().state = InHand;
  aim_ = AimStart;
  face(throwerWho(), 0);
}

void Match::selectPreviousStone() {
  for (std::int32_t index = selected_ - 1; index >= 0; --index) {
    if (row(static_cast<std::size_t>(team_)).stones.at(static_cast<std::size_t>(index)).state ==
        Waiting) {
      selected_ = index;
      return;
    }
  }
}

void Match::selectNextStone() {
  for (std::int32_t index = selected_ + 1; std::cmp_less(index, StonesPerTeam); ++index) {
    if (row(static_cast<std::size_t>(team_)).stones.at(static_cast<std::size_t>(index)).state ==
        Waiting) {
      selected_ = index;
      return;
    }
  }
}

void Match::turnAim(std::int32_t delta) {
  // KAR_turnAimLeft / KAR_turnAimRight: the thrower turns by -delta as the aim moves by delta.
  aim_ += shortOf(delta);
  face(throwerWho(), shortOf(facing(static_cast<std::size_t>(throwerWho())).value_or(0) - delta));
}

void Match::renderAimArrow() {
  // KAR_renderAimArrow, drawn in states 6 and 7 only.
  if (state_ < static_cast<std::int32_t>(Phase::Aim) ||
      state_ >= static_cast<std::int32_t>(Phase::Power)) {
    arrow_.reset();
    return;
  }
  // Its counters are unsigned 16-bit: the shade's (the intro counter, MAIN_D_80135252) moves by
  // 0xBE a frame unless it is 0, which goes to 0xFFFF; the gap's runs 0 to 0x28.
  const auto shadeCounter = static_cast<std::uint16_t>(counter_);
  counter_ = shadeCounter != 0 ? static_cast<std::uint16_t>(shadeCounter - 1 + ArrowShadeSpan)
                               : std::uint16_t{0xFFFF};
  if (arrowGap_++ >= ArrowGapTop) {
    arrowGap_ = 0;
  }
  const std::int32_t facing = facing_.at(static_cast<std::size_t>(throwerWho())).value_or(0);
  const Point from = current().position;
  AimArrow arrow;
  arrow.hidden = static_cast<std::size_t>(arrowGap_ / ArrowGapFrames);
  for (std::size_t dot = 0; dot < AimDots; ++dot) {
    // RotMatrix about y then ApplyMatrixSV of (0, 0, -200 (i + 1)).
    const std::int32_t length = -ArrowStep * static_cast<std::int32_t>(dot + 1);
    std::int32_t x = from.x + ((kar::rsin(facing) * length) >> 12);
    const std::int32_t z = from.z + ((kar::rcos(facing) * length) >> 12);
    while (x < -ArrowWall || x > ArrowWall) {
      if (x < -ArrowWall) {
        x -= 2 * (x + ArrowWall);
      }
      if (x > ArrowWall) {
        x -= 2 * (x - ArrowWall);
      }
    }
    arrow.dots.at(dot) = {.x = x, .z = z};
    const std::int32_t step = static_cast<std::int32_t>(dot) * ArrowShadeSpan;
    std::int32_t shade = (static_cast<std::int32_t>(counter_) + (step / 15)) & 0xFF;
    if (shade >= ArrowShadeSpan) {
      shade = (shade - ArrowShadeSpan) & 0xFF;
    }
    arrow.shades.at(dot) = shade + ArrowShadeBase;
  }
  const Point head = arrow.dots.back();
  const Point before = arrow.dots.at(AimDots - 2);
  arrow.headTurn = -((ratan2(before.z - head.z, before.x - head.x) - ArrowHeadTurn) % 0x1000);
  arrow_ = arrow;
}

std::optional<std::int32_t> Match::facing(std::size_t who) const {
  return who < facing_.size() ? facing_.at(who) : std::nullopt;
}

void Match::beginThrow() {
  state_ = static_cast<std::int32_t>(Phase::Camera);
  if (team_ == 0) {
    animate(Player, PlayerThrow);
  } else if (opponent_ == Opponent::Penguinmon) {
    animate(Rival, FirstThrow);
  } else {
    animate(Rival, SecondThrow);
  }
}

void Match::finishMatch() {
  if (finished()) {
    return;
  }
  state_ = static_cast<std::int32_t>(Phase::Finished);
  face(Player, FinishFacing);
  face(Partner, FinishFacing);
  face(Rival, FinishFacing);
  animate(Player, PoseAnimation);
  animate(Partner, PoseAnimation);
  animate(Rival, PoseAnimation);
  const std::int32_t mine = teams_.at(PlayerTeam).score;
  if (mine > teams_.at(OpponentTeam).score) {
    result_ = mine >= rules_->perfectScore ? 2 : 1;
  } else {
    result_ = 0;
  }
  emit(CurlingEvent::Kind::Finished, result_);
}

void Match::classifyStoneRings() {
  for (std::size_t team = 0; team < ObstacleTeam; ++team) {
    Team& line = teams_.at(team);
    line.score = 0;
    for (Stone& stone : line.stones) {
      stone.previousRing = stone.ring;
      stone.ring = 0;
      if (stone.state <= 0 || stone.position.z >= ScoreLine) {
        continue;
      }
      for (std::size_t zone = 0; zone < rules_->rings.size(); ++zone) {
        const Ring& ring = rules_->rings.at(zone);
        if (distance(stone.position.x - ring.x, stone.position.z - ring.z) > ring.radius) {
          continue;
        }
        stone.ring = static_cast<std::int32_t>(zone) + 1;
        if (team != 0) {
          stone.ring |= OpponentRing;
        }
        break;
      }
    }
  }
  registerThrownStone();
}

void Match::registerThrownStone() {
  tallyStone_ = teams_.at(PlayerTeam).thrown + teams_.at(OpponentTeam).thrown;
  thrownOrder_.at(static_cast<std::size_t>(tallyStone_)) = {
      .team = static_cast<std::size_t>(team_), .index = static_cast<std::size_t>(selected_)};
  tallyShown_ = -1;
}

bool Match::tickScoreTally(const CurlingButtons& buttons) {
  const auto points = [this](std::int32_t ring) {
    return rules_->rings.at(static_cast<std::size_t>(ring - 1)).points;
  };
  const auto at = [this](std::int32_t order) -> Stone& {
    return stoneAt(thrownOrder_.at(static_cast<std::size_t>(order)));
  };
  if (buttons.confirmPressed) {
    tallySkip_ = true;
  }
  if (tallySkip_ && (tallyStep_ == 2 || tallyStep_ == 1)) {
    // Skipped: every stone counts at once.
    tallyStone_ = teams_.at(PlayerTeam).thrown + teams_.at(OpponentTeam).thrown - 1;
    teams_.at(PlayerTeam).score = 0;
    teams_.at(OpponentTeam).score = 0;
    for (; tallyStone_ >= 0; --tallyStone_) {
      const Stone& stone = at(tallyStone_);
      const std::int32_t ring = stone.ring & RingMask;
      if (stone.state > 0 && ring != 0) {
        teams_.at((stone.ring & OpponentRing) != 0 ? 1 : 0).score += points(ring);
      }
    }
    for (Team& team : teams_) {
      for (Stone& stone : team.stones) {
        if (stone.state >= Blinking) {
          stone.state -= Blinking;
        }
      }
    }
    tallyBlinks_ = 0;
    tallyFrames_ = 0;
    tallySkip_ = false;
    tallyStep_ = 0;
    return true;
  }
  if (tallyShown_ != tallyStone_) {
    for (; tallyStone_ >= 0; --tallyStone_) {
      if ((at(tallyStone_).ring & RingMask) != 0) {
        tallyShown_ = tallyStone_;
        tallyStep_ = 0;
        break;
      }
    }
    if (tallyStone_ < 0) {
      tallySkip_ = false;
      return true;
    }
  }
  if (tallyStep_ == 0) {
    const Stone& stone = at(tallyStone_);
    if (stone.ring != stone.previousRing) {
      camera(stone.position);
    }
    tallyStep_ = 1;
  }
  if (tallyStep_ == 1) {
    const Stone& stone = at(tallyStone_);
    const std::int32_t team = (stone.ring & OpponentRing) != 0 ? 1 : 0;
    const std::int32_t ring = stone.ring & RingMask;
    teams_.at(static_cast<std::size_t>(team)).score += points(ring);
    tallyStep_ = 2;
    if (stone.ring != stone.previousRing && ring != 0) {
      sound(Bank, ring == MinusRing ? MinusRingSound : RingSound);
      emit(CurlingEvent::Kind::Tally, team, ring);
    }
  }
  if (tallyStep_ == 2 && tallyStone_ >= 0) {
    Stone& stone = at(tallyStone_);
    if (stone.ring != stone.previousRing && (stone.ring & RingMask) != 0) {
      if (tallyFrames_++ >= BlinkFirst) {
        stone.state += stone.state < Blinking ? Blinking : -Blinking;
        tallyFrames_ -= BlinkGap;
        if (tallyBlinks_++ >= BlinkCount) {
          tallyBlinks_ = 0;
          tallyStep_ = 4;
          --tallyStone_;
        }
      }
    } else {
      --tallyStone_;
    }
    return tallyStone_ < 0;
  }
  return false;
}

void Match::tickMatchState(const CurlingButtons& buttons, ruleset::Random& random) {
  Team& thrower = row(static_cast<std::size_t>(team_));
  switch (static_cast<Phase>(state_)) {
  case Phase::Placing:
    counter_ = 0;
    animate(Player, StandAnimation);
    animate(Rival, StandAnimation);
    face(Player, 0);
    face(Rival, 0);
    state_ = static_cast<std::int32_t>(Phase::Prompt);
    box_ = 0;
    break;
  case Phase::Prompt:
    switch (tickYesNoPrompt(buttons)) {
    case 1:
      state_ = static_cast<std::int32_t>(Phase::Ready);
      box_ = 0;
      break;
    case 2:
      state_ = static_cast<std::int32_t>(Phase::Demo);
      box_ = 0;
      setupMatch(true, random);
      break;
    default:
      break;
    }
    break;
  case Phase::Ready:
    if (blink_ >= ReadyBlinks) {
      counter_ = static_cast<std::uint16_t>(counter_ + IntroStep);
      if (counter_ >= IntroEnd) {
        counter_ = 0;
        state_ = static_cast<std::int32_t>(Phase::Select);
      }
    }
    break;
  case Phase::Demo:
    counter_ = static_cast<std::uint16_t>(counter_ + IntroStep);
    if (counter_ >= IntroEnd) {
      counter_ = 0;
      state_ = static_cast<std::int32_t>(Phase::DemoThrow);
    }
    break;
  case Phase::DemoThrow: {
    box_ = 1;
    state_ = static_cast<std::int32_t>(Phase::Select);
    Team& player = row(PlayerTeam);
    player.plannedStone = roll(random, static_cast<std::int32_t>(StonesPerTeam));
    player.plannedPower = DemoPower;
    player.stones.at(static_cast<std::size_t>(player.plannedStone)).angle =
        ratan2(DemoAim.z, DemoAim.x);
    break;
  }
  case Phase::Select:
    if (team_ != 0 || box_ != 0) {
      if (tickHintBox(SelectHint, buttons)) {
        break;
      }
      // The opponent (or the demonstration) steps through its rack to its planned stone.
      if (counter_++ >= AiStepFrames) {
        counter_ -= AiStepBack;
        if (selected_ == thrower.plannedStone) {
          Stone& ghost = teams_.at(ObstacleTeam).stones.at(StonesPerTeam - 1);
          ghost = current();
          ghost.state = Out;
          beginAiming();
          counter_ = 0;
          if (box_ != 0) {
            box_ = 1;
          }
        }
        if (thrower.plannedStone > selected_) {
          selectNextStone();
        }
      }
    } else if (buttons.confirmPressed) {
      beginAiming();
    } else if (buttons.upPressed) {
      selectPreviousStone();
    } else if (buttons.downPressed) {
      selectNextStone();
    }
    turnStep_ = 0;
    break;
  case Phase::Aim:
    if (team_ != 0 || box_ != 0) {
      const std::int32_t target = current().angle;
      if (tickHintBox(AimHint, buttons)) {
        if (buttons.right && aim_ < AimRightLimit) {
          turnStep_ = turnStep_ > 0 ? shortOf(turnStep_ + 1) : 1;
          turnAim(turnStep_);
        } else if (buttons.left && aim_ >= AimLeftLimit) {
          turnStep_ = turnStep_ < 0 ? shortOf(turnStep_ - 1) : -1;
          turnAim(turnStep_);
        } else if (!buttons.left && !buttons.right) {
          turnStep_ = 0;
          break;
        } else {
          break;
        }
      }
      if (target <= aim_ + AiTurn && target >= aim_ - AiTurn) {
        beginThrow();
        if (box_ != 0) {
          box_ = 1;
        }
      } else if (target > aim_) {
        turnAim(AiTurn);
      } else {
        turnAim(-AiTurn);
      }
    } else {
      if (buttons.confirmPressed) {
        beginThrow();
      }
      if (buttons.right && aim_ < AimRightLimit) {
        turnStep_ = turnStep_ > 0 ? shortOf(turnStep_ + 1) : 1;
        turnAim(turnStep_);
      }
      if (buttons.left && aim_ >= AimLeftLimit) {
        turnStep_ = turnStep_ < 0 ? shortOf(turnStep_ - 1) : -1;
        turnAim(turnStep_);
      }
      if (!buttons.left && !buttons.right) {
        turnStep_ = 0;
      }
    }
    break;
  case Phase::Camera:
    // tickCameraMoveTo: the camera goes to the stone (here at once).
    camera(current().position);
    state_ = static_cast<std::int32_t>(Phase::Power);
    turnStep_ = 0;
    break;
  case Phase::Power:
    power_ = shortOf(power_ + rules_->powerStep);
    if (power_ >= rules_->powerLimit) {
      power_ = 0;
      state_ = static_cast<std::int32_t>(Phase::Aim);
      animate(throwerWho(), StandAnimation);
    } else if (team_ != 0 || box_ != 0) {
      if (tickHintBox(PowerHint, buttons)) {
        if (thrower.plannedPower <= power_) {
          power_ = thrower.plannedPower;
        }
      } else if (row(OpponentTeam).plannedPower <= power_) {
        // The original reads the opponent's planned power here for the demonstration's throw too,
        // and its facing for the stone's.
        state_ = static_cast<std::int32_t>(Phase::Release);
        current().spin = facing(static_cast<std::size_t>(Rival)).value_or(0);
        if (box_ != 0) {
          box_ = 1;
        }
      }
    } else if (buttons.confirmPressed) {
      state_ = static_cast<std::int32_t>(Phase::Release);
      current().angle = aim_;
      current().spin = facing(static_cast<std::size_t>(Player)).value_or(0);
    }
    break;
  case Phase::Release:
    if (team_ == 0) {
      animate(Player, PlayerRelease);
    } else {
      animate(Rival, opponent_ == Opponent::Penguinmon ? FirstRelease : SecondRelease);
    }
    current().speed = shortOf(power_ + rules_->throwBonus);
    state_ = static_cast<std::int32_t>(Phase::Throwing);
    counter_ = 0;
    sound(Bank, ReleaseSound);
    followFrom_ = current().position;
    break;
  case Phase::Throwing:
    if (counter_++ >= ThrowFrames) {
      counter_ = 0;
      current().state = Sliding;
      followFrom_ = current().position;
      state_ = static_cast<std::int32_t>(Phase::Rolling);
    }
    break;
  case Phase::Rolling: {
    // moveCameraByDiff: the camera moves by the thrown stone's step. The state runs before the
    // stones move, so the camera trails the stone by a frame as in the original.
    const Point at = current().position;
    emit(CurlingEvent::Kind::Follow, at.x - followFrom_.x, at.z - followFrom_.z);
    followFrom_ = at;
    break;
  }
  case Phase::Rings:
    emit(CurlingEvent::Kind::SoundStop);
    classifyStoneRings();
    ++thrower.thrown;
    state_ = static_cast<std::int32_t>(Phase::Tally);
    for (std::size_t team = 0; team < ObstacleTeam; ++team) {
      Team& line = teams_.at(team);
      line.thrown = static_cast<std::int32_t>(std::ranges::count_if(
          line.stones, [](const Stone& stone) { return stone.state != Waiting; }));
    }
    break;
  case Phase::Tally:
    if (tickScoreTally(buttons)) {
      state_ = static_cast<std::int32_t>(box_ != 0 ? Phase::DemoCamera : Phase::NextThrower);
    }
    break;
  case Phase::DemoCamera:
    camera(TargetCentre);
    state_ = static_cast<std::int32_t>(Phase::NextThrower);
    break;
  case Phase::NextThrower:
    if (!tickHintBox(DemoEndHint, buttons)) {
      animate(Player, StandAnimation);
      animate(Rival, StandAnimation);
      if (team_ == 0) {
        lastPlayerStone_ = row(PlayerTeam).stones.at(static_cast<std::size_t>(selected_));
      }
      if (box_ != 0) {
        // After the demonstration: the camera back to the thrower and the real match.
        camera(team_ != 0 ? OpponentMark : PlayerMark);
        box_ = 0;
        state_ = static_cast<std::int32_t>(Phase::Ready);
        setupMatch(false, random);
        counter_ = 0;
      } else {
        const Team& player = row(PlayerTeam);
        const Team& rival = row(OpponentTeam);
        if (player.thrown == rival.thrown) {
          team_ = player.thrown == static_cast<std::int32_t>(StonesPerTeam) ||
                          player.score >= rival.score
                      ? 1
                      : 0;
        } else {
          team_ = player.thrown < rival.thrown ? 0 : 1;
        }
        const Team& next = row(static_cast<std::size_t>(team_));
        for (std::size_t index = 0; index < StonesPerTeam; ++index) {
          if (next.stones.at(index).state == Waiting) {
            selected_ = static_cast<std::int32_t>(index);
            break;
          }
        }
        counter_ = 0;
        power_ = 0;
        state_ = static_cast<std::int32_t>(Phase::Turn);
      }
    }
    break;
  case Phase::Turn:
    camera(team_ != 0 ? OpponentMark : PlayerMark);
    if (row(PlayerTeam).thrown >= static_cast<std::int32_t>(StonesPerTeam) &&
        row(OpponentTeam).thrown >= static_cast<std::int32_t>(StonesPerTeam)) {
      box_ = 1;
      state_ = static_cast<std::int32_t>(Phase::Comment);
      if (row(PlayerTeam).score > row(OpponentTeam).score) {
        sound(Bank, WinSound);
        counter_ = WonHint;
      } else {
        sound(Bank, LoseSound);
        counter_ = LostHint;
      }
    } else if (team_ != 0) {
      state_ = static_cast<std::int32_t>(Phase::OpponentPlan);
    } else {
      state_ = static_cast<std::int32_t>(Phase::Select);
    }
    break;
  case Phase::OpponentPlan: {
    chooseOpponentShot(random);
    state_ = static_cast<std::int32_t>(Phase::Comment);
    box_ = 1;
    power_ = 0;
    const std::int32_t mine = row(PlayerTeam).score;
    const std::int32_t theirs = row(OpponentTeam).score;
    counter_ = mine > theirs ? AheadHint : mine == theirs ? LevelHint : BehindHint;
    break;
  }
  case Phase::WaitBox:
    if (boxState_ == Box::Open) {
      state_ = static_cast<std::int32_t>(Phase::Comment);
    }
    break;
  case Phase::Comment:
    if (!tickHintBox(counter_, buttons)) {
      box_ = 0;
      if (counter_ >= LostHint) {
        finishMatch();
      } else {
        state_ = static_cast<std::int32_t>(Phase::Select);
        counter_ = 0;
        power_ = 0;
      }
    }
    break;
  case Phase::Finished:
    counter_ = 0;
    power_ = 0;
    finishMatch();
    break;
  }
}

// --- The opponent (kar.c: KAR_chooseOpponentShot and its helpers) ---

std::int32_t Match::findUnusedStoneOfType(std::int32_t type) const {
  const Team& rival = teams_.at(OpponentTeam);
  for (std::size_t index = 0; index < StonesPerTeam; ++index) {
    if (rival.stones.at(index).state == Waiting && rival.stones.at(index).type == type) {
      return static_cast<std::int32_t>(index);
    }
  }
  return -1;
}

std::int32_t Match::computeThrowPower(std::int32_t weight, std::int32_t x, std::int32_t z) const {
  // The power whose slide, friction on, covers the way from the opponent's mark to (x, z).
  const std::int32_t way = distance(OpponentMark.x - shortOf(x), OpponentMark.z - shortOf(z));
  const auto heavy = static_cast<std::uint32_t>(weight);
  std::uint32_t covered = 0;
  std::uint32_t speed = heavy;
  std::uint32_t value = speed * Hundred;
  while (covered < static_cast<std::uint32_t>(way)) {
    covered += speed / heavy;
    value = value * Hundred / Keep;
    speed = value / Hundred;
  }
  const std::size_t who = opponent_ == Opponent::Penguinmon ? 0 : 1;
  speed -= static_cast<std::uint32_t>(rules_->opponentPowerOffsets.at(who));
  return static_cast<std::int32_t>(speed);
}

void Match::setOpponentShot(std::size_t plan, std::int32_t angle, std::int32_t x, std::int32_t z) {
  Team& rival = teams_.at(OpponentTeam);
  for (const std::int32_t type : ShotPlans.at(plan)) {
    const std::int32_t index = findUnusedStoneOfType(type);
    if (index >= 0) {
      rival.plannedStone = index;
      Stone& stone = rival.stones.at(static_cast<std::size_t>(index));
      stone.angle = angle;
      rival.plannedPower = shortOf(computeThrowPower(stone.weight, x, z));
      return;
    }
  }
}

std::int32_t Match::findClearShotAngle(std::int32_t x, std::int32_t z) const {
  const bool first = opponent_ == Opponent::Penguinmon;
  const Point from = first ? OpponentMark : opponentHand_;
  std::int32_t px = shortOf(from.x);
  std::int32_t pz = shortOf(from.z);
  x = shortOf(x);
  z = shortOf(z);
  const std::int32_t angle = ratan2(z - pz, x - px);
  const std::int32_t step = first ? FirstLookStep : SecondLookStep;
  for (std::int32_t looked = 0; z < pz && looked < LookLimit; ++looked) {
    px = shortOf(px + shortOf(alongX(step, angle)));
    pz = shortOf(pz + shortOf(alongZ(step, angle)));
    for (const Team& team : teams_) {
      for (const Stone& stone : team.stones) {
        if (stone.state > 0 && distance(px - stone.position.x, pz - stone.position.z) < Blocked) {
          if (first) {
            return -1;
          }
          return ratan2(stone.position.z - z, stone.position.x - x);
        }
      }
    }
  }
  return angle;
}

std::int32_t Match::aimAtStoneInRing(std::size_t team, std::int32_t ring, Point& out) const {
  for (const Stone& stone : teams_.at(team).stones) {
    if (stone.state < Pegged && stone.ring == ring) {
      const std::int32_t angle = findClearShotAngle(stone.position.x, stone.position.z);
      out = {.x = shortOf(stone.position.x), .z = shortOf(stone.position.z)};
      return angle;
    }
  }
  return -1;
}

std::int32_t Match::aimAtRandomStone(ruleset::Random& random) const {
  if (lastPlayerStone_.state < 0) {
    return ratan2(FarBack.z, FarBack.x);
  }
  std::array<const Stone*, StonesPerTeam> candidates{};
  std::int32_t count = 0;
  for (const Stone& stone : teams_.at(PlayerTeam).stones) {
    if (stone.state == Sliding) {
      candidates.at(static_cast<std::size_t>(count++)) = &stone;
    }
  }
  Point at = TargetCentre;
  if (count != 0) {
    const Stone& picked = *candidates.at(static_cast<std::size_t>(roll(random, count)));
    at = {.x = shortOf(picked.position.x), .z = shortOf(picked.position.z)};
  }
  return ratan2(at.z - OpponentMark.z, at.x - OpponentMark.x);
}

std::int32_t Match::aimBankShot(const Stone& stone, std::int32_t x, std::int32_t z) const {
  const std::int32_t angle = ratan2(stone.position.z - z, stone.position.x - x);
  std::int32_t cx = stone.position.x + alongX(StoneRadius, angle);
  const std::int32_t cz = stone.position.z + alongZ(StoneRadius, angle);
  const std::int32_t past = cx + WallMirror;
  cx = -WallMirror - past;
  return ratan2(cz - opponentHand_.z, cx - opponentHand_.x);
}

void Match::chooseOpponentShot(ruleset::Random& random) {
  Team& rival = teams_.at(OpponentTeam);
  const Team& player = teams_.at(PlayerTeam);
  const auto jittered = [&](std::int32_t angle) {
    return rival.thrown % 2 != 0 ? angle + roll(random, AimJitter)
                                 : angle - roll(random, AimJitter);
  };
  if (opponent_ == Opponent::Penguinmon) {
    std::int32_t angle = findClearShotAngle(TargetCentre.x, TargetCentre.z);
    if (angle != -1) {
      setOpponentShot(1, angle, TargetCentre.x, TargetCentre.z);
      return;
    }
    Point at;
    // Ahead, it first knocks its own stone off the left ring (ring 4 + 0x10).
    if (player.score > rival.score) {
      angle = aimAtStoneInRing(OpponentTeam, OpponentRing | 4, at);
      if (angle != -1) {
        setOpponentShot(0, angle, at.x, at.z);
        return;
      }
    }
    angle = aimAtStoneInRing(PlayerTeam, 2, at);
    if (angle != -1) {
      setOpponentShot(0, angle, at.x, at.z);
      return;
    }
    angle = findClearShotAngle(DeepLeft.x, DeepLeft.z);
    if (angle != -1) {
      setOpponentShot(1, angle, DeepCentre.x, DeepCentre.z);
      return;
    }
    angle = aimAtRandomStone(random);
    setOpponentShot(0, angle, DeepCentre.x, DeepCentre.z);
    rival.plannedPower = BankPower;
    return;
  }
  const Point hand{.x = shortOf(opponentHand_.x), .z = shortOf(opponentHand_.z)};
  const auto toward = [&](Point at) { return ratan2(at.z - hand.z, at.x - hand.x); };
  if (rival.thrown == 0) {
    std::int32_t angle = toward(LeftRing);
    if (angle == findClearShotAngle(LeftRing.x, LeftRing.z)) {
      setOpponentShot(1, angle, LeftRing.x, LeftRing.z);
      return;
    }
    angle = toward(TargetCentre);
    if (angle == findClearShotAngle(TargetCentre.x, TargetCentre.z)) {
      setOpponentShot(1, angle, TargetCentre.x, TargetCentre.z);
      return;
    }
    setOpponentShot(0, jittered(toward(TargetCentre)), TargetCentre.x, TargetCentre.z);
    return;
  }
  for (std::size_t index = 0; index < StonesPerTeam; ++index) {
    const Stone& stone = rival.stones.at(index);
    if (stone.state != Waiting) {
      continue;
    }
    if (stone.type == PeggedType) {
      rival.plannedStone = static_cast<std::int32_t>(index);
      const std::int32_t angle = toward(TargetCentre);
      if (angle == findClearShotAngle(TargetCentre.x, TargetCentre.z)) {
        setOpponentShot(1, angle, TargetCentre.x, TargetCentre.z);
      } else {
        rival.plannedStone = NoStone;
      }
      break;
    }
    rival.plannedStone = NoStone;
  }
  if (rival.plannedStone != NoStone) {
    return;
  }
  const Stone& last = lastPlayerStone_;
  if (last.type == PeggedType) {
    setOpponentShot(0, jittered(toward(TargetCentre)), TargetCentre.x, TargetCentre.z);
    return;
  }
  if (last.type < 0 || last.type > PeggedType) {
    return;
  }
  if (last.ring == MinusRing || last.ring == 0) {
    static_cast<void>(findClearShotAngle(LeftRing.x, LeftRing.z));
    setOpponentShot(1, toward(LeftRing), LeftRing.x, LeftRing.z);
  } else if (last.state < 0) {
    setOpponentShot(0, jittered(toward(TargetCentre)), TargetCentre.x, TargetCentre.z);
  } else if (roll(random, 2) != 0) {
    const std::int32_t angle = toward(LeftRing);
    if (angle == findClearShotAngle(LeftRing.x, LeftRing.z)) {
      setOpponentShot(1, angle, LeftRing.x, LeftRing.z);
    } else {
      setOpponentShot(0, toward(last.position), shortOf(last.position.x), shortOf(last.position.z));
    }
  } else {
    setOpponentShot(0, aimBankShot(last, RightRing.x, RightRing.z), RightRing.x, RightRing.z);
    rival.plannedPower = BankPower;
  }
}

} // namespace dwe::game::curling
