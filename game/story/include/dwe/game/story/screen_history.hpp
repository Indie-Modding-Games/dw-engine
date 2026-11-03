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

// How the player came onto a screen (the original's map id): through its exit `exit` after a walk
// through warp `walkedWarp` of the screen left (tamer.c: checkMapInteraction) or after a script's
// map change (map.c: scriptTickChangeMap), or with no exit at all: a loaded game or a start at a
// place.
struct ScreenArrival {
  std::int32_t screen = 0;
  std::optional<std::int32_t> exit;
  std::optional<std::int32_t> walkedWarp;
};

class ScreenHistory {
public:
  // No screen of the original has this id.
  static constexpr std::int32_t NoScreen = -1;
  static constexpr ScreenPlace NoPlace{
      .screen = NoScreen, .exit = 0, .previousScreen = NoScreen, .previousExit = 0};

  explicit ScreenHistory(ScreenHistoryRules rules = {}) : rules_{std::move(rules)} {}

  // A new game starts with both screens on the first map at its exit (map.c: initializeMap).
  void start(std::int32_t screen, std::int32_t exit);
  // A loaded game puts back what its save kept (main.c: the continue path).
  void restore(const ScreenPlace& place) { place_ = place; }
  // A loaded game whose save kept no place: the next arrival starts over on its screen.
  void forget() { place_ = NoPlace; }
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

  // Follows a map change the field played: the exit it left by, then the screen it is on. An
  // arrival with no exit keeps the place when it is on that screen (a save put it back) and starts
  // over on it otherwise; the first arrival with no place kept starts on it too.
  void arrive(const ScreenArrival& arrival);

  [[nodiscard]] const ScreenPlace& place() const { return place_; }

private:
  ScreenHistoryRules rules_;
  // Until a new game starts, a save puts a place back or the field enters a map: no screen.
  ScreenPlace place_ = NoPlace;
};

} // namespace dwe::game::story
