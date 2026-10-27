#include <cstdlib>
#include <span>

#include "dev_views.hpp"
#include "game.hpp"
#include "options.hpp"
#include "saves.hpp"
#include "ui_demo.hpp"

int main(int argc, char** argv) {
  using namespace dwe::app;
  const auto options = parseOptions(std::span{argv, static_cast<std::size_t>(argc)});
  if (!options) {
    return EXIT_FAILURE;
  }
  if (options->listSaves) {
    return listSaves(*options);
  }
  if (options->checkContent) {
    return checkContentFolder(options->content);
  }
  if (options->uiDemo) {
    UiDemoOptions demo{.content = options->content,
                       .width = options->width,
                       .height = options->height,
                       .virtualGamepad = options->virtualGamepad,
                       .uiSkin = options->uiSkin,
                       .textFont = options->textFont,
                       .fishing = options->uiDemoFishing,
                       .bagSlots = options->uiDemoBagSlots};
    if (!options->maps.empty()) {
      demo.map = options->maps.front();
    }
    return runUiDemo(demo);
  }
  return wantsDevView(*options) ? runDevView(*options) : runGame(*options);
}
