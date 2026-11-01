#include <dwe/screens/battle_intro.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <numbers>
#include <string>
#include <utility>

namespace dwe::screens {
namespace {

// Where each piece of the banner ends on its plane (battle_effect.c:
// BTL_BATTLE_START_TEXT_POSITIONS): the top-left corner of an 8 x 12 cell.
constexpr std::array<std::array<std::int32_t, 2>, BannerPieceCount> PiecePlaces{{
    {-172, -48}, {-164, -48}, {-156, -48}, {-148, -48}, {-140, -48}, {-172, -36}, {-164, -36},
    {-140, -36}, {-132, -36}, {-172, -24}, {-164, -24}, {-140, -24}, {-132, -24}, {-172, -12},
    {-164, -12}, {-156, -12}, {-148, -12}, {-140, -12}, {-172, 0},   {-164, 0},   {-140, 0},
    {-132, 0},   {-172, 12},  {-164, 12},  {-140, 12},  {-132, 12},  {-172, 24},  {-164, 24},
    {-140, 24},  {-132, 24},  {-172, 36},  {-164, 36},  {-156, 36},  {-148, 36},  {-140, 36},
    {-92, -48},  {-84, -48},  {-92, -36},  {-84, -36},  {-100, -24}, {-92, -24},  {-84, -24},
    {-76, -24},  {-108, -12}, {-100, -12}, {-76, -12},  {-68, -12},  {-108, 0},   {-100, 0},
    {-76, 0},    {-68, 0},    {-116, 12},  {-108, 12},  {-100, 12},  {-92, 12},   {-84, 12},
    {-76, 12},   {-68, 12},   {-60, 12},   {-116, 24},  {-108, 24},  {-68, 24},   {-60, 24},
    {-116, 36},  {-108, 36},  {-68, 36},   {-60, 36},   {-44, -48},  {-36, -48},  {-28, -48},
    {-20, -48},  {-12, -48},  {-4, -48},   {-28, -36},  {-20, -36},  {-28, -24},  {-20, -24},
    {-28, -12},  {-20, -12},  {-28, 0},    {-20, 0},    {-28, 12},   {-20, 12},   {-28, 24},
    {-20, 24},   {-28, 36},   {-20, 36},   {20, -48},   {28, -48},   {36, -48},   {44, -48},
    {52, -48},   {60, -48},   {36, -36},   {44, -36},   {36, -24},   {44, -24},   {36, -12},
    {44, -12},   {36, 0},     {44, 0},     {36, 12},    {44, 12},    {36, 24},    {44, 24},
    {36, 36},    {44, 36},    {76, -48},   {84, -48},   {76, -36},   {84, -36},   {76, -24},
    {84, -24},   {76, -12},   {84, -12},   {76, 0},     {84, 0},     {76, 12},    {84, 12},
    {76, 24},    {84, 24},    {76, 36},    {84, 36},    {92, 36},    {100, 36},   {108, 36},
    {116, 36},   {132, -48},  {140, -48},  {148, -48},  {156, -48},  {164, -48},  {172, -48},
    {132, -36},  {140, -36},  {132, -24},  {140, -24},  {132, -12},  {140, -12},  {148, -12},
    {156, -12},  {164, -12},  {172, -12},  {132, 0},    {140, 0},    {132, 12},   {140, 12},
    {132, 24},   {140, 24},   {132, 36},   {140, 36},   {148, 36},   {156, 36},   {164, 36},
    {172, 36},
}};

constexpr std::int32_t PieceWidth = 8;
constexpr std::int32_t PieceHeight = 12;
// BTL_initializeBattleStartText: each trail starts 24 long and, once its piece is in place, shrinks
// by 4 a frame.
constexpr std::int32_t TrailLength = 24;
constexpr std::int32_t TrailShrink = 4;
// The pieces start 500, 600 or 700 to one side by thirds of the table, give or take 50, and fly at
// 32, 64 or 96 a frame.
constexpr std::array<std::int32_t, 3> StartDistances{500, 600, 700};
constexpr std::array<std::size_t, 2> ThirdsFrom{0x33, 0x65};
constexpr std::int32_t StartSpread = 100;
constexpr std::int32_t SpeedStep = 32;
constexpr std::int32_t Textures = 3;
// BTL_renderBattleStartTextBurst: each piece drifts away at 8/8 to 12/8 of its place a frame and
// turns 0x100 (of 0x1000) a frame on two of its axes, which its texture picks.
constexpr std::int32_t DriftEighths = 8;
constexpr std::int32_t DriftSpread = 5;
constexpr std::int32_t TurnStep = 0x100;
constexpr float FullTurn = 4096.0F;
// A piece whose corner leaves these bounds, from the screen's centre, stops and counts as gone.
constexpr float BurstBoundX = 180.0F;
constexpr float BurstBoundY = 140.0F;
// The flash cycles three palettes, two frames each.
constexpr std::int32_t FlashFrames = 6;
constexpr std::int32_t FlashStep = 2;

// The banner's plane (battle_effect.c: BTL_BATTLE_START_TEXT_MATRIX, in 4096ths) and the projection
// it is drawn with (GsSetProjection(0x200)), about the screen's centre.
constexpr float MatrixOne = 4096.0F;
constexpr std::array<std::array<float, 3>, 3> PlaneMatrix{{
    {0x100a / MatrixOne, 0.0F, 0.0F},
    {0.0F, 0x08e4 / MatrixOne, -0x0d67 / MatrixOne},
    {0.0F, 0x0d5e / MatrixOne, 0x08e9 / MatrixOne},
}};
constexpr std::array<float, 3> PlaneOffset{0.0F, -2.0F, 724.0F};
constexpr float Projection = 512.0F;
constexpr float ScreenCentreX = 160.0F;
constexpr float ScreenCentreY = 120.0F;

// btl.c: renderFleeBubble: the mark grows and rises for 5 frames from 200 over the enemy's head,
// 10 more a frame, then stands at 250 and pulses through FLEE_BUBBLE_SCALE.
constexpr std::int32_t AngerGrowFrames = 5;
constexpr float AngerBaseLift = 200.0F;
constexpr float AngerRisePerFrame = 10.0F;
constexpr float AngerTopLift = 250.0F;
constexpr std::array<std::int32_t, 10> AngerPulse{100, 104, 108, 112, 116, 120, 116, 112, 108, 104};
constexpr std::int32_t FullPercent = 100;
// The original shows the marks while it loads the battle from the disc (loadBattleData) and takes
// them away once its effects are in memory, which here is at once: they show for a second.
constexpr std::int32_t AngerFrames = 20;
// BTL_handleBattleIntro keeps the banner up for 200 frames, or until the player has run back (the
// timer jumps to 198), and in any case until every piece is in place.
constexpr std::int32_t BannerFrames = 200;
constexpr std::int32_t PlayerBackFrame = 198;

constexpr std::string_view StepAnger = "anger";
constexpr std::string_view StepBanner = "banner";
constexpr std::string_view StepBurst = "burst";
constexpr std::string_view XField = "x";
constexpr std::string_view YField = "y";
constexpr std::string_view ShownField = "shown";
constexpr double Percent = 100.0;
constexpr double Tenths = 10.0;

struct Point3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct ScreenPoint {
  float x = 0.0F;
  float y = 0.0F;
};

// A point of the banner's plane on the screen, as the GTE puts it (rtps).
ScreenPoint project(Point3 point) {
  const std::array<float, 3> in{point.x, point.y, point.z};
  std::array<float, 3> out = PlaneOffset;
  for (std::size_t row = 0; row < out.size(); ++row) {
    for (std::size_t column = 0; column < in.size(); ++column) {
      out.at(row) += PlaneMatrix.at(row).at(column) * in.at(column);
    }
  }
  const float depth = std::max(out[2], 1.0F);
  return {.x = ScreenCentreX + (Projection * out[0] / depth),
          .y = ScreenCentreY + (Projection * out[1] / depth)};
}

// The rectangle round the screen points of four corners.
BannerPiece boundsOf(const std::array<ScreenPoint, 4>& corners) {
  auto [minX, maxX] = std::ranges::minmax(corners, {}, &ScreenPoint::x);
  auto [minY, maxY] = std::ranges::minmax(corners, {}, &ScreenPoint::y);
  return {.x = minX.x,
          .y = minY.y,
          .width = maxX.x - minX.x,
          .height = maxY.y - minY.y,
          .texture = 0,
          .trail = 0.0F,
          .trailLeft = false};
}

// RotMatrix: the turn of a bursting piece, about x, then y, then z.
Point3 rotate(Point3 point, const std::array<std::int32_t, 3>& angles) {
  const auto radians = [](std::int32_t angle) {
    return static_cast<float>(angle) / FullTurn * 2.0F * std::numbers::pi_v<float>;
  };
  const float ax = radians(angles[0]);
  const float ay = radians(angles[1]);
  const float az = radians(angles[2]);
  Point3 out = point;
  out = {.x = out.x,
         .y = (out.y * std::cos(ax)) - (out.z * std::sin(ax)),
         .z = (out.y * std::sin(ax)) + (out.z * std::cos(ax))};
  out = {.x = (out.x * std::cos(ay)) + (out.z * std::sin(ay)),
         .y = out.y,
         .z = (-out.x * std::sin(ay)) + (out.z * std::cos(ay))};
  out = {.x = (out.x * std::cos(az)) - (out.y * std::sin(az)),
         .y = (out.x * std::sin(az)) + (out.y * std::cos(az)),
         .z = out.z};
  return out;
}

double rounded(double value) {
  return std::round(value * Tenths) / Tenths;
}

std::string_view stepName(IntroStep step) {
  switch (step) {
  case IntroStep::Anger:
    return StepAnger;
  case IntroStep::Banner:
    return StepBanner;
  case IntroStep::Burst:
    return StepBurst;
  case IntroStep::Done:
    break;
  }
  return {};
}

std::string packed(const std::vector<BannerPiece>& pieces) {
  std::string text;
  for (const BannerPiece& piece : pieces) {
    std::format_to(std::back_inserter(text), "{:.1f} {:.1f} {:.1f} {:.1f} {} {:.1f} {} ", piece.x,
                   piece.y, piece.width, piece.height, piece.texture, piece.trail,
                   piece.trailLeft ? 1 : 0);
  }
  return text;
}

} // namespace

BattleIntro::BattleIntro(std::uint32_t seed) : random_(seed) {}

std::int32_t BattleIntro::randomLimit(std::int32_t limit) {
  return std::uniform_int_distribution<std::int32_t>{0, limit - 1}(random_);
}

void BattleIntro::step() {
  switch (step_) {
  case IntroStep::Anger:
    if (++frame_ >= AngerFrames) {
      startBanner();
    }
    return;
  case IntroStep::Banner:
    stepBanner();
    return;
  case IntroStep::Burst:
    stepBurst();
    return;
  case IntroStep::Done:
    return;
  }
}

// battle_hud.c: BTL_initializeBattleStartText. Each piece takes a texture, a side and a speed, and
// starts far off that side on its own row.
void BattleIntro::startBanner() {
  step_ = IntroStep::Banner;
  frame_ = 0;
  for (std::size_t index = 0; index < pieces_.size(); ++index) {
    Piece& piece = pieces_.at(index);
    const auto& place = PiecePlaces.at(index);
    piece.trail = TrailLength;
    piece.texture = static_cast<std::uint8_t>(randomLimit(Textures));
    const std::int32_t side = randomLimit(2) == 1 ? 1 : -1;
    piece.y = place[1];
    piece.targetX = place[0];
    piece.speed = -side * ((randomLimit(Textures) + 1) * SpeedStep);
    const auto third = static_cast<std::size_t>(
        std::ranges::count_if(ThirdsFrom, [index](std::size_t from) { return index >= from; }));
    piece.x = (side * StartDistances.at(third)) + randomLimit(StartSpread) - (StartSpread / 2);
    const std::int32_t drift = randomLimit(DriftSpread) + DriftEighths;
    piece.driftX = drift * place[0] / DriftEighths;
    piece.driftY = drift * place[1] / DriftEighths;
    piece.angles = {};
  }
}

void BattleIntro::playerBack() {
  if (step_ == IntroStep::Banner) {
    frame_ = PlayerBackFrame;
  }
}

// BTL_renderBattleStartText: the pieces fly to their places, then their trails shrink; once all
// are in, the banner flashes. BTL_handleBattleIntro then lets it burst.
void BattleIntro::stepBanner() {
  ++frame_;
  if (gathered_) {
    flash_ = (flashFrames_++ % FlashFrames) / FlashStep;
  }
  for (Piece& piece : pieces_) {
    if (piece.x != piece.targetX) {
      piece.x += piece.speed;
      piece.x =
          piece.speed > 0 ? std::min(piece.x, piece.targetX) : std::max(piece.x, piece.targetX);
    } else if (piece.trail != 0) {
      piece.trail -= TrailShrink;
    }
  }
  const bool wasGathered = gathered_;
  gathered_ = std::ranges::all_of(pieces_, [](const Piece& piece) { return piece.trail == 0; });
  if (wasGathered && frame_ >= BannerFrames && !burstHeld_) {
    step_ = IntroStep::Burst;
    flash_ = 0;
  }
}

// BTL_renderBattleStartTextBurst: every piece turns and drifts away until it leaves the screen.
void BattleIntro::stepBurst() {
  bool anyLeft = false;
  for (Piece& piece : pieces_) {
    if (piece.driftX == 0 && piece.driftY == 0) {
      continue;
    }
    anyLeft = true;
    switch (piece.texture) {
    case 0:
      piece.angles[0] += TurnStep;
      piece.angles[1] += TurnStep;
      break;
    case 1:
      piece.angles[1] += TurnStep;
      piece.angles[2] += TurnStep;
      break;
    default:
      piece.angles[0] += TurnStep;
      piece.angles[2] += TurnStep;
      break;
    }
    const ScreenPoint corner = project(
        Point3{.x = static_cast<float>(piece.x), .y = static_cast<float>(piece.y), .z = 0.0F});
    if (std::abs(corner.x - ScreenCentreX) > BurstBoundX ||
        std::abs(corner.y - ScreenCentreY) > BurstBoundY) {
      piece.driftX = 0;
      piece.driftY = 0;
    }
    piece.x += piece.driftX;
    piece.y += piece.driftY;
  }
  if (!anyLeft) {
    step_ = IntroStep::Done;
  }
}

float BattleIntro::angerLift() const {
  if (frame_ < AngerGrowFrames) {
    return AngerBaseLift + (static_cast<float>(frame_ + 1) * AngerRisePerFrame);
  }
  return AngerTopLift;
}

std::int32_t BattleIntro::angerScale() const {
  if (frame_ < AngerGrowFrames) {
    return (frame_ + 1) * FullPercent / AngerGrowFrames;
  }
  return AngerPulse.at(static_cast<std::size_t>(frame_ - AngerGrowFrames) % AngerPulse.size());
}

BannerPiece BattleIntro::shownPiece(const Piece& piece) const {
  const auto x = static_cast<float>(piece.x);
  const auto y = static_cast<float>(piece.y);
  constexpr auto Width = static_cast<float>(PieceWidth);
  constexpr auto Height = static_cast<float>(PieceHeight);
  if (step_ == IntroStep::Burst) {
    // The burst turns each piece about its centre, which it puts half a piece up and left of
    // where the banner had it.
    std::array<ScreenPoint, 4> corners{};
    const std::array<Point3, 4> offsets{{{.x = -Width / 2, .y = -Height / 2, .z = 0.0F},
                                         {.x = Width / 2, .y = -Height / 2, .z = 0.0F},
                                         {.x = -Width / 2, .y = Height / 2, .z = 0.0F},
                                         {.x = Width / 2, .y = Height / 2, .z = 0.0F}}};
    for (std::size_t corner = 0; corner < corners.size(); ++corner) {
      const Point3 turned = rotate(offsets.at(corner), piece.angles);
      corners.at(corner) = project(
          {.x = turned.x + x - (Width / 2), .y = turned.y + y - (Height / 2), .z = turned.z});
    }
    BannerPiece shown = boundsOf(corners);
    shown.texture = piece.texture;
    return shown;
  }
  BannerPiece shown =
      boundsOf({project({.x = x, .y = y, .z = 0.0F}), project({.x = x + Width, .y = y, .z = 0.0F}),
                project({.x = x, .y = y + Height, .z = 0.0F}),
                project({.x = x + Width, .y = y + Height, .z = 0.0F})});
  shown.texture = piece.texture;
  if (!gathered_ && piece.trail > 0) {
    // The trail runs behind the piece, on the side it comes from.
    const auto trail = static_cast<float>(piece.trail);
    const bool left = piece.speed > 0;
    const float from = left ? x - trail : x + Width;
    const BannerPiece band = boundsOf({project({.x = from, .y = y, .z = 0.0F}),
                                       project({.x = from + trail, .y = y, .z = 0.0F}),
                                       project({.x = from, .y = y + Height, .z = 0.0F}),
                                       project({.x = from + trail, .y = y + Height, .z = 0.0F})});
    shown.trail = band.width;
    shown.trailLeft = left;
  }
  return shown;
}

std::vector<BannerPiece> BattleIntro::pieces() const {
  std::vector<BannerPiece> shown;
  if (step_ != IntroStep::Banner && step_ != IntroStep::Burst) {
    return shown;
  }
  for (const Piece& piece : pieces_) {
    if (step_ == IntroStep::Burst && piece.driftX == 0 && piece.driftY == 0) {
      continue;
    }
    shown.push_back(shownPiece(piece));
  }
  return shown;
}

BattleIntroView battleIntroViewOf(const BattleIntro& intro, std::vector<AngerAnchor> anger,
                                  std::vector<AngerAnchor> flee) {
  const bool angry = intro.current() == IntroStep::Anger;
  if (!angry) {
    anger.assign(anger.size(), std::nullopt);
    flee.assign(flee.size(), std::nullopt);
  }
  return {.step = intro.current(),
          .anger = std::move(anger),
          .flee = std::move(flee),
          .angerScale = angry ? intro.angerScale() : 0,
          .flash = intro.flash(),
          .pieces = intro.pieces()};
}

ScreenSettings settingsOf(const BattleIntroView& view, std::size_t enemies) {
  ScreenSettings settings{
      {.name = std::string{IntroStepSetting}, .value = std::string{stepName(view.step)}},
      {.name = std::string{IntroAngerScaleSetting}, .value = static_cast<double>(view.angerScale)},
      {.name = std::string{IntroFlashSetting}, .value = static_cast<double>(view.flash)},
      {.name = std::string{IntroPiecesSetting}, .value = packed(view.pieces)},
  };
  for (std::size_t index = 0; index < enemies; ++index) {
    const AngerAnchor anchor = index < view.anger.size() ? view.anger[index] : std::nullopt;
    settings.push_back(
        {.name = listEntryName(IntroAngerList, index, ShownField), .value = anchor.has_value()});
    settings.push_back({.name = listEntryName(IntroAngerList, index, XField),
                        .value = anchor ? rounded(anchor->x * Percent) : 0.0});
    settings.push_back({.name = listEntryName(IntroAngerList, index, YField),
                        .value = anchor ? rounded(anchor->y * Percent) : 0.0});
  }
  for (std::size_t index = 0; index < FleeMarkSlots; ++index) {
    const AngerAnchor anchor = index < view.flee.size() ? view.flee[index] : std::nullopt;
    settings.push_back(
        {.name = listEntryName(IntroFleeList, index, ShownField), .value = anchor.has_value()});
    settings.push_back({.name = listEntryName(IntroFleeList, index, XField),
                        .value = anchor ? rounded(anchor->x * Percent) : 0.0});
    settings.push_back({.name = listEntryName(IntroFleeList, index, YField),
                        .value = anchor ? rounded(anchor->y * Percent) : 0.0});
  }
  return settings;
}

} // namespace dwe::screens
