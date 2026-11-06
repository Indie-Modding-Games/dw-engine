#pragma once

#include <cstdint>
#include <variant>

// The changes the story's orders make to the map the field shows. They last until the field loads a
// map again, which brings back the map's own objects and collision, as the original reloads them
// (map.c: loadMap).
namespace dwe::session {

// set_obj_visibility (script_interp.c:1530, overworld.c: setMapObjectsFlag): the script sets the
// flag of one map object; the renderer hides an object whose flag is
// assets::MapObjectInstance::HiddenFlag.
struct StoryObjectFlag {
  std::int32_t object = 0;
  std::int32_t flag = 0;
};

// set_rect_impassable (script_interp.c:2158, map_collision.c:190 setImpassableRect): a rectangle of
// collision cells that stop movement, from (column, row), as the script gives them in bytes.
struct StoryBlockedRect {
  std::int32_t column = 0;
  std::int32_t row = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

using StoryMapEdit = std::variant<StoryObjectFlag, StoryBlockedRect>;

} // namespace dwe::session
