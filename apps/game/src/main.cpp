#include <dwe/content/disc_install.hpp>
#include <dwe/content/user_data.hpp>
#include <dwe/platform/platform.hpp>

#include <cstdlib>
#include <optional>
#include <span>

#include "dev_views.hpp"
#include "game.hpp"
#include "options.hpp"
#include "saves.hpp"
#include "sign_dump.hpp"
#include "ui_demo.hpp"

int main(int argc, char** argv) {
  using namespace dwe::app;
  auto options = parseOptions(std::span{argv, static_cast<std::size_t>(argc)});
  if (!options) {
    return EXIT_FAILURE;
  }
  if (!options->contentGiven) {
    const auto userData = dwe::platform::userDataFolder(dwe::content::UserDataOrganization,
                                                        dwe::content::UserDataApplication);
    options->content =
        dwe::content::defaultContentFolder(userData ? std::optional{*userData} : std::nullopt);
  }
  if (options->listSaves) {
    return listSaves(*options);
  }
  if (!options->dumpSigns.empty()) {
    return dumpSigns(*options);
  }
  if (options->checkContent) {
    return checkContentFolder(options->content);
  }
  if (options->uiDemo) {
    UiDemoOptions demo{.content = options->content,
                       .language = options->language,
                       .width = options->width,
                       .height = options->height,
                       .virtualGamepad = options->virtualGamepad,
                       .uiSkin = options->uiSkin,
                       .textFont = options->textFont,
                       .fishing = options->uiDemoFishing,
                       .player = options->uiDemoPlayer,
                       .chart = options->uiDemoChart,
                       .chartDetail = options->uiDemoChartDetail,
                       .chartAllRaised = options->uiDemoChartAllRaised,
                       .bagSlots = options->uiDemoBagSlots};
    if (!options->maps.empty()) {
      demo.map = options->maps.front();
    }
    return runUiDemo(demo);
  }
  return wantsDevView(*options) ? runDevView(*options) : runGame(*options);
}
