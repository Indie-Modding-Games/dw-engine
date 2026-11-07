#include <dwe/core/original_math.hpp>
#include <dwe/game/tournament/champion_show.hpp>

#include <algorithm>
#include <cstddef>

namespace dwe::game::tournament {

namespace {

using core::original::rcos;
using core::original::rsin;

constexpr std::int32_t AngleMask = 0xfff;
// STD_initializeChampionScene: the partner's feet and the podium 30 up, facing 0x200.
constexpr std::int32_t StartY = -0x1e;
constexpr std::int32_t PartnerYaw = 0x200;
// STD_setChampionCamera: the eye 200 up, the radius twice plus 800 back; projection 0x200.
constexpr std::int32_t EyeY = -0xc8;
constexpr std::int32_t EyeBack = 0x320;
constexpr std::int32_t Projection = 0x200;
// STD_loadChampionModels: the letters' row 1980 into the scene, 200 down.
constexpr std::int32_t LettersZ = 0x7bc;
constexpr std::int32_t LettersStartY = 0xc8;
constexpr std::array<std::int32_t, ChampionShow::Letters> LetterX{
    -0x2f8, -0x230, -0x168, -0xa0, 0x28, 0xbe, 0x168, 0x230, 0x2e4};
constexpr std::int32_t LetterScaleY = 0x2000;
// STD_tickChampionScene: each letter starts its spin 5 frames after the one before, shows 5
// frames into it and stops at 20; the lift starts at frame 0x61.
constexpr std::int32_t LetterGap = 5;
constexpr std::int32_t SpinFrames = 0x14;
constexpr std::int32_t SpinStep = 0x200;
constexpr std::int32_t LiftFrom = 0x61;
// STD_tickChampionScene: from frame 0x3d each letter in turn, one frame apart, copies a 6-wide
// column of the gold strip into its own face, 2 columns further left each frame for 10 frames,
// then the resting column again (MoveImage from x 0x378 - (step + 1) * 2; the strip is 4-bit, 4
// texels a column).
constexpr std::int32_t GleamFrom = 0x3d;
constexpr std::int32_t GleamSteps = 0xb;
constexpr std::int32_t GleamColumnTexels = 4 * 2;
// The lift: 25 a frame for the partner and the podium, 50 for the letters until they are 600
// down; the camera circles for 0x4f frames.
constexpr std::int32_t LiftStep = 0x19;
constexpr std::int32_t LettersStep = 0x32;
constexpr std::int32_t LettersLowest = 0x258;
constexpr std::int32_t OrbitFrames = 0x50;
// STD_CHAMPION_CAMERA_Y: the eye's climb (doubled) once the pose is over, until 2000 up; the
// projection grows 20 a frame for 7 frames and the scene ends 21 frames in.
constexpr std::array<std::int16_t, 8> ClimbY{-0x170, -0x205, -0x285, -0x2f2,
                                             -0x34a, -0x390, -0x3df, -0x3e8};
constexpr std::int32_t ClimbTop = -0x7cf;
constexpr std::int32_t ZoomFrames = 8;
constexpr std::int32_t ZoomStep = 0x14;
constexpr std::int32_t ClimbFrames = 0x15;
// STD_tournamentMain: fadeToBlack(10), fadeFromBlack(15).
constexpr double FadeOutLength = 10.0;
constexpr double FadeInLength = 15.0;

// The column a letter's face shows after `steps` copies: the resting one before the first and
// after the last.
std::int32_t gleamShift(std::int32_t steps) {
  return steps > 0 && steps < GleamSteps ? steps * GleamColumnTexels : 0;
}

} // namespace

ChampionShow::ChampionShow(ShowBody partner) : body_(partner) {
  // STD_loadChampionModels: a quarter turn and a half, easing in over 40 frames and out over 39.
  constexpr std::int32_t Sweep = 0xa00;
  constexpr std::int32_t Steps = 0x28;
  for (std::int32_t i = 1; i <= Steps; ++i) {
    orbit_.at(static_cast<std::size_t>(i - 1)) = Sweep * (i * i) / (Steps * Steps);
  }
  for (std::int32_t i = 0; i < Steps - 1; ++i) {
    orbit_.at(static_cast<std::size_t>(i + Steps)) =
        2 * Sweep - orbit_.at(static_cast<std::size_t>(0x26 - i));
  }
  lettersY_ = LettersStartY;
  // STD_initializeChampionScene, STD_setChampionCamera.
  partner_ = {.y = StartY, .yaw = PartnerYaw, .clip = StanceClip, .starts = 0};
  podiumY_ = StartY;
  view_ = {.eye = {0, EyeY, -(body_.radius * 2 + EyeBack)},
           .target = {0, -body_.height * 2 / 3, 0},
           .projection = Projection};
}

double ChampionShow::blackout() const {
  switch (phase_) {
  case Phase::FadeOut:
    return std::min(1.0, static_cast<double>(phaseFrame_) / FadeOutLength);
  case Phase::Settle:
    return 1.0;
  case Phase::FadeIn:
    return std::max(0.0, 1.0 - (static_cast<double>(phaseFrame_) / FadeInLength));
  case Phase::Scene:
    break;
  }
  return 0.0;
}

void ChampionShow::tick(bool poseEnded) {
  if (done_) {
    return;
  }
  ++phaseFrame_;
  switch (phase_) {
  case Phase::FadeOut:
    if (phaseFrame_ >= FadeOutFrames) {
      phase_ = Phase::Settle;
      phaseFrame_ = 0;
    }
    return;
  case Phase::Settle:
    if (phaseFrame_ >= SettleFrames) {
      phase_ = Phase::FadeIn;
      phaseFrame_ = 0;
    }
    return;
  case Phase::FadeIn:
    if (phaseFrame_ >= FadeInFrames) {
      phase_ = Phase::Scene;
      phaseFrame_ = 0;
    }
    return;
  case Phase::Scene:
    tickScene(poseEnded);
    return;
  }
}

void ChampionShow::play(std::uint32_t clip) {
  partner_.clip = clip;
  ++partner_.starts;
}

void ChampionShow::tickScene(bool poseEnded) {
  switch (state_) {
  case 0:
    ++timer_;
    for (std::int32_t i = 0; i < Letters; ++i) {
      const auto at = static_cast<std::size_t>(i);
      if (timer_ >= i + GleamFrom && gleam_.at(at) < GleamSteps) {
        ++gleam_.at(at);
      }
      if (timer_ < i * LetterGap) {
        continue;
      }
      if (timer_ == i * LetterGap + LetterGap) {
        shown_.at(at) = true;
      }
      spin_.at(at) =
          timer_ >= i * LetterGap + SpinFrames ? 0 : (spin_.at(at) + SpinStep) & AngleMask;
    }
    if (timer_ >= LiftFrom) {
      timer_ = 0;
      ++state_;
      play(StanceClip);
    }
    return;
  case 1: {
    ++timer_;
    if (lettersY_ < LettersLowest) {
      lettersY_ += LettersStep;
    }
    partner_.y -= LiftStep;
    podiumY_ -= LiftStep;
    if (timer_ >= OrbitFrames) {
      play(PoseClip);
      timer_ = 0;
      ++state_;
      return;
    }
    // RotMatrix about y applied to (0, 0, -distance): the eye goes round, its height kept.
    const std::int32_t angle = orbit_.at(static_cast<std::size_t>(timer_ - 1)) & AngleMask;
    const std::int64_t back = -(body_.radius * 2 + EyeBack);
    view_.eye[0] = static_cast<std::int32_t>((rsin(angle) * back) >> 12);
    view_.eye[2] = static_cast<std::int32_t>((rcos(angle) * back) >> 12);
    view_.target[1] = partner_.y - body_.height * 2 / 3;
    return;
  }
  default:
    if (poseEnded) {
      if (view_.eye[1] >= ClimbTop) {
        view_.eye[1] = ClimbY.at(static_cast<std::size_t>(std::min<std::int32_t>(
                           timer_, static_cast<std::int32_t>(ClimbY.size()) - 1))) *
                       2;
      }
      ++timer_;
      if (timer_ < ZoomFrames) {
        view_.projection += ZoomStep;
      }
    }
    done_ = timer_ >= ClimbFrames;
    return;
  }
}

LetterScene ChampionShow::letters() const {
  LetterScene scene{.letters = {}, .projection = Projection, .wipe = {}};
  if (phase_ != Phase::Scene) {
    return scene;
  }
  for (std::int32_t i = 0; i < Letters; ++i) {
    const auto at = static_cast<std::size_t>(i);
    if (shown_.at(at)) {
      scene.letters.push_back({.object = at,
                               .location = {LetterX.at(at), lettersY_, LettersZ},
                               .rotation = {0, spin_.at(at), 0},
                               .scaleY = LetterScaleY,
                               .gleamShift = gleamShift(gleam_.at(at))});
    }
  }
  return scene;
}

} // namespace dwe::game::tournament
