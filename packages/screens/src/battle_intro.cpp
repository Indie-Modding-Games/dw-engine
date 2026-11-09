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
// A banner in another language has its thirds at the same share of its pieces.
constexpr char ShapePiece = '#';
constexpr char ShapeRowEnd = '\n';
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
// BTL_handleBattleIntro keeps the banner up for FieldBannerFrames, or until the player has run back
// (the timer jumps to 198), and in any case until every piece is in place.
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

// The first corner of a bursting piece on the screen (BTL_renderBattleStartTextBurst's ft->x0,
// y0): (-4, -6) turned by its angles, from half a piece up and left of where the banner had it.
ScreenPoint burstCorner(std::int32_t x, std::int32_t y, const std::array<std::int32_t, 3>& angles) {
  constexpr auto Width = static_cast<float>(PieceWidth);
  constexpr auto Height = static_cast<float>(PieceHeight);
  const Point3 turned = rotate({.x = -Width / 2, .y = -Height / 2, .z = 0.0F}, angles);
  return project({.x = turned.x + static_cast<float>(x) - (Width / 2),
                  .y = turned.y + static_cast<float>(y) - (Height / 2),
                  .z = turned.z});
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

std::vector<BannerPlace> bannerPlacesOf(std::string_view shape) {
  std::vector<std::string_view> rows;
  for (std::size_t start = 0; start <= shape.size();) {
    const std::size_t end = std::min(shape.find(ShapeRowEnd, start), shape.size());
    rows.push_back(shape.substr(start, end - start));
    start = end + 1;
  }
  while (!rows.empty() && rows.back().find(ShapePiece) == std::string_view::npos) {
    rows.pop_back();
  }
  std::size_t columns = 0;
  for (const std::string_view row : rows) {
    columns = std::max(columns, row.size());
  }
  const auto filled = [&rows](std::size_t row, std::size_t column) {
    return column < rows.at(row).size() && rows.at(row).at(column) == ShapePiece;
  };
  // Centred as the original's columns are: every x a multiple of 8 plus 4, so no piece sits on the
  // plane's centre (which would never drift off in the burst).
  const auto left = (static_cast<std::int32_t>(columns) / 2) * -PieceWidth + (PieceWidth / 2);
  const auto top = (static_cast<std::int32_t>(rows.size()) / 2) * -PieceHeight;
  std::vector<BannerPlace> places;
  std::size_t letterStart = 0;
  for (std::size_t column = 0; column <= columns; ++column) {
    bool empty = true;
    for (std::size_t row = 0; row < rows.size() && column < columns; ++row) {
      empty = empty && !filled(row, column);
    }
    if (!empty) {
      continue;
    }
    for (std::size_t row = 0; row < rows.size(); ++row) {
      for (std::size_t inLetter = letterStart; inLetter < column; ++inLetter) {
        if (filled(row, inLetter)) {
          places.push_back({.x = left + (static_cast<std::int32_t>(inLetter) * PieceWidth),
                            .y = top + (static_cast<std::int32_t>(row) * PieceHeight)});
        }
      }
    }
    letterStart = column + 1;
  }
  if (places.size() > BannerPieceSlots) {
    places.clear();
  }
  return places;
}

BattleIntro::BattleIntro(std::uint32_t seed, std::vector<BannerPlace> places)
    : random_(seed), places_{std::move(places)} {
  if (places_.empty()) {
    for (const auto& [x, y] : PiecePlaces) {
      places_.push_back({.x = x, .y = y});
    }
  }
  pieces_.resize(places_.size());
}

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

// battle_hud.c: BTL_initializeBattleStartText. Every piece first takes its trail and a texture,
// the order list is shuffled, then each piece takes a side and a speed and starts far off that side
// on its own row: the rolls go in that order.
void BattleIntro::startBanner() {
  step_ = IntroStep::Banner;
  frame_ = 0;
  finished_ = false;
  for (std::size_t index = 0; index < pieces_.size(); ++index) {
    Piece& piece = pieces_.at(index);
    piece.order = static_cast<std::int32_t>(index);
    piece.trail = TrailLength;
    piece.texture = static_cast<std::uint8_t>(randomLimit(Textures));
  }
  shufflePieces();
  for (std::size_t index = 0; index < pieces_.size(); ++index) {
    Piece& piece = pieces_.at(index);
    const BannerPlace& place = places_.at(index);
    const std::int32_t side = randomLimit(2) == 1 ? 1 : -1;
    piece.y = place.y;
    piece.targetX = place.x;
    piece.speed = -side * ((randomLimit(Textures) + 1) * SpeedStep);
    // The original's thirds of its 155 pieces, at the same share of another banner's.
    const std::size_t scaled = index * BannerPieceCount / pieces_.size();
    const auto third = static_cast<std::size_t>(
        std::ranges::count_if(ThirdsFrom, [scaled](std::size_t from) { return scaled >= from; }));
    piece.x = (side * StartDistances.at(third)) + randomLimit(StartSpread) - (StartSpread / 2);
    const std::int32_t drift = randomLimit(DriftSpread) + DriftEighths;
    piece.driftX = drift * place.x / DriftEighths;
    piece.driftY = drift * place.y / DriftEighths;
    piece.angles = {};
  }
}

// battle_hud.c: BTL_shuffleBattleStartTextPieces: each piece swaps its order with a random one,
// one randomLimit over the whole banner each. Nothing reads the order afterwards (the pieces are
// drawn and flown in table order), so what it leaves is its rolls.
void BattleIntro::shufflePieces() {
  const auto count = static_cast<std::int32_t>(pieces_.size());
  for (Piece& piece : pieces_) {
    std::swap(piece.order, pieces_.at(static_cast<std::size_t>(randomLimit(count))).order);
  }
}

// battle_hud.c: BTL_initializeBattleStartTextBurst: the burst starts and the banner counts as not
// finished until every piece is off.
void BattleIntro::startBurst() {
  step_ = IntroStep::Burst;
  flash_ = 0;
  finished_ = false;
}

void BattleIntro::skipAnger() {
  if (step_ == IntroStep::Anger) {
    startBanner();
  }
}

void BattleIntro::playerBack() {
  if (step_ == IntroStep::Banner) {
    frame_ = PlayerBackFrame;
  }
}

// BTL_renderBattleStartText: the pieces fly to their places, then their trails shrink; a frame
// that starts with every trail gone flashes and raises BTL_BATTLE_TEXT_FINISHED.
// BTL_handleBattleIntro reads that flag before the frame's drawing and checks it after: the banner
// bursts two frames after its pieces are in, once its 200 frames are up and the NPCs that run off
// are gone.
void BattleIntro::stepBanner() {
  const bool done = finished_;
  ++frame_;
  if (gathered_) {
    flash_ = (flashFrames_++ % FlashFrames) / FlashStep;
    finished_ = true;
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
  gathered_ = std::ranges::all_of(pieces_, [](const Piece& piece) { return piece.trail == 0; });
  if (done && frame_ >= bannerFrames_ && !burstHeld_) {
    startBurst();
  }
}

// BTL_renderBattleStartTextBurst: every piece turns and drifts away until it leaves the screen. A
// piece stops once the first corner it is drawn with this frame (turned by its angles before this
// frame's turn) is off a 360 x 280 box round the screen's centre; the burst is finished
// (BTL_BATTLE_TEXT_FINISHED) on the first frame with every piece stopped.
void BattleIntro::stepBurst() {
  bool anyLeft = false;
  for (Piece& piece : pieces_) {
    if (piece.driftX == 0 && piece.driftY == 0) {
      continue;
    }
    anyLeft = true;
    const ScreenPoint corner = burstCorner(piece.x, piece.y, piece.angles);
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
    // The GTE's screen coordinates are whole pixels.
    const float x = std::floor(corner.x - ScreenCentreX);
    const float y = std::floor(corner.y - ScreenCentreY);
    if (x < -BurstBoundX || x > BurstBoundX || y < -BurstBoundY || y > BurstBoundY) {
      piece.driftX = 0;
      piece.driftY = 0;
    }
    piece.x += piece.driftX;
    piece.y += piece.driftY;
  }
  if (!anyLeft) {
    finished_ = true;
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
