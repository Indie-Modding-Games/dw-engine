#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/content_check.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// The tables of player-facing text in the packs (docs/architecture/localizacion.md): one per
// language and per area of the engine (localization/<language>/<area>) or per source of the
// original's text (text/<language>/<source>).
namespace dwe::assets {

// What joins the parts of a text key: <table>.<block>.<slot or name>.
inline constexpr char TextKeySeparator = '.';

// The folders the two kinds of tables live in.
inline constexpr std::string_view EngineTextFolder = "localization";
inline constexpr std::string_view GameTextFolder = "text";

// The language every table is checked against and every missing text falls back to.
inline constexpr std::string_view ReferenceLanguage = "en";

enum class TextTableKind : std::uint8_t {
  Engine, // the engine's own texts, written by the team
  Game,   // the original game's text, written by dwx extract
};

// A table as its asset id names it.
struct TextTableName {
  TextTableKind kind = TextTableKind::Engine;
  std::string language;
  std::string name; // the area of the engine, or the source of the original's text

  friend auto operator<=>(const TextTableName&, const TextTableName&) = default;
};

// The table an asset id is, or nothing for any other asset.
std::optional<TextTableName> textTableOf(const AssetId& id);

// Texts by key. A key is segments of a-z, 0-9 and _ joined by dots. A table file is a flat mapping
// of keys to texts, and the full key starts with the table's name: `title.main: Pause` in
// localization/<language>/pause_menu is pause_menu.title.main, `map_names.0003: File City` in
// text/<language>/main is main.map_names.0003.
using TextTable = std::map<std::string, std::string, std::less<>>;

// The digits of a slot in the key of a string of the original's text.
inline constexpr int GameTextSlotDigits = 4;

// The key of a string of the original's text inside its table: "<block>.<slot>", the slot
// zero-padded
// ("map_names.0003"). The full key adds the source in front (docs/formats/textos.md).
std::string gameTextEntryKey(std::string_view block, std::size_t slot);

// Reads a table: a flat mapping of keys to texts whose placeholders are all closed. A table of the
// original's text may still be in the format dwx extract wrote before (`source` and `strings`, a
// mapping of blocks to lists of strings or nulls), read with the same keys; a null slot has none.
core::Result<TextTable> parseTextTable(const TextTableName& name, std::string_view yaml);

// Reads a table of the stack with every pack's file of it merged, from the bottom of the stack up:
// a key in a higher pack replaces the same key below, and the others stay. So a mod can change one
// text of a table without copying it.
core::Result<TextTable> readTextTable(const VirtualFileSystem& fileSystem, const AssetId& id,
                                      const TextTableName& name);

// The placeholders of a text, sorted and without repeats: what is between { and }, as "count" for
// {count} and "color:2" for a control tag of the original's text. {{ and }} are a literal brace. A
// { without its } is an error.
core::Result<std::vector<std::string>> placeholdersOf(std::string_view text);

// Something a translation does differently from the reference table.
struct TranslationProblem {
  enum class Kind : std::uint8_t {
    MissingKey,          // the reference has the key and the translation does not
    ExtraKey,            // the translation has a key the reference does not
    PlaceholderMismatch, // both have it with different placeholders
  };
  Kind kind = Kind::MissingKey;
  std::string key;
};

// Compares a translation with its reference table, key by key, in key order.
std::vector<TranslationProblem> compareTranslation(const TextTable& reference,
                                                   const TextTable& translation);

// Checks the text tables of a stack against the reference language: every engine table of the
// reference in every language that has engine tables, with the same keys and placeholders; the
// tables of the original's text that a language has, with no key and no placeholder the reference
// does not have (a string they leave out, or a language without them, shows the original's text in
// the reference language); no table without its reference, and no key in two tables of a language.
// Tables that do not parse are left to checkContent.
std::vector<ContentProblem> checkTranslations(const VirtualFileSystem& fileSystem);

// The file beside a pack's translation of the original's text (text/<language>/) that says what
// each string was translated from. Dot files are not assets, so only dwx validate reads it, from
// the pack's folder.
inline constexpr std::string_view SourcesLockFile = ".sources.lock";

// What a translation of the original's text was made from (docs/formats/textos.md).
struct SourcesLock {
  // Full key of a translated string -> the SHA-1 (40 lowercase hex digits) of its English text when
  // it was translated, the text as the table has it, with its tags, in UTF-8.
  std::map<std::string, std::string, std::less<>> sources;
  // Full keys of the strings that stay as in English and are not in the translation.
  std::set<std::string, std::less<>> keptEnglish;
  // Full keys of the strings that are not English words and stay as they are: blanks, letters, the
  // original's leftovers. Optional in the file; checked like keptEnglish.
  std::set<std::string, std::less<>> keptOriginal;
};

core::Result<SourcesLock> parseSourcesLock(std::string_view yaml);

// The SHA-1 of a text as a lock keeps it.
std::string sourceHash(std::string_view text);

// Compares a pack's translation of the original's text with its lock and the English text it was
// made from: a translated key the English text has changed since, a key without its hash, a kept
// key that is translated too, and keys English does not have. The messages, in key order.
std::vector<std::string> compareSourcesLock(const SourcesLock& lock, const TextTable& english,
                                            const TextTable& translation);

// A problem of a lock file.
struct SourcesLockProblem {
  std::string file;
  std::string message;
};

// Reads the lock of every pack that has one (text/<language>/.sources.lock) and compares it with
// the strings that pack translates and the English text of the stack.
std::vector<SourcesLockProblem> checkSourcesLocks(const VirtualFileSystem& fileSystem);

} // namespace dwe::assets
