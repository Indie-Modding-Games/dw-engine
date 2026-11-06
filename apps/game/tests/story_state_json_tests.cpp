#include <doctest/doctest.h>

#include "control_json.hpp"
#include "story_state_json.hpp"

namespace {

using dwe::app::parseJson;
using dwe::app::selectJson;
using dwe::app::StoryCameraView;
using dwe::app::storyStateJson;
using dwe::app::StoryStateView;
using dwe::app::writeJson;

TEST_CASE("story_state lists the set triggers, the non-zero variables, the script and the camera") {
  StoryStateView view;
  view.map = "twna01";
  REQUIRE(view.state.setTrigger(799, true));
  REQUIRE(view.state.setTrigger(3, true));
  REQUIRE(view.state.setVariable(12, 4));
  REQUIRE(view.state.setVariable(255, 1));
  view.busy = true;
  view.mapScript = 5;
  view.sections = {{.script = 5, .section = 2}, {.script = 0, .section = 40}};
  view.stage = "e1";
  view.camera = StoryCameraView{.eye = {0.0F, 2000.0F, -3000.5F},
                                .target = {0.0F, 0.0F, 0.0F},
                                .focalLength = 300.0F,
                                .backgroundSize = {512.0F, 480.0F},
                                .viewport = {1280.0F, 720.0F},
                                .scale = 2.5F,
                                .scroll = {0.0F, 32.0F}};

  const std::string text = storyStateJson(view);
  CHECK(text == R"({"map":"twna01","triggers":[3,799],"variables":{"12":4,"255":1},)"
                R"("script":{"busy":true,"map_script":5,"sections":[{"script":5,"section":2},)"
                R"({"script":0,"section":40}]},"stage":"e1","camera":{"eye":[0,2000,-3000.5],)"
                R"("target":[0,0,0],"focal_length":300,"background":[512,480],)"
                R"("viewport":[1280,720],"scale":2.5,"scroll":[0,32]}})");
  // It is JSON the channel can pick parts of.
  const auto parsed = parseJson(text);
  REQUIRE(parsed);
  CHECK(writeJson(*selectJson(*parsed, "variables.12")) == "4");
  CHECK(writeJson(*selectJson(*parsed, "script.sections.0.section")) == "2");
}

TEST_CASE("story_state of a fresh story is empty, with nulls for what is missing") {
  StoryStateView view;
  view.map = "start";
  CHECK(storyStateJson(view) ==
        R"({"map":"start","triggers":[],"variables":{},"script":{"busy":false,)"
        R"("map_script":null,"sections":[]},"stage":null,"camera":null})");
}

} // namespace
