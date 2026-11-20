#pragma once

#include <dwe/game/coop/protocol.hpp>

#include <vector>

namespace dwe::session {
class StoryGameOrders;
} // namespace dwe::session

namespace dwe::app {

class FieldScene;

// Applies to the map the scene shows the edits the story's orders made this tick
// (session/story_map_edits.hpp). Without the orders there is nothing to apply. Returns them as
// co-op shares them with the other players on the map (#357).
std::vector<game::coop::WorldEdit> applyStoryMapEdits(FieldScene& scene,
                                                      session::StoryGameOrders* orders);

} // namespace dwe::app
