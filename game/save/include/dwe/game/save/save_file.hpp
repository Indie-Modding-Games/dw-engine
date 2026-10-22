#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/save/migration.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::save {

// The version of the file layout (the header keys and how sections are stored), written as
// `format_version`. Each section has its own version on top of this one
// (docs/architecture/save.md).
inline constexpr std::int32_t FormatVersion = 2;

// The value of the `format` key, which tells a save apart from any other YAML document.
inline constexpr std::string_view FormatName = "dwe-save";

// The longest section name.
inline constexpr std::size_t MaxNameLength = 64;

// What a save says about itself, readable without reading its sections: enough to list the slots.
struct SaveHeader {
  // The game that wrote it (the app's version), for the person and for bug reports.
  std::string gameVersion;
  // The ruleset the game was played with.
  std::string rulesetId;
  // When the playthrough started and when it was last saved, in UTC. The caller gives them: the
  // module never reads a clock, so tests and replays are deterministic.
  std::chrono::sys_seconds created;
  std::chrono::sys_seconds updated;
  // Time played, in seconds.
  std::int64_t playSeconds = 0;
  // A text for the slot list, such as the map and the partner's name.
  std::string label;
  // Why an autosave was written, by reason name (autosaveReasonNames); empty for a save the player
  // asked for. Kept as names so that a reason from a newer game still lists.
  std::vector<std::string> autosaveReasons;

  friend bool operator==(const SaveHeader&, const SaveHeader&) = default;
};

// The saved state of one system, at the version it was written with.
struct SavedSection {
  std::int32_t version = 1;
  core::YamlNode state = core::YamlNode::mapping();
};

// A whole save in memory: the header and every section by name, including sections no system of
// this game knows (from a mod or a newer game), which are kept as they are and written back
// unchanged.
struct SaveFile {
  SaveHeader header;
  std::map<std::string, SavedSection, std::less<>> sections;
};

// The migrations of the file format (FormatVersion is their current version).
const MigrationChain& formatMigrations();

// Reads a save file and brings its header up to the current format with formatMigrations().
// Sections are kept at the version they were written with: SaveSections::read migrates them. Fails,
// saying where, on a file that is not a save, is corrupt or truncated, or was written by a newer
// game.
core::Result<SaveFile> parseSaveFile(std::string_view text);

// As above with other format migrations; for tests.
core::Result<SaveFile> parseSaveFile(std::string_view text, const MigrationChain& format);

// Writes a save file. The same SaveFile always gives the same text, and a file read and written
// again is byte-identical.
core::Result<std::string> writeSaveFile(const SaveFile& file);

// Whether a name can name a section or an autosave reason: 1 to 64 characters `a-z`, `0-9` and `_`.
[[nodiscard]] bool isValidName(std::string_view name);

} // namespace dwe::game::save
