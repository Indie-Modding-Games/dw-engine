#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

// A bug report (F12, the report_bug action; docs/guides/prueba-vertical.md): what the game was
// doing when the player pressed it, written to state.yaml next to a save, a screenshot, the log's
// tail and an autotest route that loads the save. This is the report's data and its files' text;
// PlaySession gathers it and BugReporter writes the folder.

// Where a character stands: world position and facing (radians).
struct ReportPose {
  glm::vec3 position{};
  float yaw = 0.0F;
};

// An NPC on the field.
struct ReportNpc {
  std::int32_t scriptId = 0;
  // Its record in the map's NPC list.
  std::size_t slot = 0;
  // The record's species (its type id) and the model it is drawn with.
  std::string species;
  std::string model;
  ReportPose pose;
  // Held still by a script (set_movement), and the animation a script left on it (0: its own).
  bool held = false;
  std::int32_t scriptedAnimation = 0;
  // Moved by a script that holds the field (ScriptedActor).
  bool scripted = false;
};

// A fighter of the battle on screen: the partner first, then the enemies.
struct ReportFighter {
  std::string species;
  // The map NPC an enemy is, by script id; nothing for the partner.
  std::optional<std::int32_t> npc;
  std::int32_t hp = 0;
  std::int32_t maxHp = 0;
  std::int32_t mp = 0;
  std::int32_t maxMp = 0;
  // On the battle's ground, in world units.
  std::int32_t x = 0;
  std::int32_t z = 0;
};

// A value the partner's status page shows (partner.* settings).
struct ReportSetting {
  std::string name;
  std::string value;
};

// The game time.
struct ReportTime {
  std::int32_t year = 0;
  std::int32_t day = 0;
  std::int32_t hour = 0;
  std::int32_t minute = 0;
};

// Which save the report holds.
enum class ReportSaveKind : std::uint8_t {
  // The game as it is now.
  Now,
  // The last moment the field was free (no battle, script or scene), taken some seconds before:
  // the game cannot be saved in the middle of those.
  FieldSnapshot,
  // No save: the game was not saved yet and could not be now.
  None,
};

struct ReportSave {
  ReportSaveKind kind = ReportSaveKind::None;
  // How old a snapshot is, in real seconds.
  double secondsAgo = 0.0;
  // Why the game could not be saved now, or why there is no save.
  std::string reason;
};

struct BugReportState {
  // The source the game was built from (git describe), the game's version and when the report was
  // made (local time).
  std::string commit;
  std::string gameVersion;
  std::string created;
  std::string map;
  ReportTime time;
  // What is on screen: field, battle, or another scene (evolution, death, sleep, gym, shop...).
  std::string screen;
  // The battle document's phase (battle.phase), while a battle is on screen.
  std::string battlePhase;
  bool scriptRunning = false;
  // The running script sections, as "script <id> section <n>".
  std::vector<std::string> scriptSections;
  bool heldByScript = false;
  bool heldForCare = false;
  bool dialogOpen = false;
  bool menuOpen = false;
  bool mapChanging = false;
  ReportPose player;
  ReportPose partner;
  std::vector<ReportNpc> npcs;
  std::vector<ReportFighter> fighters;
  std::string partnerSpecies;
  std::vector<ReportSetting> partnerStatus;
  // How many times two characters stood in each other on this field (Field::overlapEpisodes).
  std::int32_t overlaps = 0;
  ReportSave save;
};

// The report's files, in its folder.
inline constexpr std::string_view ReportStateFile = "state.yaml";
inline constexpr std::string_view ReportSaveFile = "save.yaml";
inline constexpr std::string_view ReportScreenshotFile = "screenshot.png";
inline constexpr std::string_view ReportLogFile = "log.txt";
inline constexpr std::string_view ReportRouteFile = "route.yaml";
// How many of the log's last lines the report keeps.
inline constexpr std::size_t ReportLogLines = 2000;

// The report's folder name: <YYYYMMDD-HHMMSS>-<map>, the map's characters other than letters,
// digits, '-' and '_' made '_'.
[[nodiscard]] std::string bugReportFolderName(const std::tm& local, std::string_view map);

// The text of state.yaml.
[[nodiscard]] core::Result<std::string> bugReportStateYaml(const BugReportState& state);

// The text of route.yaml: an autotest route that loads the report's save (load_save, relative to
// the route) and waits on its map, so `dwe-game --autotest route.yaml` plays the spot again.
[[nodiscard]] core::Result<std::string> bugReportRouteYaml(std::string_view map, bool hasSave);

} // namespace dwe::app
