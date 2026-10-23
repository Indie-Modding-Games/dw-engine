#pragma once

namespace dwe::session {
class StoryGameOrders;
} // namespace dwe::session

namespace dwe::app {

class FieldScene;

// Applies to the map the scene shows the edits the story's orders made this tick
// (session/story_map_edits.hpp). Without the orders there is nothing to apply.
void applyStoryMapEdits(FieldScene& scene, session::StoryGameOrders* orders);

} // namespace dwe::app
