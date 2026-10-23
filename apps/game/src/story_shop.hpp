#pragma once

#include <dwe/session/shop_counter.hpp>
#include <dwe/session/story_scenes.hpp>

#include "field_scene.hpp"

namespace dwe::app {

// The story's shop (call_routine 8 -> script_common.c: openShop): it opens the session's shop
// counter for the story's Shop and RecycleShop screens, with the keeper the operand names, and
// tells the story when the player leaves; the shop screen shows the visit.
class StoryShop {
public:
  // The counter must outlive this object; without one the story's shops close at once.
  void attach(session::ShopCounter* shop) { shop_ = shop; }
  // Takes the story's Shop and RecycleShop screens; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const FieldScene& scene);
  // Tells the story that the visit it waits on is over.
  void update(session::StoryScenes& scenes);

private:
  session::ShopCounter* shop_ = nullptr;
  // Whether the story waits on the open visit.
  bool waiting_ = false;
};

} // namespace dwe::app
