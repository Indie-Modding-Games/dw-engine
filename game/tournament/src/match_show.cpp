#include <dwe/core/original_math.hpp>
#include <dwe/game/tournament/match_show.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dwe::game::tournament {

namespace {

using core::original::atan4096;
using core::original::rcos;
using core::original::rsin;

constexpr std::int32_t One = 4096;
constexpr std::int32_t AngleMask = 0xfff;

// effect_common.c: lerp, with its wrap-around past the ends.
std::int32_t lerp(std::int32_t start, std::int32_t end, std::int32_t t0, std::int32_t t1,
                  std::int32_t t) {
  if (t1 == t0) {
    return 0;
  }
  const std::int32_t range = end - start;
  auto result = static_cast<std::int32_t>(static_cast<std::int64_t>(range) * (t - t0) / (t1 - t0));
  if (range >= 0) {
    result %= range + 1;
    if (result < 0) {
      result += range;
    }
  } else {
    result %= range - 1;
    if (result > 0) {
      result += range;
    }
  }
  return result + start;
}

// STD_interpolateClamped2: from start to end as t goes from lo to hi, held at the ends.
std::int32_t interpolateClamped(std::int32_t lo, std::int32_t hi, std::int32_t t,
                                std::int32_t start, std::int32_t end) {
  if (hi < lo) {
    std::swap(lo, hi);
  }
  t = std::clamp(t, lo, hi);
  if (lo >= t) {
    return start;
  }
  return start + ((end - start) * (t - lo) / (hi - lo));
}

// vecmath.c: getDistance on the ground (SquareRoot0, rounded down).
std::int32_t groundDistance(std::int64_t dx, std::int64_t dz) {
  auto root = static_cast<std::int64_t>(std::sqrt(static_cast<double>((dx * dx) + (dz * dz))));
  while (root * root > (dx * dx) + (dz * dz)) {
    --root;
  }
  while ((root + 1) * (root + 1) <= (dx * dx) + (dz * dz)) {
    ++root;
  }
  return static_cast<std::int32_t>(root);
}

// STD_getFighterDistance: how far `at` has come from `from` toward `to`, in hundredths.
std::int32_t progress(ArenaPoint from, ArenaPoint to, ArenaPoint at) {
  const std::int32_t whole = groundDistance(to.x - from.x, to.z - from.z);
  if (whole == 0) {
    return 100;
  }
  return groundDistance(from.x - at.x, from.z - at.z) * 100 / whole;
}

// RotMatrixZYX(pitch, yaw, 0) applied to (0, 0, distance) (ApplyMatrixLV).
ShowPoint turned(std::int32_t pitch, std::int32_t yaw, std::int32_t distance) {
  const std::int64_t sx = rsin(pitch);
  const std::int64_t cx = rcos(pitch);
  const std::int64_t sy = rsin(yaw);
  const std::int64_t cy = rcos(yaw);
  const std::int64_t m02 = (sy * cx) / One;
  const std::int64_t m12 = -sx;
  const std::int64_t m22 = (cy * cx) / One;
  return {static_cast<std::int32_t>((m02 * distance) / One),
          static_cast<std::int32_t>((m12 * distance) / One),
          static_cast<std::int32_t>((m22 * distance) / One)};
}

ShowPoint plus(ShowPoint a, ShowPoint b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

ShowBones defaultBones(ArenaPoint at, ShowBody body) {
  return {.root = {at.x, -body.height / 2, at.z}, .node1Y = -body.height / 2};
}

// STD_setViewpointFromBone with no offset: the camera hangs on node 0 at the height of node 1 and
// stands `distance` away along the angles; the projection is 0x15e.
ShowView viewFromBone(const ShowBones& bones, std::int32_t pitch, std::int32_t yaw,
                      std::int32_t distance) {
  ShowView view;
  view.target = {bones.root[0], bones.node1Y, bones.root[2]};
  view.eye = plus(view.target, turned(pitch, yaw, distance));
  view.projection = 0x15e;
  return view;
}

// The chase's distance: ((height + radius) / 2) * scale / 450.
std::int32_t chaseDistance(ShowBody body, std::int32_t scale) {
  return ((body.height + body.radius) / 2) * scale / 450;
}

// effect_common.c: processSomeArenaArrays with its slopes all 0 (initializeSomeArenaArrays): the
// value between the two keys around t, the step per key rounded first.
std::int32_t keyed(const std::array<std::int32_t, 22>& values, std::int32_t t) {
  // std_main.c: STD_D_8007A41C, the keys (frames) of the end camera; the last two are unused.
  static constexpr std::array<std::int32_t, 22> Keys{-1,  0,   30,  45,  60,  80,  100, 120,
                                                     140, 160, 180, 200, 220, 240, 260, 280,
                                                     300, 320, 340, 360, 360, 360};
  std::size_t lo = 0;
  std::size_t hi = Keys.size() - 1;
  while (lo < hi) {
    const std::size_t mid = (lo + hi) / 2;
    if (Keys.at(mid) < t) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo > 0) {
    --lo;
  }
  lo = std::min(lo, Keys.size() - 2);
  const std::int32_t dx = Keys.at(lo + 1) - Keys.at(lo);
  if (dx == 0) {
    return values.at(lo);
  }
  const std::int32_t dt = t - Keys.at(lo);
  return values.at(lo) + (dt * ((values.at(lo + 1) - values.at(lo)) / dx));
}

std::int32_t randomBetween(ruleset::Random& random, std::int32_t low, std::int32_t high) {
  if (high == low) {
    return low;
  }
  if (high < low) {
    std::swap(low, high);
  }
  return low + random.below(high - low + 1);
}

} // namespace

// --- STD_runIntro ---

MatchIntro::MatchIntro(IntroFighter partner, IntroFighter rival)
    : fighters_{Fighter{.desc = std::move(partner), .target = {}},
                Fighter{.desc = std::move(rival), .target = {}}} {
  // STD_placePlayer1, STD_placePlayer2: both stand at their starts.
  for (std::size_t i = 0; i < fighters_.size(); ++i) {
    const IntroFighter& desc = fighters_.at(i).desc;
    figures_.at(i) = {
        .position = desc.start, .yaw = desc.yaw, .clip = IntroStanceClip, .starts = 0};
    fighters_.at(i).target = {.x = desc.start.x - desc.walkIn, .z = desc.start.z};
  }
  startTurn();
}

void MatchIntro::play(std::size_t fighter, std::uint32_t clip) {
  Figure& figure = figures_.at(fighter);
  figure.clip = clip;
  ++figure.starts;
  clipFrames_.at(fighter) = 0;
}

bool MatchIntro::clipEnded(std::size_t fighter) const {
  const Figure& figure = figures_.at(fighter);
  const auto found = fighters_.at(fighter).desc.clips.find(static_cast<std::int32_t>(figure.clip));
  if (found == fighters_.at(fighter).desc.clips.end()) {
    // A pose without its clip's length ends at once (the port's fallback).
    return figure.clip != IntroStanceClip && figure.clip != IntroRunClip;
  }
  return found->second.ended(clipFrames_.at(fighter));
}

void MatchIntro::stepClip(std::size_t fighter) {
  Figure& figure = figures_.at(fighter);
  const Fighter& who = fighters_.at(fighter);
  const auto found = who.desc.clips.find(static_cast<std::int32_t>(figure.clip));
  std::int32_t forward = 0;
  if (found != who.desc.clips.end()) {
    if (const auto step = found->second.stepAt(clipFrames_.at(fighter))) {
      forward = step->forward;
    }
  } else if (figure.clip == IntroRunClip) {
    forward = RunFallbackStep;
  }
  ++clipFrames_.at(fighter);
  if (forward == 0) {
    return;
  }
  // anim.c: applyRootMomentum along its facing, which is toward where its run ends.
  const std::int32_t dx = who.target.x - figure.position.x;
  const std::int32_t dz = who.target.z - figure.position.z;
  const std::int32_t left = groundDistance(dx, dz);
  if (left == 0) {
    return;
  }
  figure.position.x += static_cast<std::int32_t>(static_cast<std::int64_t>(dx) * forward / left);
  figure.position.z += static_cast<std::int32_t>(static_cast<std::int64_t>(dz) * forward / left);
}

ShowBones MatchIntro::bonesOf(std::size_t fighter, const std::optional<ShowBones>& bones) const {
  return bones.value_or(
      defaultBones(figures_.at(fighter).position, fighters_.at(fighter).desc.body));
}

void MatchIntro::startTurn() {
  const std::size_t f = turn_;
  const IntroFighter& desc = fighters_.at(f).desc;
  // STD_startCameraChase(entity, dist, side): the stance, the camera beside it on its side.
  chaseFrom_ = figures_.at(f).position;
  play(f, IntroStanceClip);
  chaseTimer_ = 0;
  chasePhase_ = 0;
  const std::int32_t yaw = (f == 0 ? -0x638 : 0x638) + desc.yaw;
  const std::int32_t height = -(desc.body.height * 2) / 3;
  view_ = viewFromBone(defaultBones(figures_.at(f).position, desc.body), 0, yaw,
                       chaseDistance(desc.body, 0x5aa));
  view_.eye[1] = std::min((height * 3) / 10, -0xb4);
  // STD_addIntroText: the name from 4 frames on, its row at y 68.
  nameFrames_ = 4;
  nameShown_ = 0;
  nameY_ = 68;
  stats_.reset();
  music_ = static_cast<std::int32_t>(f);
}

void MatchIntro::endTurn() {
  // STD_removeIntroText, STD_removeIntroStats.
  stats_.reset();
  counting_ = false;
  ++turn_;
  if (turn_ < fighters_.size()) {
    startTurn();
  }
}

void MatchIntro::tickName() {
  const IntroFighter& desc = fighters_.at(turn_).desc;
  // STD_tickIntroName.
  ++nameFrames_;
  if (desc.nameLength == nameShown_ && nameStep_ == 3) {
    if (chaseTimer_ == 0) {
      play(turn_, IntroRunClip);
      chaseTimer_ = 20;
    }
    if (nameY_ >= -71) {
      nameY_ -= 28;
    } else if (!stats_) {
      // STD_addIntroStats: HP and MP from -100, the others from -10.
      stats_ = std::array<std::int32_t, 6>{-100, -100, -10, -10, -10, -10};
    }
  }
  // STD_renderIntroName: a letter every 4 frames, each growing in over 3.
  if (nameFrames_ % 4 == 0) {
    if (nameShown_ < desc.nameLength) {
      ++nameShown_;
      nameStep_ = 0;
    }
  } else if (nameStep_ != 3) {
    ++nameStep_;
  }
}

void MatchIntro::tickStats() {
  counting_ = false;
  if (!stats_) {
    return;
  }
  // STD_tickIntroStats: 200 a frame for HP and MP, 20 for the others, up to the fighter's.
  const std::array<std::int32_t, 6>& real = fighters_.at(turn_).desc.stats;
  for (std::size_t i = 0; i < stats_->size(); ++i) {
    stats_->at(i) = std::min(stats_->at(i) + (i < 2 ? 200 : 20), real.at(i));
  }
  counting_ = !statsCounted();
}

bool MatchIntro::statsCounted() const {
  return stats_ && *stats_ == fighters_.at(std::min<std::size_t>(turn_, 1)).desc.stats;
}

void MatchIntro::tickChase(const std::optional<ShowBones>& bones) {
  // STD_tickCameraChase: nothing until the name is in (its timer 20), nothing once over (-1).
  if (chaseTimer_ < 0x14) {
    return;
  }
  const std::size_t f = turn_;
  const Fighter& who = fighters_.at(f);
  Figure& figure = figures_.at(f);
  const ShowBody body = who.desc.body;
  const bool side = f != 0;
  if (chaseTimer_ == 0x14) {
    play(f, IntroRunClip);
  }
  if (chasePhase_ == 0) {
    const std::int32_t done = progress(chaseFrom_, who.target, figure.position);
    if (done >= 0x23) {
      chaseFrom_ = figure.position;
      chasePhase_ = 1;
    } else {
      std::int32_t yaw =
          side ? lerp(0x638, 0x293, 0, 0x23, done) : lerp(-0x638, -0x293, 0, 0x23, done);
      yaw += figure.yaw;
      const std::int32_t height = -(body.height * 7) / 10;
      view_ = viewFromBone(bonesOf(f, bones), 0, yaw, chaseDistance(body, 0x5aa));
      view_.eye[1] = std::min((height * 7) / 10, -0xb4);
      ++chaseTimer_;
      return;
    }
  }
  // STD_updateCameraLerp: up and round to the fighter's front as it closes in.
  const std::int32_t t = progress(chaseFrom_, who.target, figure.position);
  const std::int32_t base = chaseDistance(body, 0x62c);
  const std::int32_t pitch = lerp(0x2aa, 0xe3, 0, 0x64, t);
  std::int32_t yaw = lerp(0x5c7, 0xa38, 0, 0x64, t);
  const std::int32_t distance = yaw < 0x801 ? lerp(base, base * 2, 0x5c7, 0x800, yaw)
                                            : lerp(base * 2, base, 0x800, 0xa38, yaw);
  if (side) {
    yaw = -yaw;
  }
  yaw += figure.yaw;
  view_ = viewFromBone(bonesOf(f, bones), pitch, yaw, distance);
  // STD_isPositionNearEntity: within 50 of where the run ends it strikes its pose.
  if (std::abs(figure.position.x - who.target.x) <= 50 &&
      std::abs(figure.position.z - who.target.z) <= 50) {
    if (who.desc.poseClip) {
      play(f, *who.desc.poseClip);
    } else {
      play(f, IntroStanceClip);
    }
    chaseTimer_ = -1;
    return;
  }
  ++chaseTimer_;
}

void MatchIntro::tick(bool confirm, const std::array<std::optional<ShowBones>, 2>& bones) {
  if (done()) {
    return;
  }
  const std::size_t f = turn_;
  // STD_runIntro loops while the fighter's clip plays (animFlag bit 0): its pose is over.
  if (chaseTimer_ < 0 && (clipEnded(f) || !fighters_.at(f).desc.poseClip)) {
    endTurn();
    return;
  }
  // STD_applyEntityViewpoint while the camera waits beside it: it looks at the fighter's feet,
  // two thirds of its height up.
  tickChase(bones.at(f));
  tickName();
  tickStats();
  stepClip(0);
  stepClip(1);
  if (chasePhase_ == 0 && chaseTimer_ >= 0) {
    const ShowBones at = bonesOf(f, bones.at(f));
    view_.target = {at.root[0], -(fighters_.at(f).desc.body.height * 2) / 3, at.root[2]};
  }
  if (confirm) {
    // STD_setPostIntroPosition: straight to where the run ends, in the stance.
    if (chaseTimer_ != -1) {
      figures_.at(f).position = fighters_.at(f).target;
      play(f, IntroStanceClip);
      chaseTimer_ = -1;
    }
    endTurn();
  }
}

// --- the fight's camera ---

void MatchCamera::tickBanner(bool showing) {
  // STD_combatSetup: out to 4200 while the banner shows, back to 1000 as it bursts.
  if (showing) {
    if (minDistance_ < 4200) {
      minDistance_ += 400;
    }
  } else {
    minDistance_ = std::max(minDistance_ - 400, orbit_.minDistance);
  }
}

void MatchCamera::tickFight(bool busy, ruleset::Random& random) {
  minDistance_ = orbit_.minDistance;
  ++frame_;
  // STD_tickBattle: the corner's timer runs down to the orbit.
  if (timer_ != 0) {
    --timer_;
    if (timer_ == 0) {
      corner_ = false;
    }
  }
  if (frame_ % 600 == 0 && random.below(2) == 1) {
    corner_ = true;
    timer_ = random.below(0x29) + 0x3c;
  }
  if (!busy && timer_ == 0) {
    corner_ = false;
  }
}

ShowView MatchCamera::view(ArenaPoint partner, ArenaPoint rival,
                           const std::optional<ShowBones>& partnerBones) const {
  if (corner_) {
    // STD_setRandomViewpoint(ENTITY_TABLE[1], 4): STD_RANDOM_VIEWPOINTS[4], looking at the
    // partner's node 1 (STD_setViewpointRotationFromEntity).
    const ShowBones bones = partnerBones.value_or(defaultBones(partner, {}));
    return {.eye = {0xbb8, -0x5dc, 0xbb8},
            .target = {bones.root[0], bones.node1Y, bones.root[2]},
            .projection = orbit_.projection};
  }
  ArenaCamera camera = orbit_;
  camera.minDistance = minDistance_;
  const ArenaView orbit = arenaView(camera, partner, rival);
  return {.eye = orbit.eye, .target = orbit.target, .projection = orbit_.projection};
}

// --- STD_startCameraIntro ---

EndCamera::EndCamera(ArenaPoint partner, ShowBody partnerBody, ArenaPoint rival, ShowBody rivalBody,
                     ruleset::Random& random)
    : target_(partnerBody) {
  const std::int32_t dx = rival.x - partner.x;
  const std::int32_t dz = rival.z - partner.z;
  angle_ = (-atan4096(dz, dx) + 0x7de) & AngleMask;
  const std::int32_t d = groundDistance(dx, dz) + 0x2bc;
  const std::int32_t far = std::max(d, 0x5dc);
  const std::int32_t around = partnerBody.radius * 3 * 2;
  distances_ = {d, d, far * 120 / 100, far * 80 / 100, around};
  heights_ = {rivalBody.height, rivalBody.height, rivalBody.height * 200 / 100,
              partnerBody.height * 380 / 100, partnerBody.height * 250 / 100};
  // The keys from 5 on: heights first, then distances, as STD_startCameraIntro draws them.
  for (std::size_t i = 5; i < heights_.size(); ++i) {
    heights_.at(i) = randomBetween(random, 0x50, partnerBody.height * 180 / 100);
  }
  for (std::size_t i = 5; i < distances_.size(); ++i) {
    distances_.at(i) = randomBetween(random, around * 90 / 100, around * 180 / 100);
  }
}

void EndCamera::tick() {
  // STD_tickCameraIntro: from frame 30 it turns ever faster, up to 0x5b a frame by frame 60.
  if (frame_ >= 0x1e) {
    angle_ += interpolateClamped(0x1e, 0x3c, frame_, 0, 0x5b);
  }
  ++frame_;
}

ShowView EndCamera::view(ArenaPoint rival, const std::optional<ShowBones>& rivalBones) const {
  // The ticks hang the camera on the rival (STD_D_8007B9D0.entity), at the partner's eye height.
  const std::int32_t frame = std::max(frame_ - 1, 0);
  ShowView view = viewFromBone(rivalBones.value_or(defaultBones(rival, {})), 0, angle_ & AngleMask,
                               keyed(distances_, frame));
  view.target[1] = -(target_.height * 2) / 3;
  view.eye[1] = -keyed(heights_, frame);
  return view;
}

// --- STD_tickBattleResultScreen ---

void TimeUpNotice::tick(bool confirm) {
  if (done_) {
    return;
  }
  if (draw_) {
    // Two frames of the text, then the box until frame 0x3d of its own or a confirm.
    if (frame_ >= 2) {
      boxShown_ = true;
      if (confirm || frame_ - 2 >= SkipFrom) {
        done_ = true;
        return;
      }
    }
    ++frame_;
    return;
  }
  if (frame_ >= WinnerFrames || (frame_ >= SkipFrom && confirm)) {
    done_ = true;
    return;
  }
  if (frame_ == BoxFrame) {
    boxShown_ = true;
  }
  ++frame_;
}

// --- the letters ---

namespace {

// std_hud.c: STD_WIN_X_CURVE, STD_LOSE_DROP_Y, STD_LOSE_TILT, STD_VERSUS_MODEL_TARGET_X.
constexpr std::array<std::int32_t, 18> WinXCurve{0,    1,    5,    0xb,   0x14,  0x1f,
                                                 0x2d, 0x3d, 0x50, 0x65,  0x7d,  0x97,
                                                 0xb4, 0xd3, 0xf5, 0x119, 0x140, 0};
constexpr std::array<std::int32_t, 24> LoseDropY{-700, -692, -668, -628, -572, -500, -412, -308,
                                                 -188, -52,  100,  67,   44,   31,   28,   31,
                                                 44,   67,   100,  86,   82,   86,   100,  0};
constexpr std::array<std::int32_t, 14> LoseTilt{5,     0x14,  0x2f,  0x53,  0x82,  0xbc,  0x100,
                                                0x14e, 0x1a7, 0x20a, 0x278, 0x2f0, 0x372, 0x400};
constexpr std::array<std::int32_t, 4> DrawTargetX{-273, -86, 94, 272};

// STD_loadWinLoseModel: the letters' turns and the outer two letters' x, step by step (20 steps).
struct WinCurves {
  std::array<std::int32_t, 21> rotationX{};
  std::array<std::int32_t, 21> rotationY{};
  std::array<std::int32_t, 21> firstX{};
  std::array<std::int32_t, 21> lastX{};
};

WinCurves winCurves() {
  WinCurves curves;
  for (std::int32_t i = 0x14; i < 0x29; ++i) {
    const auto k = static_cast<std::size_t>(i - 0x14);
    curves.rotationX.at(k) = ((i * 0x2000) / 40) & AngleMask;
    curves.rotationY.at(k) = (((i * 0x800) / 40) + 0x800) & AngleMask;
    const std::int32_t curve = i < 0x25 ? WinXCurve.at(static_cast<std::size_t>(0x24 - i))
                                        : WinXCurve.at(static_cast<std::size_t>(i - 0x24));
    curves.firstX.at(k) = curve - 0xe6;
    curves.lastX.at(k) = 0x118 - curve;
  }
  return curves;
}

} // namespace

MatchLetters::MatchLetters(MatchEnding ending, ruleset::Random& random) : ending_(ending) {
  if (ending_ == MatchEnding::Won) {
    // STD_addWinScene: the four letters far behind the eye.
    z_.fill(-0x30c);
  } else if (ending_ == MatchEnding::Lost) {
    // STD_addLoseScene: the wipe's columns in a random order.
    for (std::size_t i = 0; i < order_.size(); ++i) {
      order_.at(i) = static_cast<std::int32_t>(i);
    }
    for (std::size_t i = 0; i < order_.size(); ++i) {
      std::swap(order_.at(i), order_.at(static_cast<std::size_t>(random.below(0x20))));
    }
  } else {
    // STD_loadVersusSceneModel: the letters off to the right.
    drawX_.fill(0x3e8);
  }
}

void MatchLetters::tick() {
  switch (ending_) {
  case MatchEnding::Won: {
    // STD_tickWinScene: from frame 61 a letter every 6 frames flies in spinning.
    if (frame_ < 0xa0) {
      ++frame_;
    }
    for (std::size_t i = 0; i < 4; ++i) {
      if (frame_ > static_cast<std::int32_t>(i) * 6 + 0x3c) {
        if (z_.at(i) < 0xdc) {
          z_.at(i) += 0x32;
        }
        if (steps_.at(i) < 0x14) {
          ++steps_.at(i);
        }
      }
    }
    break;
  }
  case MatchEnding::Lost: {
    // STD_tickLoseScene: the four letters drop one after another from frame 61, the wipe's
    // groups follow each letter's eleventh step; then they tilt and drop out.
    if (frame_ < 0x8c) {
      ++frame_;
    }
    constexpr std::array<std::int32_t, 4> Starts{0x3d, 0x40, 0x43, 0x46};
    for (std::size_t i = 0; i < 4; ++i) {
      if (frame_ >= Starts.at(i) && steps_.at(i) < 0x16) {
        ++steps_.at(i);
      }
    }
    for (std::size_t i = 0; i < 4; ++i) {
      if (steps_.at(i) < 0xb) {
        continue;
      }
      for (std::size_t j = 0; j < 8; ++j) {
        std::int32_t& height = wipe_.at(static_cast<std::size_t>(order_.at(j + (i * 8))));
        if (height < 0xf0) {
          height += 0x3c;
        }
      }
    }
    if (frame_ == 0x7f) {
      steps_.at(0) = 0;
    }
    break;
  }
  case MatchEnding::Drawn: {
    // STD_tickVersusModelScene: a letter every 5 frames slides in 200 a frame.
    ++frame_;
    for (std::size_t i = 0; i < 4; ++i) {
      if (frame_ > static_cast<std::int32_t>(i) * 5) {
        drawX_.at(i) = std::max(drawX_.at(i) - 200, DrawTargetX.at(i));
      }
    }
    break;
  }
  }
}

bool MatchLetters::finished() const {
  return ending_ == MatchEnding::Drawn && frame_ >= 0x51;
}

LetterScene MatchLetters::scene() const {
  LetterScene scene;
  switch (ending_) {
  case MatchEnding::Won: {
    static const WinCurves curves = winCurves();
    constexpr std::array<std::int32_t, 4> StartX{-0xfa, -0x32, 0x64, 0x104};
    scene.projection = 0x15e;
    for (std::size_t i = 0; i < 4; ++i) {
      const auto step = static_cast<std::size_t>(steps_.at(i));
      std::int32_t x = StartX.at(i);
      if (frame_ > static_cast<std::int32_t>(i) * 6 + 0x3c) {
        if (i == 0) {
          x = curves.firstX.at(step);
        } else if (i == 3) {
          x = curves.lastX.at(step);
        }
      }
      const bool moving = frame_ > static_cast<std::int32_t>(i) * 6 + 0x3c;
      scene.letters.push_back({.object = i,
                               .location = {x, 0, z_.at(i)},
                               .rotation = {moving ? curves.rotationX.at(step) : 0,
                                            moving ? curves.rotationY.at(step) : 0, 0}});
    }
    break;
  }
  case MatchEnding::Lost: {
    scene.projection = 0x15e;
    std::int32_t tilt = 0;
    if (frame_ >= 0x74) {
      tilt = LoseTilt.at(static_cast<std::size_t>(std::min(frame_, 0x81) - 0x74));
    }
    for (std::size_t i = 0; i < 4; ++i) {
      std::int32_t y = LoseDropY.at(static_cast<std::size_t>(steps_.at(i)));
      if (frame_ >= 0x74) {
        y = frame_ >= 0x80 ? LoseDropY.at(static_cast<std::size_t>(steps_.at(0))) + 0x320
                           : LoseDropY.at(static_cast<std::size_t>(steps_.at(i)));
      }
      scene.letters.push_back(
          {.object = 4 + i, .location = {0, y, 0xdc}, .rotation = {tilt, 0, 0}});
    }
    scene.wipe.assign(wipe_.begin(), wipe_.end());
    break;
  }
  case MatchEnding::Drawn:
    scene.projection = 0x200;
    for (std::size_t i = 0; i < 4; ++i) {
      scene.letters.push_back(
          {.object = i, .location = {drawX_.at(i), 0x78, 0x280}, .rotation = {}});
    }
    break;
  }
  return scene;
}

} // namespace dwe::game::tournament
