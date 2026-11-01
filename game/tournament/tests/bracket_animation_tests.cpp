#include <dwe/core/yaml.hpp>
#include <dwe/game/tournament/bracket_animation.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

using namespace dwe::game::tournament;

// data/stadium's bracket with a made-up board: rows climbing 26, 26 and 22 pixels, the loser's
// hop and each slot's path through the tree (a first-round pair stands 40 pixels apart).
constexpr std::string_view Stadium = R"(bracket:
  row_y: [30, 4, 4, -22, -22, -48, -48, -70]
  hop_y: [0, 0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20, 22, 21, 20, 21, 22]
  paths:
  - {x: [-110, -110, -90, -90, -70, -70, 0, 0], flip: [1, 1, 1, 1, 1, 1, 1, 1]}
  - {x: [-70, -70, -90, -90, -70, -70, 0, 0], flip: [0, 0, 1, 1, 1, 1, 1, 1]}
  - {x: [-50, -50, -30, -30, -70, -70, 0, 0], flip: [1, 1, 0, 0, 1, 1, 1, 1]}
  - {x: [-10, -10, -30, -30, -70, -70, 0, 0], flip: [0, 0, 0, 0, 1, 1, 1, 1]}
  - {x: [10, 10, 30, 30, 70, 70, 0, 0], flip: [1, 1, 1, 1, 0, 0, 1, 1]}
  - {x: [50, 50, 30, 30, 70, 70, 0, 0], flip: [0, 0, 1, 1, 0, 0, 1, 1]}
  - {x: [70, 70, 90, 90, 70, 70, 0, 0], flip: [1, 1, 0, 0, 0, 0, 1, 1]}
  - {x: [110, 110, 90, 90, 70, 70, 0, 0], flip: [0, 0, 0, 0, 0, 0, 1, 1]}
  sprite_palettes: [0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 3]
  projectile_icons: [0, 0, 0, 3, 4, 2, 6, 5, 2, 6, 5, 9]
  projectile_palettes: [3, 0, 0, 0, 2]
)";

BracketLayout layout() {
  auto stadium = dwe::core::parseYaml(Stadium);
  REQUIRE(stadium);
  auto parsed = parseBracketLayout(*stadium);
  REQUIRE(parsed);
  REQUIRE(parsed->has_value());
  return **parsed;
}

// Slots 0 to 7 hold species 3 to 10; the first round's winners are the even slots and the second
// round's 0 and 4.
Bracket bracketWithPartnerIn(std::size_t slot) {
  Bracket bracket;
  bracket.playerSlot = slot;
  for (std::size_t i = 0; i < Entrants; ++i) {
    bracket.entrants.at(i) = static_cast<std::int32_t>(i) + 3;
  }
  bracket.firstRound = {0, 2, 4, 6};
  bracket.secondRound = {0, 4};
  return bracket;
}

int ticksUntilFinished(BracketAnimation& animation, int limit = 2000) {
  int ticks = 0;
  while (!animation.finished() && ticks < limit) {
    animation.tick();
    ++ticks;
  }
  return ticks;
}

} // namespace

TEST_CASE("data/stadium: the bracket's tables, none in an older extraction") {
  const BracketLayout board = layout();
  CHECK(board.rowY.size() == 8);
  CHECK(board.paths[3].x[2] == -30);
  CHECK_FALSE(board.paths[3].flip[0]);
  CHECK(board.paths[2].flip[0]);
  auto older = dwe::core::parseYaml("opponent_base_stats: [1]\n");
  REQUIRE(older);
  auto none = parseBracketLayout(*older);
  REQUIRE(none);
  CHECK_FALSE(none->has_value());
}

TEST_CASE("STD_renderBracketDigimon: a species' cell, its face and its palette") {
  const BracketLayout board = layout();
  // Species 3: the third block of the first page; face 1 is the right half, 3 the lower right.
  const BracketCell idle = bracketSpriteCell(board, 3, 1);
  CHECK(idle.page == 0);
  CHECK(idle.u == 80);
  CHECK(idle.v == 0);
  CHECK(idle.palette == 3);
  const BracketCell beaten = bracketSpriteCell(board, 3, 3);
  CHECK(beaten.v == 16);
  // Species 70 is on the second page; u and v wrap in a byte.
  const BracketCell second = bracketSpriteCell(board, 70, 0);
  CHECK(second.page == 1);
  CHECK(second.u == 160);
  CHECK(second.v == 0);
  CHECK(second.palette == 3);
  // The last column of a page is 15 wide.
  CHECK(bracketSpriteCell(board, 8, 1).width == 15);
  // STD_renderBracketProjectile: the icon's 8 x 8 cell on row 0xC0, its palette by icon - 1.
  const BracketCell shot = bracketShotCell(board, 4);
  CHECK(shot.u == 24);
  CHECK(shot.v == 0xC0);
  CHECK(shot.palette == 0);
}

TEST_CASE("STD_addBracketIntro: the card shows 100 frames and fades for 20") {
  BracketAnimation animation{layout(), bracketWithPartnerIn(0)};
  animation.startIntro();
  CHECK(animation.view().card == 128);
  CHECK(animation.view().fade == 128);
  for (int frame = 0; frame < 100; ++frame) {
    animation.tick();
  }
  CHECK(animation.view().card == 128);
  CHECK_FALSE(animation.introDone());
  for (int frame = 0; frame < 20; ++frame) {
    animation.tick();
  }
  CHECK(animation.introDone());
  CHECK(animation.view().card == 0);
  CHECK(animation.view().fade == 0);
}

TEST_CASE("STD_updateBracket: the partner's first match waits, climbs and fades") {
  BracketAnimation animation{layout(), bracketWithPartnerIn(1)};
  animation.show(BracketShow::Match);
  // 41 frames of waiting, 26 of climbing (30 to 4), then the board fades 3 a frame for each of
  // the two fighters until 128: 22 frames.
  CHECK(ticksUntilFinished(animation) == 41 + 26 + 22);
  const BracketAnimation::View view = animation.view();
  CHECK(view.fade == 128);
  CHECK(view.shots.empty());
  CHECK(view.slots[1].y == 4);
  CHECK(view.slots[0].y == 4);
  // The others have not moved.
  CHECK(view.slots[2].y == 30);
  const std::vector<std::int32_t> sounds = animation.takeSounds();
  CHECK(std::ranges::count(sounds, 5) == 2);
}

TEST_CASE("STD_updateBracket: a match the partner does not play is fought with shots") {
  BracketAnimation animation{layout(), bracketWithPartnerIn(3)};
  animation.show(BracketShow::Match);
  bool sawDoubleShot = false;
  bool sawFlash = false;
  int ticks = 0;
  while (!animation.finished() && ticks < 2000) {
    animation.tick();
    ++ticks;
    const BracketAnimation::View view = animation.view();
    sawDoubleShot = sawDoubleShot || std::ranges::any_of(view.shots, [](const auto& shot) {
                      return shot.doubled && shot.species == 3;
                    });
    sawFlash = sawFlash || std::ranges::contains(view.flashes, std::size_t{1});
  }
  REQUIRE(animation.finished());
  CHECK(sawDoubleShot);
  CHECK(sawFlash);
  const BracketAnimation::View view = animation.view();
  // Slot 0 won its match and stands on the next step's x; slot 1 lost and hopped down.
  CHECK(view.slots[0].x == -90);
  CHECK(view.slots[0].y == 4);
  CHECK(view.slots[1].face == 3);
  CHECK(view.slots[1].y == 4 + 22);
  // Then the partner's match (slots 2 and 3) came up.
  CHECK(view.slots[3].y == 4);
  CHECK(view.fade == 128);
  const std::vector<std::int32_t> sounds = animation.takeSounds();
  for (const std::int32_t sound : {1, 3, 4, 5}) {
    CHECK(std::ranges::contains(sounds, sound));
  }
}

TEST_CASE("STD_updateBracket: after a win the partner climbs to its next match, and the "
          "champion to the top") {
  BracketAnimation animation{layout(), bracketWithPartnerIn(0)};
  for (int match = 0; match < 3; ++match) {
    animation.show(BracketShow::Match);
    REQUIRE(ticksUntilFinished(animation) < 2000);
    animation.matchOver(true);
  }
  // The partner met slot 1, then slot 2 (the first round's winner beside it), then slot 4.
  BracketAnimation::View view = animation.view();
  CHECK(view.slots[0].x == -70);
  CHECK(view.slots[0].y == -48);
  CHECK(view.slots[4].y == -48);
  animation.show(BracketShow::Champion);
  CHECK(ticksUntilFinished(animation) == 61);
  view = animation.view();
  CHECK(view.slots[0].y == -70);
  CHECK(view.slots[0].x == 0);
  CHECK(view.slots[4].face == 3);
  CHECK(view.slots[4].y == -22);
}
