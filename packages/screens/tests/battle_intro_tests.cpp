#include <dwe/screens/battle_intro.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using dwe::screens::BattleIntro;
using dwe::screens::IntroStep;

namespace {

// The frames of the banner until it bursts, calling playerBack on frame `playerBackAt` (0: never).
std::int32_t bannerFrames(std::int32_t playerBackAt) {
  BattleIntro intro(1);
  while (intro.current() == IntroStep::Anger) {
    intro.step();
  }
  std::int32_t frames = 0;
  while (intro.current() == IntroStep::Banner && frames < 1000) {
    ++frames;
    if (frames == playerBackAt) {
      intro.playerBack();
    }
    intro.step();
  }
  return frames;
}

} // namespace

TEST_CASE("the banner stays 200 frames unless the player is back, then until its pieces are in") {
  CHECK(bannerFrames(0) == 200);
  // Back early, the banner waits for its pieces only.
  const std::int32_t early = bannerFrames(16);
  CHECK(early < 200);
  CHECK(early >= 17);
  // Back late, two more frames.
  CHECK(bannerFrames(150) == 151);
}

TEST_CASE("the banner waits for the NPCs that run off before it bursts") {
  BattleIntro intro(1);
  while (intro.current() == IntroStep::Anger) {
    intro.step();
  }
  std::int32_t frames = 0;
  while (intro.current() == IntroStep::Banner && frames < 1000) {
    ++frames;
    // battle_setup.c: BTL_handleBattleIntro loops until every one of them is off screen.
    intro.holdBurst(frames < 260);
    intro.step();
  }
  CHECK(frames == 260);
}

namespace {

// The original's word as battle.banner_shape draws it in English.
constexpr std::string_view OriginalShape = "#####.....##....######..######.##.....######\n"
                                           "##..##....##......##......##...##.....##....\n"
                                           "##..##...####.....##......##...##.....##....\n"
                                           "#####...##..##....##......##...##.....######\n"
                                           "##..##..##..##....##......##...##.....##....\n"
                                           "##..##.########...##......##...##.....##....\n"
                                           "##..##.##....##...##......##...##.....##....\n"
                                           "#####..##....##...##......##...######.######";

// The banner's pieces once every one is in place.
std::vector<dwe::screens::BannerPiece> gatheredBanner(BattleIntro intro) {
  while (intro.current() == IntroStep::Anger) {
    intro.step();
  }
  intro.playerBack();
  for (std::int32_t frame = 0; frame < 400 && intro.current() == IntroStep::Banner; ++frame) {
    intro.step();
  }
  return intro.pieces();
}

} // namespace

TEST_CASE("the English banner drawing gives the original's pieces in the original's order") {
  using dwe::screens::bannerPlacesOf;
  const auto places = bannerPlacesOf(OriginalShape);
  REQUIRE(places.size() == dwe::screens::BannerPieceCount);
  CHECK(places.front() == dwe::screens::BannerPlace{.x = -172, .y = -48});
  CHECK(places.back() == dwe::screens::BannerPlace{.x = 172, .y = 36});
  // Flown in from the same seed, the pieces move as the original's do.
  CHECK(gatheredBanner(BattleIntro{7, places}) == gatheredBanner(BattleIntro{7}));
}

TEST_CASE("a banner in another language takes its own word, letter by letter, centred") {
  using dwe::screens::BannerPlace;
  using dwe::screens::bannerPlacesOf;
  // "LA": the L first, top to bottom, then the A.
  const auto places = bannerPlacesOf("#...#\n#..#.#\n##.###");
  REQUIRE(places.size() == 10);
  CHECK(places.at(0) == BannerPlace{.x = -20, .y = -12});
  CHECK(places.at(3) == BannerPlace{.x = -12, .y = 12});
  CHECK(places.at(4) == BannerPlace{.x = 12, .y = -12});
  CHECK(places.back() == BannerPlace{.x = 20, .y = 12});
  CHECK(gatheredBanner(BattleIntro{3, places}).size() == 10);
}

TEST_CASE("a drawing with no piece or too many leaves the original's banner") {
  using dwe::screens::bannerPlacesOf;
  CHECK(bannerPlacesOf("battle.banner_shape").empty());
  CHECK(bannerPlacesOf("").empty());
  CHECK(bannerPlacesOf(std::string(dwe::screens::BannerPieceSlots + 1, '#')).empty());
  CHECK(gatheredBanner(BattleIntro{3, {}}).size() == dwe::screens::BannerPieceCount);
}

TEST_CASE("the banner is finished two frames after its pieces are in, and again once they burst") {
  BattleIntro intro(3);
  while (intro.current() == IntroStep::Anger) {
    intro.step();
  }
  CHECK_FALSE(intro.finished());
  intro.playerBack();
  // The frame its last trail goes: not yet (BTL_renderBattleStartText counts before it moves).
  std::int32_t frames = 0;
  const auto gathered = [&intro] {
    const std::vector<dwe::screens::BannerPiece> pieces = intro.pieces();
    return std::ranges::all_of(pieces, [](const auto& piece) { return piece.trail == 0.0F; });
  };
  while (!gathered() && frames < 400) {
    intro.step();
    ++frames;
  }
  REQUIRE(intro.current() == IntroStep::Banner);
  CHECK_FALSE(intro.finished());
  intro.step();
  CHECK(intro.finished());
  CHECK(intro.current() == IntroStep::Banner);
  intro.step();
  // BTL_handleBattleIntro read the flag before this frame: the burst starts
  // (BTL_initializeBattleStartTextBurst) and the banner is not finished until its pieces are off.
  CHECK(intro.current() == IntroStep::Burst);
  CHECK_FALSE(intro.finished());
  std::int32_t burst = 0;
  while (intro.current() == IntroStep::Burst && burst < 100) {
    intro.step();
    ++burst;
  }
  CHECK(intro.finished());
  CHECK(intro.done());
  // Measured: 60 vblanks from BTL_initializeBattleStartTextBurst to its removal, 20 battle frames.
  CHECK(burst >= 15);
  CHECK(burst <= 25);
}
