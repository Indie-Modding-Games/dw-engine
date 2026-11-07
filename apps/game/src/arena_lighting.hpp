#pragma once

#include <dwe/core/math.hpp>
#include <dwe/render/model_renderer.hpp>

#include <glm/vec3.hpp>

namespace dwe::app {

// The tournament arena's light (std_setup.c: STD_initializeLighting): three flat lights (where they
// travel, in the original's axes, with 8-bit colours where 256 is full) and the ambient 0x400, a
// quarter of the GTE's 4096, where a map keeps 0x800 (field_scene.cpp: mapLighting).
[[nodiscard]] inline render::ModelLighting arenaLighting() {
  constexpr float FullColor = 256.0F;
  constexpr float Ambient = 0.25F;
  constexpr float Far = 1000.0F;
  constexpr float Near = 20.0F;
  constexpr float Back = 200.0F;
  constexpr float Key = 255.0F;
  constexpr float Fill = 160.0F;
  constexpr float Rim = 96.0F;
  const auto light = [](glm::vec3 direction, float color) {
    return render::DirectionalLight{.direction = core::fromOriginalAxes(direction),
                                    .color = glm::vec3{color / FullColor}};
  };
  return {.lights = {light({Far, Far, Far}, Key), light({-Far, Far, -Far}, Fill),
                     light({-Near, Near, Back}, Rim)},
          .ambient = glm::vec3{Ambient}};
}

} // namespace dwe::app
