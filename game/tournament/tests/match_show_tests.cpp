#include <dwe/game/tournament/match_show.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>

namespace {

using namespace dwe::game::tournament;

constexpr std::array<std::optional<ShowBones>, 2> NoBones{};

IntroFighter partner() {
  return {.start = {.x = 1800, .z = 0},
          .yaw = 1024,
          .walkIn = 1300,
          .body = {.height = 300, .radius = 150},
          .nameLength = 4,
          .stats = {500, 300, 100, 80, 60, 40},
          .poseClip = std::nullopt,
          .clips = {}};
}

IntroFighter rival() {
  return {.start = {.x = -1800, .z = 0},
          .yaw = 3072,
          .walkIn = -1300,
          .body = {.height = 400, .radius = 200},
          .nameLength = 6,
          .stats = {900, 700, 200, 150, 120, 90},
          .poseClip = std::nullopt,
          .clips = {}};
}

} // namespace

TEST_CASE("the intro brings the name in a letter every 4 frames before the run") {
  MatchIntro intro{partner(), rival()};
  CHECK(intro.turn() == 0);
  CHECK(intro.takeMusic() == 0);
  CHECK(intro.view().projection == 0x15e);
  // STD_startCameraChase: the eye no lower than 180 above the ground.
  CHECK(intro.view().eye[1] <= -180);
  for (int i = 0; i < 4; ++i) {
    intro.tick(false, NoBones);
  }
  CHECK(intro.nameShown() == 1);
  for (int i = 0; i < 12; ++i) {
    intro.tick(false, NoBones);
  }
  CHECK(intro.nameShown() == 4);
  CHECK(intro.figure(0).clip == IntroStanceClip);
  // The last letter grows in over 3 frames, then the fighter runs and the row slides up.
  for (int i = 0; i < 4; ++i) {
    intro.tick(false, NoBones);
  }
  CHECK(intro.figure(0).clip == IntroRunClip);
  CHECK(intro.nameY() == 40);
  CHECK_FALSE(intro.stats());
}

TEST_CASE("the intro counts the stats up to the fighter's and runs it to its mark") {
  MatchIntro intro{partner(), rival()};
  bool sawStats = false;
  bool sawCount = false;
  int frames = 0;
  while (intro.turn() == 0 && frames < 400) {
    intro.tick(false, NoBones);
    ++frames;
    if (intro.stats()) {
      sawStats = true;
      sawCount = sawCount || intro.counting();
    }
  }
  CHECK(sawStats);
  CHECK(sawCount);
  REQUIRE(intro.turn() == 1);
  // It ran 1300 toward the centre at the fallback step, within 50 of its mark.
  CHECK(intro.figure(0).position.x >= 500 - 50);
  CHECK(intro.figure(0).position.x <= 500 + 50);
  CHECK(intro.takeMusic() == 1);
  while (!intro.done() && frames < 800) {
    intro.tick(false, NoBones);
    ++frames;
  }
  CHECK(intro.done());
  CHECK(intro.figure(1).position.x >= -500 - 50);
  CHECK(intro.figure(1).position.x <= -500 + 50);
}

TEST_CASE("confirm cuts a fighter's turn short and puts it on its mark") {
  MatchIntro intro{partner(), rival()};
  intro.tick(false, NoBones);
  intro.tick(true, NoBones);
  CHECK(intro.turn() == 1);
  CHECK(intro.figure(0).position.x == 500);
  CHECK(intro.figure(0).clip == IntroStanceClip);
  intro.tick(true, NoBones);
  CHECK(intro.done());
  CHECK(intro.figure(1).position.x == -500);
}

TEST_CASE("the camera swings round to the runner's front") {
  MatchIntro intro{partner(), rival()};
  const ShowView before = intro.view();
  while (intro.figure(0).clip != IntroRunClip) {
    intro.tick(false, NoBones);
  }
  for (int i = 0; i < 25; ++i) {
    intro.tick(false, NoBones);
  }
  const ShowView after = intro.view();
  CHECK(after != before);
  // STD_updateCameraLerp: looking down at the runner from above.
  CHECK(after.eye[1] < after.target[1]);
}

TEST_CASE("the banner pulls the arena camera out to 4200 and back") {
  const ArenaCamera orbit{.minDistance = 1000};
  MatchCamera camera{orbit};
  CHECK(camera.minDistance() == MatchCamera::BannerMinDistance);
  for (int i = 0; i < 20; ++i) {
    camera.tickBanner(true);
  }
  CHECK(camera.minDistance() == 4200);
  camera.tickBanner(false);
  CHECK(camera.minDistance() == 3800);
  for (int i = 0; i < 20; ++i) {
    camera.tickBanner(false);
  }
  CHECK(camera.minDistance() == 1000);
}

TEST_CASE("the corner camera only comes on a 600th frame and holds 60 to 100 frames") {
  dwe::ruleset::Random random{7};
  MatchCamera camera{ArenaCamera{}};
  int corners = 0;
  for (int frame = 1; frame <= 6000; ++frame) {
    const bool was = camera.corner();
    camera.tickFight(true, random);
    if (camera.corner() && !was) {
      CHECK(frame % 600 == 0);
      ++corners;
      int held = 0;
      while (camera.corner() && held < 200) {
        camera.tickFight(true, random);
        ++frame;
        ++held;
      }
      CHECK(held >= 0x3c - 1);
      CHECK(held <= 0x64);
    }
  }
  CHECK(corners > 0);
  const ShowView corner = [&] {
    MatchCamera fixed{ArenaCamera{}};
    dwe::ruleset::Random always{1};
    while (!fixed.corner()) {
      fixed.tickFight(true, always);
    }
    return fixed.view({.x = 100, .z = 0}, {.x = 400, .z = 0}, std::nullopt);
  }();
  CHECK(corner.eye == ShowPoint{3000, -1500, 3000});
  CHECK(corner.target[0] == 100);
}

TEST_CASE("the end camera circles faster after 30 frames") {
  dwe::ruleset::Random random{3};
  EndCamera camera{{.x = 0, .z = 0},
                   {.height = 300, .radius = 150},
                   {.x = 800, .z = 0},
                   {.height = 400, .radius = 200},
                   random};
  camera.tick();
  const ShowView first = camera.view({.x = 800, .z = 0}, std::nullopt);
  CHECK(first.projection == 0x15e);
  CHECK(first.target[0] == 800);
  CHECK(first.target[1] == -200);
  // At frame 0 it stands the distance plus 700 away, the rival's height up.
  CHECK(first.eye[1] == -400);
  for (int i = 0; i < 28; ++i) {
    camera.tick();
  }
  const ShowView still = camera.view({.x = 800, .z = 0}, std::nullopt);
  for (int i = 0; i < 40; ++i) {
    camera.tick();
  }
  const ShowView turned = camera.view({.x = 800, .z = 0}, std::nullopt);
  CHECK(turned.eye != still.eye);
}

TEST_CASE("time up with a winner shows the damage box at frame 60 and ends at 121") {
  TimeUpNotice notice{false};
  for (int i = 0; i < TimeUpNotice::BoxFrame; ++i) {
    notice.tick(true);
    CHECK_FALSE(notice.done());
  }
  CHECK_FALSE(notice.boxShown());
  notice.tick(false);
  CHECK(notice.boxShown());
  notice.tick(true);
  CHECK(notice.done());

  TimeUpNotice waited{false};
  int frames = 0;
  while (!waited.done() && frames < 300) {
    waited.tick(false);
    ++frames;
  }
  CHECK(frames == TimeUpNotice::WinnerFrames + 1);
}

TEST_CASE("time up on a draw opens the box at once") {
  TimeUpNotice notice{true};
  notice.tick(false);
  notice.tick(false);
  notice.tick(false);
  CHECK(notice.boxShown());
  notice.tick(true);
  CHECK(notice.done());
}

TEST_CASE("the won match's letters fly in and come to rest upright") {
  dwe::ruleset::Random random{1};
  MatchLetters letters{MatchEnding::Won, random};
  const LetterScene start = letters.scene();
  REQUIRE(start.letters.size() == 4);
  CHECK(start.letters[0].location[2] == -780);
  for (int i = 0; i < 0xa0; ++i) {
    letters.tick();
  }
  const LetterScene end = letters.scene();
  CHECK(end.projection == 0x15e);
  for (const LetterPose& letter : end.letters) {
    CHECK(letter.location[2] == 220);
    CHECK(letter.rotation == ShowPoint{0, 0, 0});
  }
  CHECK(end.letters[0].location[0] == -210);
  CHECK(end.letters[1].location[0] == -50);
  CHECK(end.letters[3].location[0] == 260);
}

TEST_CASE("the lost match's letters drop, the wipe darkens and they fall away") {
  dwe::ruleset::Random random{1};
  MatchLetters letters{MatchEnding::Lost, random};
  CHECK(letters.scene().letters[0].location[1] == -700);
  for (int i = 0; i < 0x60; ++i) {
    letters.tick();
  }
  LetterScene mid = letters.scene();
  REQUIRE(mid.wipe.size() == 32);
  for (std::int32_t height : mid.wipe) {
    CHECK(height == 240);
  }
  for (const LetterPose& letter : mid.letters) {
    CHECK(letter.location[1] == 100);
    CHECK(letter.object >= 4);
  }
  for (int i = 0; i < 0x40; ++i) {
    letters.tick();
  }
  const LetterScene end = letters.scene();
  CHECK(end.letters[0].rotation[0] == 0x400);
  CHECK(end.letters[0].location[1] > 600);
}

TEST_CASE("the drawn match's letters slide in and finish at frame 81") {
  dwe::ruleset::Random random{1};
  MatchLetters letters{MatchEnding::Drawn, random};
  for (int i = 0; i < 0x50; ++i) {
    letters.tick();
    CHECK_FALSE(letters.finished());
  }
  letters.tick();
  CHECK(letters.finished());
  const LetterScene scene = letters.scene();
  CHECK(scene.projection == 0x200);
  CHECK(scene.letters[0].location == ShowPoint{-273, 120, 640});
  CHECK(scene.letters[3].location == ShowPoint{272, 120, 640});
}
