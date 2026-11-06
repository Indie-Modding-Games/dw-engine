// Prints what the engine's map camera (packages/engine/render/src/map_camera.cpp) gives for the
// cameras and points of a vectors file, so the map tools' Rust camera can be checked against it
// (packages/rust/dwe-map/tests/camera.rs). Built and run by scripts/mapper.sh camera-vectors;
// not part of the CMake build.
//
// Input, one command per line ('#' starts a comment):
//   map <name> <viewpoint xyz> <reference xyz> <projection> <tiles w h> <viewport w h> <follow xyz>
//   project <world xyz>
//   pick <window x y> <ground height>
// Output: the same lines, each followed by its results.

#include <dwe/render/map_camera.hpp>

#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

using dwe::render::MapCamera;

void printMap(std::istringstream& in, std::optional<MapCamera>& camera) {
  std::string name;
  dwe::render::OriginalMapCamera original;
  glm::vec2 viewport{};
  glm::vec3 follow{};
  in >> name >> original.viewpoint.x >> original.viewpoint.y >> original.viewpoint.z >>
      original.reference.x >> original.reference.y >> original.reference.z >> original.projection >>
      original.tiles.x >> original.tiles.y >> viewport.x >> viewport.y >> follow.x >> follow.y >>
      follow.z;
  camera.emplace(dwe::render::mapCameraDesc(original), viewport);
  camera->follow(follow);
  std::printf("map %s scroll %.6f %.6f scale %.6f\n", name.c_str(), camera->scroll().x,
              camera->scroll().y, camera->scale());
}

void printProject(std::istringstream& in, const MapCamera& camera) {
  glm::vec3 world{};
  in >> world.x >> world.y >> world.z;
  if (const auto point = camera.projectToBackground(world)) {
    std::printf("project %.6f %.6f %.6f\n", point->pixel.x, point->pixel.y, point->viewDepth);
  } else {
    std::printf("project none\n");
  }
}

void printPick(std::istringstream& in, const MapCamera& camera) {
  glm::vec2 window{};
  float height = 0.0F;
  in >> window.x >> window.y >> height;
  if (const auto world = camera.pickGround(window, height)) {
    std::printf("pick %.6f %.6f %.6f\n", world->x, world->y, world->z);
  } else {
    std::printf("pick none\n");
  }
}

} // namespace

int main() {
  std::optional<MapCamera> camera;
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream in{line};
    std::string command;
    if (!(in >> command) || command.starts_with('#')) {
      continue;
    }
    if (command == "map") {
      printMap(in, camera);
    } else if (command == "project" && camera) {
      printProject(in, *camera);
    } else if (command == "pick" && camera) {
      printPick(in, *camera);
    } else {
      std::fprintf(stderr, "unknown or early command: %s\n", line.c_str());
      return 1;
    }
  }
  return 0;
}
