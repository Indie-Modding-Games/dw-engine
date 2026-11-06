#include <dwe/game/tournament/champion_show.hpp>

#include <doctest/doctest.h>

#include <cstdint>

namespace {

using dwe::game::tournament::ChampionShow;
using dwe::game::tournament::ShowBody;

constexpr ShowBody Body{.height = 300, .radius = 150};
constexpr std::int32_t BeforeScene =
    ChampionShow::FadeOutFrames + ChampionShow::SettleFrames + ChampionShow::FadeInFrames;

ChampionShow inScene() {
  ChampionShow show{Body};
  for (std::int32_t i = 0; i < BeforeScene; ++i) {
    show.tick(false);
  }
  return show;
}

} // namespace

TEST_CASE("the champion's podium shows behind a fade to black and back") {
  ChampionShow show{Body};
  CHECK(show.blackout() == 0.0);
  CHECK_FALSE(show.podiumShown());
  for (std::int32_t i = 0; i < 10; ++i) {
    show.tick(false);
  }
  CHECK(show.blackout() == 1.0);
  CHECK_FALSE(show.podiumShown());
  show.tick(false);
  CHECK(show.podiumShown());
  // STD_setChampionCamera: 200 up, twice the radius plus 800 back, two thirds of the height.
  CHECK(show.view().eye == dwe::game::tournament::ShowPoint{0, -200, -1100});
  CHECK(show.view().target[1] == -200);
  CHECK(show.view().projection == 0x200);
  CHECK(show.partner().y == -30);
  CHECK(show.partner().clip == ChampionShow::StanceClip);
  for (std::int32_t i = 0; i < ChampionShow::SettleFrames + ChampionShow::FadeInFrames; ++i) {
    show.tick(false);
  }
  CHECK(show.blackout() == 0.0);
  CHECK(show.letters().letters.empty());
}

TEST_CASE("the title's letters show 5 frames apart, spinning, then stand still") {
  ChampionShow show = inScene();
  for (int i = 0; i < 5; ++i) {
    show.tick(false);
  }
  REQUIRE(show.letters().letters.size() == 1);
  CHECK(show.letters().letters[0].location == dwe::game::tournament::ShowPoint{-760, 200, 1980});
  CHECK(show.letters().letters[0].scaleY == 0x2000);
  CHECK(show.letters().letters[0].rotation[1] == 0xa00);
  for (int i = 0; i < 40; ++i) {
    show.tick(false);
  }
  CHECK(show.letters().letters.size() == 9);
  CHECK(show.letters().letters[8].location[0] == 740);
  for (int i = 0; i < 20; ++i) {
    show.tick(false);
  }
  for (const auto& letter : show.letters().letters) {
    CHECK(letter.rotation[1] == 0);
  }
}

TEST_CASE("the podium lifts the partner while the camera circles it, then it poses") {
  ChampionShow show = inScene();
  for (int i = 0; i < 0x61; ++i) {
    show.tick(false);
  }
  CHECK(show.partner().clip == ChampionShow::StanceClip);
  CHECK(show.partner().starts == 1);
  show.tick(false);
  CHECK(show.partner().y == -55);
  CHECK(show.podiumY() == -55);
  // The first step of the orbit is a 640000th of 0xa00: nearly straight behind.
  CHECK(show.view().eye[2] == -1100);
  CHECK(show.view().target[1] == -255);
  for (int i = 0; i < 19; ++i) {
    show.tick(false);
  }
  // 640 of 4096 round at frame 20: the eye swung to the partner's left.
  CHECK(show.view().eye[0] < -900);
  CHECK(show.letters().letters[0].location[1] == 600);
  for (int i = 0; i < 60; ++i) {
    show.tick(false);
  }
  CHECK(show.partner().clip == ChampionShow::PoseClip);
  CHECK(show.partner().y == -30 - 25 * 0x50);
}

TEST_CASE("once the pose is over the camera climbs to 2000 and the scene ends at frame 21") {
  ChampionShow show = inScene();
  for (int i = 0; i < 0x61 + 0x50; ++i) {
    show.tick(false);
  }
  REQUIRE(show.partner().clip == ChampionShow::PoseClip);
  for (int i = 0; i < 10; ++i) {
    show.tick(false);
  }
  CHECK(show.view().eye[1] == -200);
  CHECK_FALSE(show.done());
  show.tick(true);
  CHECK(show.view().eye[1] == -736);
  CHECK(show.view().projection == 0x200 + 20);
  for (int i = 0; i < 19; ++i) {
    show.tick(true);
  }
  CHECK(show.view().eye[1] == -2000);
  CHECK(show.view().projection == 0x200 + 7 * 20);
  CHECK_FALSE(show.done());
  show.tick(true);
  CHECK(show.done());
}
