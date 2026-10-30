#pragma once

#include <dwe/session/item_keeper.hpp>
#include <dwe/session/story_scenes.hpp>

#include "field_scene.hpp"

namespace dwe::app {

// The story's item keeper (call_routine 11 -> shop/script_menu.c: tickItemKeeper): it opens the
// session's item keeper for the story's ItemKeeper screen, with the keeper the operand names, and
// tells the story when the player leaves; the item keeper screen shows the visit.
class StoryItemKeeper {
public:
  // The keeper must outlive this object; without one the story's visits close at once.
  void attach(session::ItemKeeper* keeper) { keeper_ = keeper; }
  // Takes the story's ItemKeeper screen; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const FieldScene& scene);
  // Tells the story that the visit it waits on is over.
  void update(session::StoryScenes& scenes);

private:
  session::ItemKeeper* keeper_ = nullptr;
  // Whether the story waits on the open visit.
  bool waiting_ = false;
};

} // namespace dwe::app
