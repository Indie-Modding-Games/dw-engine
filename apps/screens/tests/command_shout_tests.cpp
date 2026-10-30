#include <dwe/assets/digimon_table.hpp>
#include <dwe/screens/command_shout.hpp>

#include <doctest/doctest.h>

#include <vector>

#include "game/battle/tests/fixture.hpp"

namespace {

using namespace battle_test;
using dwe::screens::CommandShout;
using dwe::screens::NameKeys;
using dwe::screens::WindowFraction;

// A 4:3 window, as the original's 320 x 240 screen.
constexpr double FourThirds = 4.0 / 3.0;

} // namespace

TEST_CASE("the shout grows in 4 frames, hops up 40 pixels and goes after 20 frames") {
  CommandShout shout;
  CHECK_FALSE(shout.shown());
  // The node 60 lines under the middle: it hops.
  shout.shout("btl.shouts.0001", WindowFraction{.x = 0.5, .y = 0.75}, FourThirds);
  CHECK(shout.y() == 60);
  // command_shout.c: BTL_renderCommandShout with BTL_SHOUT_HOP_OFFSETS.
  const std::vector<int> scales{2, 4, 6, 8, 10, 10};
  const std::vector<int> hops{0,   -8,  -14, -20, -25, -30, -34, -36, -38, -39,
                              -40, -39, -38, -36, -34, -30, -34, -36, -38, -39};
  for (int frame = 0; frame < CommandShout::Frames; ++frame) {
    REQUIRE(shout.shown());
    CHECK(shout.hop() == hops.at(static_cast<std::size_t>(frame)));
    if (frame < 6) {
      CHECK(shout.scaleTenths() == scales.at(static_cast<std::size_t>(frame)));
    }
    shout.step();
  }
  CHECK_FALSE(shout.shown());
}

TEST_CASE("a shout high on the screen drops instead, and stays within 100 lines of the middle") {
  CommandShout shout;
  // The node 114 lines over the middle: held at -100, which is over -40, so it drops.
  shout.shout("btl.shouts.0000", WindowFraction{.x = 0.25, .y = 0.025}, FourThirds);
  CHECK(shout.nodeX() == -80);
  CHECK(shout.nodeY() == -114);
  CHECK(shout.y() == -100);
  for (int frame = 0; frame < 10; ++frame) {
    shout.step();
  }
  CHECK(shout.hop() == 40);
  // A new shout starts over.
  shout.shout("btl.shouts.0002", std::nullopt, FourThirds);
  CHECK(shout.frame() == 0);
  CHECK(shout.y() == 0);
  CHECK(shout.hop() == 0);
}

TEST_CASE("the shout is the command's, or the technique's name") {
  dwe::assets::DigimonTable species;
  const NameKeys names{&species, nullptr, &testCatalog()};
  Battle battle = startBattle(duel(evenStats(900, 600), evenStats(700, 80), {"punch", "shot"}), 3);
  const auto& partner = battle.state().partner();
  CHECK(dwe::screens::shoutTextKey(Command::Flee, partner, names) == "btl.shouts.0000");
  CHECK(dwe::screens::shoutTextKey(Command::Change, partner, names) == "btl.shouts.0006");
  CHECK(dwe::screens::shoutTextKey(Command::Technique2, partner, names) == "main.move_names.0001");
  CHECK(dwe::screens::shoutTextKey(Command::None, partner, names).empty());
}
