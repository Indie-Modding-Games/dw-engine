// The fishing's bait menu (fish.c: FISH_renderBaitMenu): each row writes its count after a plain x
// in two right-aligned digit cells (renderItemAmount(0, 2, ...)), "x 3" and "x12", with a letter
// both fonts have; the original font has no multiplication sign.

#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::string_view Bait = "fishing_bait";

void open(Screen& screen) {
  screen.ui->setSetting("fishing_baits", 2.0);
  screen.ui->setSetting("fishing_bait_name.0", std::string{"fishing.bait.title"});
  screen.ui->setSetting("fishing_bait_count.0", 3.0);
  screen.ui->setSetting("fishing_bait_name.1", std::string{"fishing.bait.title"});
  screen.ui->setSetting("fishing_bait_count.1", 12.0);
  screen.ui->show(Bait, true);
  screen.frames(2);
}

} // namespace

TEST_CASE("the bait menu writes the count after an x in two cells, as the original") {
  for (const std::string_view language : {"en", "es"}) {
    CAPTURE(language);
    Screen screen{Bait, language};
    open(screen);
    const std::string one = screen.ui->elementText(Bait, "bait-1");
    const std::string two = screen.ui->elementText(Bait, "bait-2");
    CHECK(one.ends_with(" x 3"));
    CHECK(two.ends_with(" x12"));
    CHECK_FALSE(one.contains("\xC3\x97"));
  }
}

} // namespace dwe::ui
