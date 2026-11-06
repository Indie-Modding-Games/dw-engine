// The game pack's HUD in both looks: what it writes stays inside the boxes drawn round it
// (Ui::overflowingTexts, docs/modulos/engine/ui.md).

#include <dwe/ui/minimap.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

} // namespace

TEST_CASE("the minimap's north mark sits on its own badge over the ring, in both looks") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    Screen screen{"hud", "en"};
    screen.skin(skin);
    screen.ui->setSetting("minimap", std::string{"auto"});
    screen.ui->setSetting("minimap_size", std::string{"medium"});
    screen.ui->show("hud");
    MinimapGrid grid{
        .columns = 4, .rows = 2, .cells = {}, .cellSize = 100.0F, .originX = 0.0F, .originZ = 0.0F};
    grid.cells.assign(8, MinimapCell::Walkable);
    screen.ui->setMinimap(grid, "Bosque");
    screen.frames();
    const auto north = screen.ui->elementRect("hud", "minimap-north");
    const auto frame = screen.ui->elementRect("hud", "minimap-frame");
    REQUIRE(north.has_value());
    REQUIRE(frame.has_value());
    // On the top of the ring, crossing its edge: the badge is drawn over it, so the letter reads.
    CHECK(north->top < frame->top);
    CHECK(north->bottom > frame->top);
    for (const Ui::TextOverflow& overflow : screen.ui->overflowingTexts()) {
      MESSAGE(overflow.element << " '" << overflow.text << "' " << overflow.box << " by "
                               << overflow.overflow << " dp");
      CHECK_FALSE(overflow.element.contains("minimap"));
    }
  }
}

} // namespace dwe::ui
