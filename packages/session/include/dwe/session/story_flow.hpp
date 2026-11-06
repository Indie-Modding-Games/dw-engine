#pragma once

#include <dwe/game/story/story_ports.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

namespace dwe::session {

// The story API names of the script flow orders (story_lua.hpp: story_api), published by
// StoryLuaHost::publishFlowApi.
namespace story_api {
inline constexpr std::string_view SetScript = "set_script";
inline constexpr std::string_view SectionOnExit = "section_on_exit";
inline constexpr std::string_view WarpTo = "warp_to";
inline constexpr std::string_view Teleport = "teleport";

inline constexpr std::array FlowPublished{SetScript, SectionOnExit, WarpTo, Teleport};
} // namespace story_api

// Plays a script's map change on the field: the original's map number, the exit and whether the
// new map's name shows (map.c: scriptTickChangeMap). False when it cannot (no such map).
using StoryMapChangeTarget =
    std::function<bool(std::int32_t map, std::int32_t exit, bool showName)>;

// The runner's map changes, to the target the app sets. One the target cannot play (or with no
// target) is kept as unplayed: the adapter then tells the runner the player stayed on the map, so
// the story goes on. The target logs why it could not; with no target, it is an error here.
class StoryMapChanges final : public game::story::MapChangePort {
public:
  StoryMapChanges() = default;

  void setTarget(StoryMapChangeTarget target) { target_ = std::move(target); }

  void startMapChange(std::int32_t map, std::int32_t exit, bool showName) override;

  // Whether the last change was not played, once.
  [[nodiscard]] bool takeUnplayed() { return std::exchange(unplayed_, false); }

private:
  StoryMapChangeTarget target_;
  bool unplayed_ = false;
};

// Holds the player for a script or lets it go (map.c: scriptPauseGame, tickScript): true holds.
using StoryHoldTarget = std::function<void(bool held)>;

// The runner's hold on the player, to the target the app sets; a target set while held is held at
// once, so a field entered during a script starts held.
class StoryFieldControl final : public game::story::FieldControlPort {
public:
  StoryFieldControl() = default;

  void setTarget(StoryHoldTarget target);

  void freeze() override;
  void release() override;

private:
  void apply();

  StoryHoldTarget target_;
  bool held_ = false;
};

} // namespace dwe::session
