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

TEST_CASE("std_main.c STD_setupMoveExecution: one technique in three hangs a camera on its "
          "attacker until the fight calms down") {
  dwe::ruleset::Random random{5};
  int hung = 0;
  constexpr int Moves = 3000;
  for (int i = 0; i < Moves; ++i) {
    MatchCamera camera{ArenaCamera{}};
    camera.tickFight(true, random);
    camera.moveStarted(1, 2, std::nullopt, random);
    if (!camera.focused()) {
      continue;
    }
    ++hung;
    CHECK(*camera.focused() == 1);
    CHECK(camera.preset() < 5);
    // It holds while someone attacks, and the orbit comes back once nobody does.
    camera.tickFight(true, random);
    CHECK(camera.focused());
    camera.tickFight(false, random);
    CHECK_FALSE(camera.focused());
  }
  CHECK(hung > Moves / 4);
  CHECK(hung < Moves * 5 / 12);
  // An all-around move draws one of the far cameras; a melee move only the first two.
  bool far = false;
  bool near = false;
  for (int i = 0; i < 200; ++i) {
    MatchCamera area{ArenaCamera{}};
    area.moveStarted(0, 3, std::nullopt, random);
    if (area.focused()) {
      far = true;
      CHECK(area.preset() >= 6);
      CHECK(area.preset() <= 8);
    }
    MatchCamera melee{ArenaCamera{}};
    melee.moveStarted(0, 1, std::nullopt, random);
    if (melee.focused()) {
      near = true;
      CHECK(melee.preset() <= 1);
    }
  }
  CHECK(far);
  CHECK(near);
}

TEST_CASE("the technique camera keeps an all-around move's view and never cuts the corner") {
  dwe::ruleset::Random random{9};
  MatchCamera camera{ArenaCamera{}};
  while (!camera.focused()) {
    camera.moveStarted(0, 3, std::nullopt, random);
  }
  const std::size_t preset = camera.preset();
  for (int i = 0; i < 50; ++i) {
    camera.moveStarted(1, 2, 3, random);
  }
  CHECK(*camera.focused() == 0);
  CHECK(camera.preset() == preset);
  MatchCamera corner{ArenaCamera{}};
  dwe::ruleset::Random always{1};
  while (!corner.corner()) {
    corner.tickFight(true, always);
  }
  for (int i = 0; i < 50; ++i) {
    corner.moveStarted(0, 2, std::nullopt, random);
  }
  CHECK(corner.corner());
  CHECK_FALSE(corner.focused());
}

TEST_CASE("std_main.c STD_setCameraToEntity: the camera turns with the fighter it hangs on") {
  // STD_CAMERA_PRESETS[4]: in front of the fighter, 1500 deep and 260 down in view space.
  const FixedCamera front = TechniqueCameras.at(4);
  const ShowView facingZ = fixedCameraView(front, {.x = 0, .z = 0}, 0, 500);
  CHECK(facingZ.eye[0] == 0);
  CHECK(facingZ.eye[1] == -750);
  CHECK(facingZ.eye[2] == 1324);
  CHECK(facingZ.target[2] < facingZ.eye[2]);
  CHECK(facingZ.projection == 500);
  // The fighter turned a quarter to face +x: the eye goes round with it, and with its place.
  const ShowView facingX = fixedCameraView(front, {.x = 300, .z = -200}, 1024, 500);
  CHECK(facingX.eye[0] == 300 + 1324);
  CHECK(facingX.eye[1] == -750);
  CHECK(facingX.eye[2] == -200);
  CHECK(facingX.target[0] < facingX.eye[0]);
  // Behind it to one side (preset 0): the eye is behind the fighter.
  const ShowView behind = fixedCameraView(TechniqueCameras.at(0), {.x = 0, .z = 0}, 0, 500);
  CHECK(behind.eye[2] < -1000);
  CHECK(behind.target[2] > behind.eye[2]);
  // The match camera uses it while it hangs on the rival.
  dwe::ruleset::Random random{3};
  MatchCamera camera{ArenaCamera{}};
  while (camera.focused() != std::optional<std::size_t>{1}) {
    camera = MatchCamera{ArenaCamera{.projection = 500}};
    camera.moveStarted(1, 2, std::nullopt, random);
  }
  const std::array<FightFigure, 2> fighters{
      FightFigure{.at = {.x = 900, .z = 0}, .yaw = 3072, .moveRange = std::nullopt},
      FightFigure{.at = {.x = -900, .z = 0}, .yaw = 1024, .moveRange = 2}};
  CHECK(camera.view(fighters, std::nullopt) ==
        fixedCameraView(TechniqueCameras.at(camera.preset()), fighters[1].at, 1024, 500));
}

TEST_CASE("std_setup.c STD_initializeCamera, STD_applyRotatingCamera: the arena turns 2 a frame") {
  ArenaTurnCamera camera;
  const ShowView start = camera.view();
  // 500 down and 3000 deep in view space, tilted down 100: the eye high over -z, looking at +z.
  CHECK(start.eye[0] == 0);
  CHECK(start.eye[1] < -900);
  CHECK(start.eye[2] < -2800);
  CHECK(start.target[2] > start.eye[2]);
  CHECK(start.projection == 500);
  for (int frame = 0; frame < 1024; ++frame) {
    camera.tick();
  }
  CHECK(camera.yaw() == 2048);
  const ShowView half = camera.view();
  CHECK(half.eye[0] == 0);
  CHECK(half.eye[1] == start.eye[1]);
  CHECK(half.eye[2] == -start.eye[2]);
  for (int frame = 0; frame < 1024; ++frame) {
    camera.tick();
  }
  CHECK(camera.yaw() == 0);
}

TEST_CASE(
    "vs_combat.c VS__tickBattle: the versus' corner camera only holds while a fighter is busy") {
  dwe::ruleset::Random always{1};
  MatchCamera camera{ArenaCamera{}, MatchCamera::Kind::Versus};
  while (!camera.corner()) {
    camera.tickFight(true, always);
  }
  camera.tickFight(true, always);
  CHECK(camera.corner());
  // Idle, the cups' corner would stay its 60 to 100 frames; the versus' goes back to the orbit.
  camera.tickFight(false, always);
  CHECK_FALSE(camera.corner());
}

TEST_CASE("vs_hud.c VS_tickResultModelScene: 1P WIN and 2P WIN fly in and rest upright") {
  for (const std::size_t winner : {std::size_t{0}, std::size_t{1}}) {
    VersusWinLetters letters{winner};
    const LetterScene start = letters.scene();
    REQUIRE(start.letters.size() == 5);
    // VS_RESULT_MODEL_INDICES: the winner's two objects, then WIN's three.
    CHECK(start.letters[0].object == winner * 2);
    CHECK(start.letters[1].object == winner * 2 + 1);
    CHECK(start.letters[4].object == 6);
    CHECK(start.letters[0].location == ShowPoint{-0x154, 0, -0x4d0});
    // Nothing moves before frame 61.
    for (int i = 0; i < 0x3c; ++i) {
      letters.tick();
    }
    CHECK(letters.scene().letters[0].location[2] == -0x4d0);
    letters.tick();
    CHECK(letters.scene().letters[0].location[2] == -0x4d0 + 0x42);
    CHECK(letters.scene().letters[1].location[2] == -0x4d0);
    for (int i = 0; i < 0xa0; ++i) {
      letters.tick();
    }
    const LetterScene end = letters.scene();
    CHECK(end.projection == 0x15e);
    for (const LetterPose& letter : end.letters) {
      CHECK(letter.location[2] == 0xdc);
      CHECK(letter.rotation == ShowPoint{0, 0, 0});
    }
    // VS_D_800729CC (1P) and VS_D_800729F8 (2P) at step 20; VS_RESULT_LAST_X for the last letter.
    CHECK(end.letters[0].location[0] == (winner == 0 ? 0x14 - 0x134 : 0x14 - 0x140));
    CHECK(end.letters[1].location[0] == -0x9b);
    CHECK(end.letters[4].location[0] == 0x111 - 0x14);
  }
}

TEST_CASE("vs_scene.c VS_tickVersusIntro: each row slides out, flashes, slides back and the next") {
  using Moment = VersusScoreShow::Moment;
  VersusScoreShow show{2};
  // Before the first battle: it fades in, the first row slides out 10 and flashes 20.
  show.start(Moment::First);
  int frames = 0;
  for (; !show.done() && frames < 1000; ++frames) {
    show.tick();
    if (frames == VersusScoreShow::SlideFrames) {
      CHECK(show.flash());
      CHECK(show.offset() == VersusScoreShow::SlideFrames);
    }
  }
  CHECK(show.round() == 0);
  // Slide 10, flash 20, then the fade out over 3 frames and one more.
  CHECK(frames == VersusScoreShow::SlideFrames + VersusScoreShow::FlashFrames + 4);
  CHECK(show.black() == doctest::Approx(1.0F));

  // After the first battle the row slides back, a pause, then the second slides out and flashes.
  show.start(Moment::Between);
  CHECK(show.black() == doctest::Approx(1.0F));
  show.tick();
  CHECK(show.offset() == VersusScoreShow::SlideFrames - 1);
  frames = 1;
  for (; !show.done() && frames < 1000; ++frames) {
    show.tick();
  }
  CHECK(show.round() == 1);
  CHECK(frames == VersusScoreShow::SlideFrames + VersusScoreShow::PauseFrames + 1 +
                      VersusScoreShow::SlideFrames + VersusScoreShow::FlashFrames + 4);

  // After the last battle: back, the pause, then until VS_STATE_TIMER reaches 80, which the pause
  // left at 10, and no fade.
  show.start(Moment::Last);
  frames = 0;
  for (; !show.done() && frames < 1000; ++frames) {
    show.tick();
  }
  CHECK(show.round() == 2);
  CHECK(show.offset() == 0);
  CHECK_FALSE(show.flash());
  CHECK(frames == VersusScoreShow::SlideFrames + 1 + VersusScoreShow::LastFrames);
  CHECK(show.black() == doctest::Approx(0.0F));
}
