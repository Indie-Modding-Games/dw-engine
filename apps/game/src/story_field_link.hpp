#pragma once

#include <dwe/assets/map_document.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "story_stage.hpp"

namespace dwe::session {
class StoryAdapter;
} // namespace dwe::session

namespace dwe::game::story {
class ScreenHistory;
} // namespace dwe::game::story

namespace dwe::app {

class FieldScene;

// Joins the story to the field it plays on (map.c: changeMap, scriptTickChangeMap; tamer.c:
// checkMapInteraction): every map entered runs its map-head section, the cells of a map whose
// script the story set up ask for their sections, and the story's map changes fade the field out to
// the map of the original they name. Until the story sets up a map's script, field/script_warps
// stands in for its cells. Both must outlive the link.
//
// It also keeps the story lock (session::StoryLock): an exit the player takes, a script's map
// change or a cell section into what the stage keeps closed does not happen, and the game shows the
// lock's message. A player already on a closed map (a save from further on) stays and may leave by
// any exit into an open map.
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

  // The previous screen a fled battle goes back to (story_scenes): every map change the field plays
  // moves it on (map.c: changeMap). Null follows none; it must outlive the link.
  void followScreens(game::story::ScreenHistory* screens) { screens_ = screens; }

  // The text key of the story lock's message, once, when something the stage keeps closed was
  // asked for since the last call.
  [[nodiscard]] std::optional<std::string> takeLockNotice();

private:
  bool playMapChange(std::int32_t map, std::int32_t exit, bool showName);
  // Moves the previous screen on to the map just entered (followScreens).
  void followScreen(std::int32_t map);
  // The player's cell sections the stage lets the story run; a newly asked closed one notices.
  [[nodiscard]] std::vector<std::int32_t> openCellSections();
  void noticeLock();

  session::StoryAdapter* story_;
  FieldScene* scene_;
  game::story::ScreenHistory* screens_ = nullptr;
  // The characters and the camera the story's scripts move, and the hold on the player.
  std::unique_ptr<StoryStage> stage_;
  std::uint32_t mapSerial_ = 0;
  // The original's number of the map the player is on, when it has one.
  std::optional<std::int32_t> mapNumber_;
  // The closed cell sections the player's cell asked for in the last tick: a step cell asks every
  // tick, and only its first ask shows the message.
  std::vector<std::int32_t> closedSections_;
  // The cell the player stood on when the lock kept a script's map change from playing: its
  // sections do not run again until the player steps off it, or the change and its message would
  // repeat every frame.
  std::optional<assets::CollisionCell> stalledCell_;
  bool lockNoticed_ = false;
};

} // namespace dwe::app
