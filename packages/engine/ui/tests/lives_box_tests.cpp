#include <doctest/doctest.h>

#include <format>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "game_pack_fixture.hpp"

namespace dwe::ui {
namespace {

using fixture::Screen;

constexpr std::string_view Box = "lives_box";

bool opaque(const std::string& colour) {
  if (colour == "transparent") {
    return false;
  }
  if (colour.starts_with('#') && colour.size() == 9) {
    return colour.substr(7) != "00";
  }
  static const std::regex Number{R"(\d+(\.\d+)?)"};
  std::vector<double> parts;
  for (auto it = std::sregex_iterator(colour.begin(), colour.end(), Number);
       it != std::sregex_iterator(); ++it) {
    parts.push_back(std::stod(it->str()));
  }
  return parts.size() < 4 || parts[3] > 0.0;
}

// What draws a life's slot: its picture of the original or the skin's own drawing, empty when the
// skin hides both.
std::string slotPicture(const Ui& ui, int slot) {
  const std::string life = std::format("life-{}", slot);
  if (ui.styleProperty(Box, life, "display") != "none" &&
      opaque(ui.styleProperty(Box, life, "image-color"))) {
    return "picture";
  }
  const std::string id = std::format("slot-{}", slot);
  const std::string decorator = ui.styleProperty(Box, id, "decorator");
  if (decorator.empty() || decorator == "none" ||
      !opaque(ui.styleProperty(Box, id, "image-color"))) {
    return {};
  }
  return decorator;
}

} // namespace

TEST_CASE("the lives box draws every life, full and lost, in every skin") {
  for (const std::string_view skin : {"original", "modern", "pixel", "devvago"}) {
    CAPTURE(skin);
    Screen screen{Box};
    screen.skin(skin);
    Ui& ui = *screen.ui;
    ui.setSetting("lives_box.shown", true);
    ui.setSetting("lives_box.outline", false);
    ui.setSetting("lives_box.grow", 1.0);
    ui.setSetting("lives_box.lives", 1.0);
    ui.setSetting("lives_box.slots", 3.0);
    ui.setSetting("lives_box.lost_full", false);
    ui.setSetting("lives_box.revision", 1.0);
    ui.show(Box);
    screen.frames();
    for (int slot = 0; slot < 3; ++slot) {
      CAPTURE(slot);
      CHECK(ui.elementShown(Box, std::format("slot-{}", slot)));
      CHECK_FALSE(slotPicture(ui, slot).empty());
    }
    CHECK_FALSE(ui.elementHasClass(Box, "slot-0", "empty"));
    CHECK(ui.elementHasClass(Box, "slot-1", "empty"));
    CHECK(ui.elementHasClass(Box, "slot-2", "empty"));
    if (slotPicture(ui, 0) != "picture") {
      CHECK(slotPicture(ui, 0) != slotPicture(ui, 1));
    }

    ui.setSetting("lives_box.lost_full", true);
    ui.setSetting("lives_box.revision", 2.0);
    screen.frames();
    CHECK_FALSE(ui.elementHasClass(Box, "slot-1", "empty"));
    CHECK(ui.elementHasClass(Box, "slot-2", "empty"));
  }
}

} // namespace dwe::ui
