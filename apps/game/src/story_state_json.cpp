#include "story_state_json.hpp"

#include <format>
#include <iterator>

#include "control_json.hpp"

namespace dwe::app {

namespace {

void appendVec(std::string& out, glm::vec2 value) {
  std::format_to(std::back_inserter(out), "[{},{}]", value.x, value.y);
}

void appendVec(std::string& out, glm::vec3 value) {
  std::format_to(std::back_inserter(out), "[{},{},{}]", value.x, value.y, value.z);
}

void appendCamera(std::string& out, const StoryCameraView& camera) {
  out += R"({"eye":)";
  appendVec(out, camera.eye);
  out += R"(,"target":)";
  appendVec(out, camera.target);
  std::format_to(std::back_inserter(out), R"(,"focal_length":{},"background":)",
                 camera.focalLength);
  appendVec(out, camera.backgroundSize);
  out += R"(,"viewport":)";
  appendVec(out, camera.viewport);
  std::format_to(std::back_inserter(out), R"(,"scale":{},"scroll":)", camera.scale);
  appendVec(out, camera.scroll);
  out += '}';
}

} // namespace

std::string storyStateJson(const StoryStateView& view) {
  std::string out = R"({"map":)";
  out += jsonString(view.map);
  out += R"(,"triggers":[)";
  bool first = true;
  for (std::int32_t trigger = 0; trigger < game::story::TriggerCount; ++trigger) {
    if (view.state.trigger(trigger).value_or(false)) {
      std::format_to(std::back_inserter(out), "{}{}", first ? "" : ",", trigger);
      first = false;
    }
  }
  out += R"(],"variables":{)";
  first = true;
  for (std::int32_t variable = 0; variable < game::story::VariableCount; ++variable) {
    if (const std::uint8_t value = view.state.variable(variable).value_or(0); value != 0) {
      std::format_to(std::back_inserter(out), R"({}"{}":{})", first ? "" : ",", variable, value);
      first = false;
    }
  }
  std::format_to(std::back_inserter(out), R"(}},"script":{{"busy":{},"map_script":)",
                 view.busy ? "true" : "false");
  out += view.mapScript ? std::to_string(*view.mapScript) : "null";
  out += R"(,"sections":[)";
  first = true;
  for (const game::story::SectionRef& section : view.sections) {
    std::format_to(std::back_inserter(out), R"({}{{"script":{},"section":{}}})", first ? "" : ",",
                   section.script, section.section);
    first = false;
  }
  out += R"(]},"stage":)";
  out += view.stage.empty() ? std::string{"null"} : jsonString(view.stage);
  out += R"(,"camera":)";
  if (view.camera) {
    appendCamera(out, *view.camera);
  } else {
    out += "null";
  }
  out += '}';
  return out;
}

} // namespace dwe::app
