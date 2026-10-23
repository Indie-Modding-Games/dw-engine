#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// The screen the player came from, as the original keeps it for a fled battle (map.c: changeMap,
// PREVIOUS_SCREEN, CURRENT_EXIT, PREVIOUS_EXIT).
namespace dwe::game::story {

// A map change whose previous screen is not the screen left: leaving `from` (any screen when
// none) for `to`, the previous screen becomes `previous` and, when given, its exit `previousExit`
// (map.c: changeMap, the special cases after it copies CURRENT_SCREEN).
struct PreviousScreenOverride {
  std::optional<std::int32_t> from;
  std::int32_t to = 0;
  std::int32_t previous = 0;
  std::optional<std::int32_t> previousExit;
};

struct ScreenHistoryRules {
  std::vector<PreviousScreenOverride> overrides;
};

// Where the player is and was. The numbers are the original's map ids and exits.
struct ScreenPlace {
  std::int32_t screen = 0;
  std::int32_t exit = 0;
  std::int32_t previousScreen = 0;
  std::int32_t previousExit = 0;

  friend bool operator==(const ScreenPlace&, const ScreenPlace&) = default;
};

class ScreenHistory {
public:
  explicit ScreenHistory(ScreenHistoryRules rules = {}) : rules_{std::move(rules)} {}

  // A new game starts with both screens on the first map at its exit (map.c: initializeMap).
  void start(std::int32_t screen, std::int32_t exit);
  // A loaded game puts back what its save kept (main.c: the continue path).
  void restore(const ScreenPlace& place) { place_ = place; }
  // A script's map change keeps the exit it leaves by as the previous exit before the change
  // (map.c: scriptTickChangeMap); a walk through warp `warp` keeps the warp's number
  // (tamer.c: checkMapInteraction).
  void leaveByScript() { place_.previousExit = place_.exit; }
  void leaveByWarp(std::int32_t warp) { place_.previousExit = warp; }
  // A fled battle keeps the exit the player is at as the previous exit (script_interp.c:
  // scriptInstruction64to7E).
  void leaveByFlight() { place_.previousExit = place_.exit; }
  // The player is on `screen` at `exit` (map.c: changeMap).
  void enter(std::int32_t screen, std::int32_t exit);

  [[nodiscard]] const ScreenPlace& place() const { return place_; }

private:
  ScreenHistoryRules rules_;
  ScreenPlace place_;
};

} // namespace dwe::game::story
