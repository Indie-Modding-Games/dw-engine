#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "story_stage.hpp"

namespace dwe::session {
class StoryAdapter;
} // namespace dwe::session

namespace dwe::app {

class FieldScene;

// Joins the story to the field it plays on (map.c: changeMap, scriptTickChangeMap; tamer.c:
// checkMapInteraction): every map entered runs its map-head section, the cells of a map whose
// script the story set up ask for their sections, and the story's map changes fade the field out to
// the map of the original they name. Until the story sets up a map's script, field/script_warps
// stands in for its cells. Both must outlive the link.
class StoryFieldLink {
public:
  StoryFieldLink(session::StoryAdapter& story, FieldScene& scene);
  ~StoryFieldLink();
  StoryFieldLink(const StoryFieldLink&) = delete;
  StoryFieldLink& operator=(const StoryFieldLink&) = delete;
  StoryFieldLink(StoryFieldLink&&) = delete;
  StoryFieldLink& operator=(StoryFieldLink&&) = delete;

  // After the field's tick: the sections the player's cell asked for.
  void afterFieldTick();
  // After the scene took its map change: a new map runs its map-head section, then the field
  // learns whether the story runs its cells.
  void afterMapChange();

private:
  bool playMapChange(std::int32_t map, std::int32_t exit, bool showName);

  session::StoryAdapter* story_;
  FieldScene* scene_;
  // The characters and the camera the story's scripts move, and the hold on the player.
  std::unique_ptr<StoryStage> stage_;
  std::uint32_t mapSerial_ = 0;
  // The original's number of the map the player is on, when it has one.
  std::optional<std::int32_t> mapNumber_;
};

} // namespace dwe::app
