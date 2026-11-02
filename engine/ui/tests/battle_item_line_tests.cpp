// The battle shows no line of its own while a thrown item flies: the original has none, so the
// engine's "Here it comes!" is gone from both skins (docs/modulos/apps/screens.md).

#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "game_pack_fixture.hpp"

namespace dwe::ui {

TEST_CASE("the battle draws no line while a thrown item flies, in either skin") {
  for (const std::string_view skin : {"original", "modern"}) {
    CAPTURE(skin);
    fixture::Screen screen{"battle", "es"};
    screen.skin(skin);
    screen.ui->setSetting("battle.item_in_flight", true);
    screen.ui->show("battle");
    screen.frames();
    CHECK_FALSE(screen.ui->elementCenter("battle", "item-in-flight").has_value());
    CHECK(screen.texts.text("battle.item_in_flight") != "¡Ahí va!");
  }
}

} // namespace dwe::ui
