#pragma once

#include <dwe/assets/content_id.hpp>
#include <dwe/assets/text_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The player-facing texts in the player's language (docs/architecture/localizacion.md).
namespace dwe::localization {

// The language a missing text falls back to.
inline constexpr std::string_view FallbackLanguage = assets::ReferenceLanguage;

// The value of a placeholder: Catalog::text writes `value` where the text says {name}.
struct TextArgument {
  std::string_view name;
  std::string value;
};

// Every text of a language, read from the mounted packs: the engine's own
// (localization/<language>/*) and the original's (text/<language>/*). A key the language does not
// have shows in English; a key English does not have either shows as the key itself, so the missing
// text is easy to find.
class Catalog {
public:
  // Reads the tables of a language and of English. Fails when either has no engine tables.
  static core::Result<Catalog> load(const assets::VirtualFileSystem& files,
                                    std::string_view language);

  // Reads the tables of another language. On failure the catalog keeps the language it had.
  core::Result<void> setLanguage(std::string_view language);

  // The language the texts are in, such as "en" or "es".
  [[nodiscard]] const std::string& language() const { return language_; }

  // The languages with engine tables in the packs, sorted: the ones a player can choose.
  [[nodiscard]] std::span<const std::string> languages() const { return languages_; }

  // The text of a key with its placeholders filled in (formatText), in the language or in English:
  // plain text for the UI. A renderer that draws the original's tags itself (colours, waits) reads
  // them with find.
  [[nodiscard]] std::string text(std::string_view key,
                                 std::span<const TextArgument> arguments = {}) const;

  // The text of a key as written in its table, in the language or in English; nothing when neither
  // has it.
  [[nodiscard]] std::optional<std::string_view> find(std::string_view key) const;

  // What the last load found missing in the language, which shows in English instead. Also logged
  // as warnings.
  [[nodiscard]] std::span<const std::string> warnings() const { return warnings_; }

  // Counts the language changes, so a cache of texts knows when to read them again.
  [[nodiscard]] std::uint32_t revision() const { return revision_; }

private:
  Catalog(const assets::VirtualFileSystem& files, std::vector<std::string> languages);

  const assets::VirtualFileSystem* files_;
  std::vector<std::string> languages_;
  std::string language_;
  assets::TextTable chosen_;
  assets::TextTable fallback_;
  std::vector<std::string> warnings_;
  std::uint32_t revision_ = 0;
};

// Fills the placeholders of a text: {name} becomes the value of the argument of that name,
// {name:one|other} the word of the two that the argument's count takes (pluralWord), {{ and }}
// become { and }. The layout tags of the original's text become plain text: {br} and {wait} a
// new line, {color:N} nothing. Any other placeholder without an argument stays as written
// ({digimon:pstat=3}), so it shows where it is missing.
[[nodiscard]] std::string formatText(std::string_view text,
                                     std::span<const TextArgument> arguments);

// Whether a count takes the word for one of a plural placeholder ({days:day|days}): exactly 1, the
// rule of the packs' languages (English and Spanish say "1 day" but "0 days" and "1.5 days").
[[nodiscard]] bool takesOne(double count);

// The word a plural placeholder gives for a count as an argument holds it ("1", "3"): its word for
// one when takesOne, else its other word, also for a value that is not a number.
[[nodiscard]] std::string_view pluralWord(const assets::PluralPlaceholder& plural,
                                          std::string_view count);

// A number as a placeholder shows it: without decimals when it is whole ("3"), else in its shortest
// form ("0.75").
[[nodiscard]] std::string numberText(double value);

// The languages with engine tables (localization/<language>/) in the packs, sorted: the ones a
// player can choose.
[[nodiscard]] std::vector<std::string> availableLanguages(const assets::VirtualFileSystem& files);

// Makes a catalog the one tr reads, or none with nullptr. The app sets the one it owns once, before
// any code that shows text runs, and clears it before destroying it; code that only shows text uses
// tr instead of passing a catalog.
void useCatalog(const Catalog* catalog);

// The catalog set with useCatalog, or nullptr: for code that follows the language itself (the
// signs painted in a map follow its language and revision).
[[nodiscard]] const Catalog* catalogInUse();

// The text of a key in the player's language (Catalog::text) of the catalog set with useCatalog;
// the key itself when none is set.
[[nodiscard]] std::string tr(std::string_view key, std::span<const TextArgument> arguments = {});

// The key of a string of the original's text: "<source>.<block>.<slot>", the slot zero-padded
// (docs/formats/textos.md).
[[nodiscard]] std::string gameTextKey(std::string_view source, std::string_view block,
                                      std::size_t slot);

// The key of the name the original shows for a map: slot `loadingName` of main.map_names
// (MapEntry::loadingName). Empty when the map has none.
[[nodiscard]] std::string mapNameKey(std::int32_t loadingName);

// The key of a text of an entry a pack adds, such as the name of `mymod:newmon`:
// "<pack>.<block>.<name>", in the pack's own table of the original's kind of text,
// text/<language>/<pack> (docs/architecture/vfs.md).
[[nodiscard]] std::string addedEntryTextKey(const assets::ContentId& id, std::string_view block);

// The key of the name of a map a pack adds (addedEntryTextKey, block map_names).
[[nodiscard]] std::string addedMapNameKey(const assets::ContentId& id);

// The language of the system's locale ("es" for es_ES) when the packs have it, else English.
[[nodiscard]] std::string pickLanguage(std::span<const std::string> available,
                                       std::string_view systemLanguage);

} // namespace dwe::localization
