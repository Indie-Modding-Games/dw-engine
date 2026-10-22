#include <dwe/assets/text_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <map>
#include <set>
#include <utility>

#include "messages.hpp"

namespace dwe::localization {
namespace {

using assets::TextTable;
using assets::TextTableName;

constexpr char Open = '{';
constexpr char Close = '}';
// The missing keys and tables a warning names; the rest are only counted.
constexpr std::size_t MissingKeysNamed = 5;
constexpr std::size_t MissingTablesNamed = 5;
constexpr std::string_view KeyListSeparator = ", ";
// The source and block of the map names in the original's text (docs/formats/textos.md).
constexpr std::string_view MapNamesSource = "main";
constexpr std::string_view MapNamesBlock = "map_names";
// Whole numbers below this show without decimals.
constexpr double LargestWholeNumber = 1e15;

// The catalog tr reads, set by the app (useCatalog).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): see useCatalog.
const Catalog* currentCatalog = nullptr;

// The tables of one language, by name.
using LanguageTables = std::map<TextTableName, TextTable>;
// The pack that provides each table, by name.
using TablePacks = std::map<TextTableName, std::string>;

core::Result<LanguageTables> readLanguage(const assets::VirtualFileSystem& files,
                                          std::string_view language, TablePacks* packs = nullptr) {
  LanguageTables tables;
  for (const assets::AssetId& id : files.ids()) {
    std::optional<TextTableName> name = assets::textTableOf(id);
    if (!name || name->language != language) {
      continue;
    }
    auto table = assets::readTextTable(files, id, *name);
    if (!table) {
      return core::fail(std::format(messages::UnreadableTable, id.str(), table.error().message));
    }
    if (packs != nullptr) {
      const assets::AssetSource* source = files.resolve(id);
      packs->emplace(*name, source == nullptr ? std::string{} : source->packId);
    }
    tables.emplace(std::move(*name), std::move(*table));
  }
  return tables;
}

TextTable merged(LanguageTables tables) {
  TextTable all;
  for (auto& [name, table] : tables) {
    all.merge(table);
  }
  return all;
}

std::string tableIdText(const TextTableName& name, std::string_view language) {
  const std::string_view folder = name.kind == assets::TextTableKind::Engine
                                      ? assets::EngineTextFolder
                                      : assets::GameTextFolder;
  return std::format("{}/{}/{}", folder, language, name.name);
}

template <typename Text>
std::string nameList(const std::vector<Text>& names, std::size_t named) {
  std::string list;
  for (std::size_t i = 0; i < std::min(names.size(), named); ++i) {
    list += i == 0 ? std::string{names[i]} : std::format("{}{}", KeyListSeparator, names[i]);
  }
  return list;
}

// The tables of one pack that have no translation in the language at all.
struct UntranslatedTables {
  std::vector<std::string> ids;
  std::size_t texts = 0;
};

// What a language misses of the fallback's tables: one line per pack for its whole tables (a
// partial translation leaves hundreds out), then one per table that misses keys.
std::vector<std::string> missingTexts(const LanguageTables& fallback,
                                      const TablePacks& fallbackPacks, const LanguageTables& chosen,
                                      std::string_view language) {
  std::vector<std::string> warnings;
  std::map<std::string, UntranslatedTables> untranslated;
  for (const auto& [name, table] : fallback) {
    const TextTableName translated{
        .kind = name.kind, .language = std::string{language}, .name = name.name};
    const auto found = chosen.find(translated);
    if (found == chosen.end()) {
      const auto pack = fallbackPacks.find(name);
      UntranslatedTables& tables =
          untranslated[pack == fallbackPacks.end() ? std::string{} : pack->second];
      tables.ids.push_back(tableIdText(name, FallbackLanguage));
      tables.texts += table.size();
      continue;
    }
    std::vector<std::string_view> missing;
    for (const auto& [key, text] : table) {
      if (!found->second.contains(key)) {
        missing.push_back(key);
      }
    }
    if (!missing.empty()) {
      warnings.push_back(std::format(messages::MissingKeys, tableIdText(translated, language),
                                     missing.size(), FallbackLanguage, FallbackLanguage,
                                     nameList(missing, MissingKeysNamed)));
    }
  }
  std::vector<std::string> perPack;
  for (const auto& [pack, tables] : untranslated) {
    std::string named = nameList(tables.ids, MissingTablesNamed);
    if (tables.ids.size() > MissingTablesNamed) {
      named += messages::MoreNamed;
    }
    perPack.push_back(std::format(messages::MissingTables, pack, language, tables.ids.size(),
                                  tables.texts, FallbackLanguage, named));
  }
  warnings.insert(warnings.end(), std::make_move_iterator(perPack.begin()),
                  std::make_move_iterator(perPack.end()));
  return warnings;
}

// The tags of the original's text that plain text shows (docs/formats/textos.md): a new line, also
// after a wait, and a colour, which plain text has no way to show.
constexpr std::string_view NewLineTag = "br";
constexpr std::string_view WaitTag = "wait";
constexpr std::string_view ColorTagPrefix = "color:";
constexpr std::string_view NewLine = "\n";

// What plain text shows for a tag no argument fills; nothing when it shows the tag as written.
std::optional<std::string_view> plainTag(std::string_view tag) {
  if (tag == NewLineTag || tag == WaitTag) {
    return NewLine;
  }
  if (tag.starts_with(ColorTagPrefix)) {
    return std::string_view{};
  }
  return std::nullopt;
}

const TextArgument* argumentNamed(std::span<const TextArgument> arguments, std::string_view name) {
  const auto found = std::ranges::find(arguments, name, &TextArgument::name);
  return found == arguments.end() ? nullptr : &*found;
}

} // namespace

Catalog::Catalog(const assets::VirtualFileSystem& files, std::vector<std::string> languages)
    : files_{&files}, languages_{std::move(languages)} {}

core::Result<Catalog> Catalog::load(const assets::VirtualFileSystem& files,
                                    std::string_view language) {
  Catalog catalog{files, availableLanguages(files)};
  if (auto loaded = catalog.setLanguage(language); !loaded) {
    return std::unexpected{loaded.error()};
  }
  return catalog;
}

core::Result<void> Catalog::setLanguage(std::string_view language) {
  for (const std::string_view needed : {language, FallbackLanguage}) {
    if (!std::ranges::contains(languages_, needed)) {
      return core::fail(std::format(messages::NoLanguage, needed, needed));
    }
  }
  TablePacks fallbackPacks;
  auto fallback = readLanguage(*files_, FallbackLanguage, &fallbackPacks);
  if (!fallback) {
    return std::unexpected{fallback.error()};
  }
  LanguageTables chosen;
  if (language != FallbackLanguage) {
    auto read = readLanguage(*files_, language);
    if (!read) {
      return std::unexpected{read.error()};
    }
    chosen = std::move(*read);
  }
  warnings_ = language == FallbackLanguage
                  ? std::vector<std::string>{}
                  : missingTexts(*fallback, fallbackPacks, chosen, language);
  for (const std::string& warning : warnings_) {
    core::logWarning(messages::Warning, warning);
  }
  const std::size_t fallbackKeys = merged(LanguageTables{*fallback}).size();
  chosen_ = merged(std::move(chosen));
  fallback_ = merged(std::move(*fallback));
  language_ = std::string{language};
  ++revision_;
  core::logInfo(messages::Loaded, language_, chosen_.size(), fallbackKeys, FallbackLanguage);
  return {};
}

std::optional<std::string_view> Catalog::find(std::string_view key) const {
  if (const auto found = chosen_.find(key); found != chosen_.end()) {
    return found->second;
  }
  if (const auto found = fallback_.find(key); found != fallback_.end()) {
    return found->second;
  }
  return std::nullopt;
}

std::string Catalog::text(std::string_view key, std::span<const TextArgument> arguments) const {
  const std::optional<std::string_view> found = find(key);
  return found ? formatText(*found, arguments) : std::string{key};
}

std::string formatText(std::string_view text, std::span<const TextArgument> arguments) {
  std::string formatted;
  formatted.reserve(text.size());
  std::size_t at = 0;
  while (at < text.size()) {
    const char c = text[at];
    if ((c == Open || c == Close) && at + 1 < text.size() && text[at + 1] == c) {
      formatted += c;
      at += 2;
      continue;
    }
    const std::size_t close = c == Open ? text.find(Close, at + 1) : std::string_view::npos;
    if (close == std::string_view::npos) {
      formatted += c;
      ++at;
      continue;
    }
    const std::string_view name = text.substr(at + 1, close - at - 1);
    if (const TextArgument* argument = argumentNamed(arguments, name)) {
      formatted += argument->value;
    } else {
      formatted += plainTag(name).value_or(text.substr(at, close - at + 1));
    }
    at = close + 1;
  }
  return formatted;
}

std::string numberText(double value) {
  if (std::isfinite(value) && std::trunc(value) == value && std::abs(value) < LargestWholeNumber) {
    return std::format("{}", static_cast<long long>(value));
  }
  return std::format("{}", value);
}

std::string gameTextKey(std::string_view source, std::string_view block, std::size_t slot) {
  return std::format("{}{}{}", source, assets::TextKeySeparator,
                     assets::gameTextEntryKey(block, slot));
}

std::string mapNameKey(std::int32_t loadingName) {
  if (loadingName < 0) {
    return {};
  }
  return gameTextKey(MapNamesSource, MapNamesBlock, static_cast<std::size_t>(loadingName));
}

std::string addedEntryTextKey(const assets::ContentId& id, std::string_view block) {
  return std::format("{}{}{}{}{}", id.packNamespace(), assets::TextKeySeparator, block,
                     assets::TextKeySeparator, id.name());
}

std::string addedMapNameKey(const assets::ContentId& id) {
  return addedEntryTextKey(id, MapNamesBlock);
}

std::vector<std::string> availableLanguages(const assets::VirtualFileSystem& files) {
  std::set<std::string> languages;
  for (const assets::AssetId& id : files.ids()) {
    if (const auto table = assets::textTableOf(id);
        table && table->kind == assets::TextTableKind::Engine) {
      languages.insert(table->language);
    }
  }
  return {languages.begin(), languages.end()};
}

void useCatalog(const Catalog* catalog) {
  currentCatalog = catalog;
}

std::string tr(std::string_view key, std::span<const TextArgument> arguments) {
  return currentCatalog != nullptr ? currentCatalog->text(key, arguments) : std::string{key};
}

std::string pickLanguage(std::span<const std::string> available, std::string_view systemLanguage) {
  if (std::ranges::contains(available, systemLanguage)) {
    return std::string{systemLanguage};
  }
  return std::string{FallbackLanguage};
}

} // namespace dwe::localization
