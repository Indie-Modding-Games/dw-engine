#include <dwe/assets/file_system.hpp>
#include <dwe/assets/text_table.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::assets {
namespace {

using core::YamlNode;

constexpr char KeySeparator = TextKeySeparator;
constexpr char PlaceholderOpen = '{';
constexpr char PlaceholderClose = '}';
constexpr std::string_view PlaceholderListSeparator = ", ";
constexpr std::string_view SourceKey = "source";
constexpr std::string_view StringsKey = "strings";
constexpr std::string_view LockSourcesKey = "sources";
constexpr std::string_view LockKeptKey = "kept_english";
constexpr std::string_view LockKeptOriginalKey = "kept_original";
constexpr std::size_t HashDigits = 40;

std::string joinKey(std::string_view prefix, std::string_view segment) {
  return std::format("{}{}{}", prefix, KeySeparator, segment);
}

bool isKeySegment(std::string_view segment) {
  return !segment.empty() && std::ranges::all_of(segment, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

// A key of a table file: segments joined by dots.
bool isEntryKey(std::string_view key) {
  std::size_t start = 0;
  while (true) {
    const std::size_t dot = key.find(KeySeparator, start);
    if (!isKeySegment(key.substr(start, dot == std::string_view::npos ? std::string_view::npos
                                                                      : dot - start))) {
      return false;
    }
    if (dot == std::string_view::npos) {
      return true;
    }
    start = dot + 1;
  }
}

void readText(YamlReader& reader, const YamlNode& node, std::string key, TextTable& table) {
  if (!node.isScalar() || YamlReader::isNull(node)) {
    reader.fail(node, messages::NotText);
    return;
  }
  std::string text = reader.text(node);
  if (auto placeholders = placeholdersOf(text); !placeholders) {
    reader.fail(node, placeholders.error().message);
  }
  table.insert_or_assign(std::move(key), std::move(text));
}

void readFlatTable(YamlReader& reader, const YamlNode& root, std::string_view name,
                   TextTable& table) {
  for (const core::YamlEntry& entry : root.entries()) {
    if (!isEntryKey(entry.key)) {
      reader.fail(entry.value, std::format(messages::BadKey, entry.key));
      continue;
    }
    readText(reader, entry.value, joinKey(name, entry.key), table);
  }
}

// The format dwx extract wrote before the flat one: `source` and `strings`, blocks of lists of
// strings or nulls.
bool isListedGameTable(const YamlNode& root) {
  const YamlNode* strings = root.find(StringsKey);
  return root.find(SourceKey) != nullptr && strings != nullptr && strings->isMapping();
}

void readListedGameTable(YamlReader& reader, const YamlNode& root, std::string_view source,
                         TextTable& table) {
  if (!reader.expectKeys(root, messages::GameTableWhat, {SourceKey, StringsKey})) {
    return;
  }
  for (const core::YamlEntry& block : reader.field(root, StringsKey).entries()) {
    if (!isKeySegment(block.key)) {
      reader.fail(block.value, std::format(messages::BadKey, block.key));
      continue;
    }
    const auto strings = reader.sequence(block.value, messages::StringsWhat);
    for (std::size_t slot = 0; slot < strings.size(); ++slot) {
      if (!YamlReader::isNull(strings[slot])) {
        readText(reader, strings[slot], joinKey(source, gameTextEntryKey(block.key, slot)), table);
      }
    }
  }
}

// The placeholders of a text, or none when it has a brace without its pair (the reader reports
// that).
std::vector<std::string> placeholderSet(std::string_view text) {
  return placeholdersOf(text).value_or(std::vector<std::string>{});
}

std::string placeholderList(const std::string& text) {
  std::string list;
  for (const std::string& placeholder : placeholderSet(text)) {
    list += list.empty() ? placeholder : std::format("{}{}", PlaceholderListSeparator, placeholder);
  }
  return list;
}

// A table of the stack that parsed.
struct LoadedTable {
  AssetId id;
  std::string file;
  TextTable texts;
};

std::string tableIdText(const TextTableName& name) {
  const std::string_view folder =
      name.kind == TextTableKind::Engine ? EngineTextFolder : GameTextFolder;
  return std::format("{}/{}/{}", folder, name.language, name.name);
}

std::map<TextTableName, LoadedTable> loadTables(const VirtualFileSystem& fileSystem) {
  std::map<TextTableName, LoadedTable> tables;
  for (const AssetId& id : fileSystem.ids()) {
    std::optional<TextTableName> name = textTableOf(id);
    if (!name) {
      continue;
    }
    auto texts = readTextTable(fileSystem, id, *name);
    if (!texts) {
      continue;
    }
    tables.emplace(std::move(*name), LoadedTable{.id = id,
                                                 .file = pathText(fileSystem.resolve(id)->file),
                                                 .texts = std::move(*texts)});
  }
  return tables;
}

std::string describe(const TranslationProblem& problem, const TextTable& reference,
                     const TextTable& translation) {
  switch (problem.kind) {
  case TranslationProblem::Kind::MissingKey:
    return std::format(messages::MissingKey, problem.key, ReferenceLanguage);
  case TranslationProblem::Kind::ExtraKey:
    return std::format(messages::ExtraKey, problem.key, ReferenceLanguage);
  case TranslationProblem::Kind::PlaceholderMismatch:
    break;
  }
  return std::format(messages::PlaceholderMismatch, problem.key,
                     placeholderList(translation.find(problem.key)->second), ReferenceLanguage,
                     placeholderList(reference.find(problem.key)->second));
}

class TranslationCheck {
public:
  explicit TranslationCheck(const VirtualFileSystem& fileSystem) : tables_{loadTables(fileSystem)} {
    for (const auto& [name, table] : tables_) {
      if (name.kind == TextTableKind::Engine) {
        engineLanguages_.insert(name.language);
      }
    }
  }

  std::vector<ContentProblem> run() {
    for (const auto& [name, table] : tables_) {
      if (name.language == ReferenceLanguage) {
        checkTranslated(name, table);
      } else {
        checkAgainstReference(name, table);
      }
      checkUniqueKeys(name, table);
    }
    return std::move(problems_);
  }

private:
  void add(const LoadedTable& table, std::string message) {
    problems_.push_back(
        ContentProblem{.id = table.id, .file = table.file, .message = std::move(message)});
  }

  // An engine table of the reference must be in every language with engine tables.
  void checkTranslated(const TextTableName& name, const LoadedTable& table) {
    if (name.kind != TextTableKind::Engine) {
      return;
    }
    for (const std::string& language : engineLanguages_) {
      const TextTableName translated{.kind = name.kind, .language = language, .name = name.name};
      if (language != ReferenceLanguage && !tables_.contains(translated)) {
        add(table, std::format(messages::MissingTable, language, tableIdText(translated),
                               table.texts.size(), ReferenceLanguage));
      }
    }
  }

  void checkAgainstReference(const TextTableName& name, const LoadedTable& table) {
    const TextTableName referenceName{
        .kind = name.kind, .language = std::string{ReferenceLanguage}, .name = name.name};
    const auto reference = tables_.find(referenceName);
    if (reference == tables_.end()) {
      add(table,
          std::format(messages::NoReferenceTable, ReferenceLanguage, tableIdText(referenceName)));
      return;
    }
    for (const TranslationProblem& problem :
         compareTranslation(reference->second.texts, table.texts)) {
      // A translation of the original's text may leave strings out: they show in the reference
      // language.
      if (name.kind == TextTableKind::Game &&
          problem.kind == TranslationProblem::Kind::MissingKey) {
        continue;
      }
      add(table, describe(problem, reference->second.texts, table.texts));
    }
  }

  void checkUniqueKeys(const TextTableName& name, const LoadedTable& table) {
    auto& owners = keyOwners_[name.language];
    for (const auto& [key, text] : table.texts) {
      const auto [owner, added] = owners.try_emplace(key, table.id.str());
      if (!added) {
        add(table, std::format(messages::KeyInTwoTables, key, owner->second));
      }
    }
  }

  std::map<TextTableName, LoadedTable> tables_;
  std::set<std::string, std::less<>> engineLanguages_;
  // Per language, the table that has each key.
  std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> keyOwners_;
  std::vector<ContentProblem> problems_;
};

void readKeptKeys(YamlReader& reader, const YamlNode& node,
                  std::set<std::string, std::less<>>& keys) {
  for (const YamlNode& kept : reader.sequence(node, messages::LockKeptWhat)) {
    std::string key = reader.text(kept);
    if (!isEntryKey(key)) {
      reader.fail(kept, std::format(messages::BadKey, key));
    }
    keys.insert(std::move(key));
  }
}

bool isHash(std::string_view text) {
  return text.size() == HashDigits && std::ranges::all_of(text, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

std::string lockFileOf(const MountedPack& pack, std::string_view language) {
  return pathText(pack.root / GameTextFolder / language / SourcesLockFile);
}

// The strings of the original's text in a language: of the whole stack, or of one pack only.
TextTable gameTexts(const VirtualFileSystem& fileSystem, std::string_view language,
                    const MountedPack* onlyPack) {
  TextTable all;
  for (const AssetId& id : fileSystem.ids()) {
    const std::optional<TextTableName> name = textTableOf(id);
    if (!name || name->kind != TextTableKind::Game || name->language != language) {
      continue;
    }
    if (onlyPack == nullptr) {
      if (auto texts = readTextTable(fileSystem, id, *name)) {
        all.merge(*texts);
      }
      continue;
    }
    for (const AssetSource& source : fileSystem.sources(id)) {
      if (source.packId != onlyPack->manifest.id) {
        continue;
      }
      const auto bytes = onlyPack->fileSystem->readFile(source.file);
      if (!bytes) {
        continue;
      }
      if (auto texts =
              parseTextTable(*name, std::string_view{reinterpret_cast<const char*>(bytes->data()),
                                                     bytes->size()})) {
        all.merge(*texts);
      }
    }
  }
  return all;
}

// The languages with tables of the original's text, but English.
std::set<std::string, std::less<>> translatedLanguages(const VirtualFileSystem& fileSystem) {
  std::set<std::string, std::less<>> languages;
  for (const AssetId& id : fileSystem.ids()) {
    const std::optional<TextTableName> name = textTableOf(id);
    if (name && name->kind == TextTableKind::Game && name->language != ReferenceLanguage) {
      languages.insert(name->language);
    }
  }
  return languages;
}

} // namespace

core::Result<SourcesLock> parseSourcesLock(std::string_view yaml) {
  const std::string context = std::format(messages::LockContext, SourcesLockFile);
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return core::fail(std::format(messages::InFile, context, tree.error().message));
  }
  YamlReader reader;
  SourcesLock lock;
  if (reader.expectKeys(*tree, messages::LockWhat, {LockSourcesKey, LockKeptKey},
                        {LockKeptOriginalKey})) {
    const YamlNode& sources = reader.field(*tree, LockSourcesKey);
    const std::span<const core::YamlEntry> entries =
        sources.isMapping() ? sources.entries() : std::span<const core::YamlEntry>{};
    if (!sources.isMapping()) {
      reader.fail(sources, messages::NotMapping);
    }
    for (const core::YamlEntry& entry : entries) {
      if (!isEntryKey(entry.key)) {
        reader.fail(entry.value, std::format(messages::BadKey, entry.key));
      }
      std::string hash = reader.text(entry.value);
      if (!isHash(hash)) {
        reader.fail(entry.value, std::format(messages::BadHash, hash));
      }
      lock.sources.insert_or_assign(entry.key, std::move(hash));
    }
    readKeptKeys(reader, reader.field(*tree, LockKeptKey), lock.keptEnglish);
    if (const YamlNode* keptOriginal = tree->find(LockKeptOriginalKey)) {
      readKeptKeys(reader, *keptOriginal, lock.keptOriginal);
    }
  }
  return reader.finish(std::move(lock), context);
}

std::string sourceHash(std::string_view text) {
  return core::toHex(core::sha1(std::as_bytes(std::span{text})));
}

std::vector<std::string> compareSourcesLock(const SourcesLock& lock, const TextTable& english,
                                            const TextTable& translation) {
  std::multimap<std::string, std::string, std::less<>>
      problems; // by key, so they come out in key order
  const auto add = [&problems](std::string_view key, std::string message) {
    problems.emplace(std::string{key}, std::move(message));
  };
  for (const auto& [key, hash] : lock.sources) {
    const auto source = english.find(key);
    if (source == english.end()) {
      add(key, std::format(messages::NotInEnglish, key));
    } else if (const std::string now = sourceHash(source->second); now != hash) {
      add(key, std::format(messages::StaleTranslation, key, now));
    }
  }
  for (const auto* kept : {&lock.keptEnglish, &lock.keptOriginal}) {
    for (const std::string& key : *kept) {
      if (!english.contains(key)) {
        add(key, std::format(messages::NotInEnglish, key));
      } else if (translation.contains(key)) {
        add(key, std::format(messages::KeptAndTranslated, key));
      }
    }
  }
  for (const auto& [key, text] : translation) {
    // A kept key that is translated is reported above.
    const bool kept = lock.keptEnglish.contains(key) || lock.keptOriginal.contains(key);
    if (!lock.sources.contains(key) && !kept && english.contains(key)) {
      add(key, std::format(messages::UnhashedTranslation, key));
    }
  }
  std::vector<std::string> sorted;
  sorted.reserve(problems.size());
  for (auto& [key, message] : problems) {
    sorted.push_back(std::move(message));
  }
  return sorted;
}

std::vector<SourcesLockProblem> checkSourcesLocks(const VirtualFileSystem& fileSystem) {
  std::vector<SourcesLockProblem> problems;
  const std::set<std::string, std::less<>> languages = translatedLanguages(fileSystem);
  if (languages.empty()) {
    return problems;
  }
  const TextTable english = gameTexts(fileSystem, ReferenceLanguage, nullptr);
  for (const MountedPack& pack : fileSystem.packs()) {
    for (const std::string& language : languages) {
      const std::string file = lockFileOf(pack, language);
      const auto bytes =
          pack.fileSystem->readFile(pack.root / GameTextFolder / language / SourcesLockFile);
      if (!bytes) {
        continue; // a translation without a lock is not checked for stale strings
      }
      const auto lock = parseSourcesLock(
          std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
      if (!lock) {
        problems.push_back({.file = file, .message = lock.error().message});
        continue;
      }
      for (std::string& message :
           compareSourcesLock(*lock, english, gameTexts(fileSystem, language, &pack))) {
        problems.push_back({.file = file, .message = std::move(message)});
      }
    }
  }
  return problems;
}

core::Result<TextTable> readTextTable(const VirtualFileSystem& fileSystem, const AssetId& id,
                                      const TextTableName& name) {
  TextTable merged;
  for (const AssetSource& source : fileSystem.sources(id)) {
    const auto pack =
        std::ranges::find(fileSystem.packs(), source.packId,
                          [](const MountedPack& mounted) { return mounted.manifest.id; });
    if (pack == fileSystem.packs().end()) {
      return core::fail(std::format(messages::NoPackOfFile, pathText(source.file), source.packId));
    }
    auto bytes = pack->fileSystem->readFile(source.file);
    if (!bytes) {
      return core::fail(
          std::format(messages::InFile, pathText(source.file), bytes.error().message));
    }
    auto texts = parseTextTable(
        name, std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    if (!texts) {
      return core::fail(
          std::format(messages::InFile, pathText(source.file), texts.error().message));
    }
    for (auto& [key, text] : *texts) {
      merged.insert_or_assign(key, std::move(text));
    }
  }
  return merged;
}

std::optional<TextTableName> textTableOf(const AssetId& id) {
  const std::string& text = id.str();
  const std::size_t first = text.find('/');
  const std::size_t second =
      first == std::string::npos ? std::string::npos : text.find('/', first + 1);
  if (second == std::string::npos || text.find('/', second + 1) != std::string::npos) {
    return std::nullopt;
  }
  const std::string_view folder = std::string_view{text}.substr(0, first);
  TextTableName name{.kind = TextTableKind::Engine,
                     .language = text.substr(first + 1, second - first - 1),
                     .name = text.substr(second + 1)};
  if (folder == GameTextFolder) {
    name.kind = TextTableKind::Game;
  } else if (folder != EngineTextFolder) {
    return std::nullopt;
  }
  return name;
}

std::string gameTextEntryKey(std::string_view block, std::size_t slot) {
  return std::format("{}{}{:0{}}", block, KeySeparator, slot, GameTextSlotDigits);
}

core::Result<TextTable> parseTextTable(const TextTableName& name, std::string_view yaml) {
  const std::string context = std::format(messages::TableContext, tableIdText(name));
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return core::fail(std::format("{}: {}", context, tree.error().message));
  }
  YamlReader reader;
  TextTable table;
  if (!tree->isMapping()) {
    reader.fail(*tree, messages::NotMapping);
  } else if (name.kind == TextTableKind::Game && isListedGameTable(*tree)) {
    readListedGameTable(reader, *tree, name.name, table);
  } else {
    readFlatTable(reader, *tree, name.name, table);
  }
  return reader.finish(std::move(table), context);
}

core::Result<std::vector<std::string>> placeholdersOf(std::string_view text) {
  std::set<std::string, std::less<>> found;
  std::size_t at = 0;
  while (at < text.size()) {
    const char c = text[at];
    const bool doubled = at + 1 < text.size() && text[at + 1] == c;
    if ((c == PlaceholderOpen || c == PlaceholderClose) && doubled) {
      at += 2;
      continue;
    }
    if (c == PlaceholderClose) {
      return core::fail(std::format(messages::LoneClosingBrace, at));
    }
    if (c != PlaceholderOpen) {
      ++at;
      continue;
    }
    const std::size_t close = text.find(PlaceholderClose, at + 1);
    if (close == std::string_view::npos) {
      return core::fail(std::format(messages::UnclosedPlaceholder, at));
    }
    found.emplace(text.substr(at + 1, close - at - 1));
    at = close + 1;
  }
  return std::vector<std::string>{std::make_move_iterator(found.begin()),
                                  std::make_move_iterator(found.end())};
}

std::vector<TranslationProblem> compareTranslation(const TextTable& reference,
                                                   const TextTable& translation) {
  std::vector<TranslationProblem> problems;
  for (const auto& [key, text] : reference) {
    const auto translated = translation.find(key);
    if (translated == translation.end()) {
      problems.push_back({.kind = TranslationProblem::Kind::MissingKey, .key = key});
    } else if (placeholderSet(text) != placeholderSet(translated->second)) {
      problems.push_back({.kind = TranslationProblem::Kind::PlaceholderMismatch, .key = key});
    }
  }
  for (const auto& [key, text] : translation) {
    if (!reference.contains(key)) {
      problems.push_back({.kind = TranslationProblem::Kind::ExtraKey, .key = key});
    }
  }
  std::ranges::stable_sort(problems, {}, &TranslationProblem::key);
  return problems;
}

std::vector<ContentProblem> checkTranslations(const VirtualFileSystem& fileSystem) {
  return TranslationCheck{fileSystem}.run();
}

} // namespace dwe::assets
