#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/save/save_file.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <utility>

#include "keys.hpp"
#include "messages.hpp"

namespace dwe::game::save {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view True = "true";

// Times are ISO 8601 in UTC, formatted and read by hand: std::chrono::parse is not in every
// standard library of the CI. In the shape, each lowercase letter stands for a digit.
constexpr std::string_view TimeFormat = "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z";
constexpr std::string_view TimeShape = "yyyy-mo-ddThh:mi:ssZ";
constexpr std::string_view YearField = "yyyy";
constexpr std::string_view MonthField = "mo";
constexpr std::string_view DayField = "dd";
constexpr std::string_view HourField = "hh";
constexpr std::string_view MinuteField = "mi";
constexpr std::string_view SecondField = "ss";

YamlNode quoted(std::string text) {
  return YamlNode::scalar(std::move(text), YamlNode::ScalarStyle::Quoted);
}

YamlNode plain(std::string text) {
  return YamlNode::scalar(std::move(text));
}

bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

std::string formatTime(std::chrono::sys_seconds time) {
  const auto day = std::chrono::floor<std::chrono::days>(time);
  const std::chrono::year_month_day date{day};
  const std::chrono::hh_mm_ss clock{time - day};
  return std::format(TimeFormat, static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
                     static_cast<unsigned>(date.day()), clock.hours().count(),
                     clock.minutes().count(), clock.seconds().count());
}

bool matchesTimeShape(std::string_view text) {
  return std::ranges::equal(text, TimeShape, [](char c, char shape) {
    const bool digit = shape >= 'a' && shape <= 'z';
    return digit ? isDigit(c) : c == shape;
  });
}

// The number at the place of a field of TimeShape, in a text that matches it.
int timeField(std::string_view text, std::string_view field) {
  const std::string_view digits = text.substr(TimeShape.find(field), field.size());
  int value = 0;
  std::from_chars(digits.data(), digits.data() + digits.size(), value);
  return value;
}

std::optional<std::chrono::sys_seconds> parseTime(std::string_view text) {
  if (!matchesTimeShape(text)) {
    return std::nullopt;
  }
  const std::chrono::year_month_day date{
      std::chrono::year{timeField(text, YearField)},
      std::chrono::month{static_cast<unsigned>(timeField(text, MonthField))},
      std::chrono::day{static_cast<unsigned>(timeField(text, DayField))}};
  const std::chrono::hours hour{timeField(text, HourField)};
  const std::chrono::minutes minute{timeField(text, MinuteField)};
  const std::chrono::seconds second{timeField(text, SecondField)};
  if (!date.ok() || hour >= std::chrono::days{1} || minute >= std::chrono::hours{1} ||
      second >= std::chrono::minutes{1}) {
    return std::nullopt;
  }
  return std::chrono::sys_days{date} + hour + minute + second;
}

std::chrono::sys_seconds readTime(YamlReader& reader, const YamlNode& node) {
  const std::string text = reader.text(node);
  const auto time = parseTime(text);
  if (reader.ok() && !time) {
    reader.fail(node, messages::BadTime);
  }
  return time.value_or(std::chrono::sys_seconds{});
}

std::vector<std::string> readAutosaveReasons(YamlReader& reader, const YamlNode& node) {
  std::vector<std::string> names;
  for (const YamlNode& item : reader.sequence(node, messages::AutosaveReasonsWhat)) {
    std::string name = reader.text(item);
    if (reader.ok() && !isValidName(name)) {
      reader.fail(item, std::format(messages::BadAutosaveReason, name));
    }
    names.push_back(std::move(name));
  }
  return names;
}

SaveSummary readSummary(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::SummaryWhat,
                         {summary_key::Slot, summary_key::Tamer, summary_key::Species,
                          summary_key::Nickname, summary_key::Map})) {
    return {};
  }
  return {.slot = reader.text(reader.field(node, summary_key::Slot)),
          .tamer = reader.text(reader.field(node, summary_key::Tamer)),
          .species = reader.text(reader.field(node, summary_key::Species)),
          .nickname = reader.text(reader.field(node, summary_key::Nickname)),
          .map = reader.text(reader.field(node, summary_key::Map))};
}

SaveHeader readHeader(YamlReader& reader, const YamlNode& root) {
  SaveHeader header{.gameVersion = reader.text(reader.field(root, save_key::GameVersion)),
                    .rulesetId = reader.text(reader.field(root, save_key::Ruleset)),
                    .created = readTime(reader, reader.field(root, save_key::Created)),
                    .updated = readTime(reader, reader.field(root, save_key::Updated)),
                    .playSeconds =
                        reader.integer<std::int64_t>(reader.field(root, save_key::PlaySeconds)),
                    .label = reader.text(reader.field(root, save_key::Label)),
                    .summary = readSummary(reader, reader.field(root, save_key::Summary)),
                    .autosaveReasons =
                        readAutosaveReasons(reader, reader.field(root, save_key::AutosaveReasons))};
  if (reader.ok() && header.playSeconds < 0) {
    reader.fail(reader.field(root, save_key::PlaySeconds), messages::NegativePlayTime);
  }
  return header;
}

std::optional<SavedSection> readSection(YamlReader& reader, std::string_view name,
                                        const YamlNode& node) {
  if (!isValidName(name)) {
    reader.fail(node, std::format(messages::BadSectionName, name));
    return std::nullopt;
  }
  if (!reader.expectKeys(node, std::format(messages::SectionWhat, name),
                         {save_key::Version, save_key::State})) {
    return std::nullopt;
  }
  const YamlNode& version = reader.field(node, save_key::Version);
  SavedSection section{.version = reader.integer<std::int32_t>(version),
                       .state = reader.field(node, save_key::State)};
  if (reader.ok() && section.version < 1) {
    reader.fail(version, messages::BadSectionVersion);
  }
  return reader.ok() ? std::optional{std::move(section)} : std::nullopt;
}

void readSections(YamlReader& reader, const YamlNode& node, SaveFile& file) {
  if (!node.isMapping()) {
    reader.fail(node, messages::SectionsNotMapping);
    return;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    auto section = readSection(reader, entry.key, entry.value);
    if (!section) {
      return;
    }
    file.sections.emplace(entry.key, std::move(*section));
  }
}

void checkComplete(YamlReader& reader, const YamlNode& root) {
  const YamlNode& complete = reader.field(root, save_key::Complete);
  if (!reader.boolean(complete) && reader.ok()) {
    reader.fail(complete, messages::NotComplete);
  }
}

bool isSave(const YamlNode& tree) {
  const YamlNode* format = tree.isMapping() ? tree.find(save_key::Format) : nullptr;
  return format != nullptr && format->isScalar() && format->text() == FormatName;
}

YamlNode writeAutosaveReasons(const SaveHeader& header) {
  YamlNode names = YamlNode::sequence();
  for (const std::string& name : header.autosaveReasons) {
    names.append(plain(name));
  }
  return names;
}

// Format 2 adds the header's autosave_reasons; a format 1 save was never an autosave with reasons.
core::Result<YamlNode> addAutosaveReasons(YamlNode root) {
  root.set(std::string{save_key::AutosaveReasons}, YamlNode::sequence());
  return root;
}

YamlNode writeSummary(const SaveSummary& summary) {
  YamlNode node = YamlNode::mapping();
  node.set(std::string{summary_key::Slot}, quoted(summary.slot));
  node.set(std::string{summary_key::Tamer}, quoted(summary.tamer));
  node.set(std::string{summary_key::Species}, quoted(summary.species));
  node.set(std::string{summary_key::Nickname}, quoted(summary.nickname));
  node.set(std::string{summary_key::Map}, quoted(summary.map));
  return node;
}

// Format 3 adds the header's summary. packages/game/save does not know the sections it would come
// from, so a format 2 save gets an empty one; the game can fill it from the sections
// (session::summaryFromSections) and does on its next save.
core::Result<YamlNode> addSummary(YamlNode root) {
  root.set(std::string{save_key::Summary}, writeSummary({}));
  return root;
}

YamlNode writeSections(const SaveFile& file) {
  YamlNode sections = YamlNode::mapping();
  for (const auto& [name, section] : file.sections) {
    YamlNode entry = YamlNode::mapping();
    entry.set(std::string{save_key::Version}, plain(std::to_string(section.version)));
    entry.set(std::string{save_key::State}, section.state);
    sections.set(name, std::move(entry));
  }
  return sections;
}

} // namespace

bool isValidName(std::string_view name) {
  return !name.empty() && name.size() <= MaxNameLength && std::ranges::all_of(name, [](char c) {
    return (c >= 'a' && c <= 'z') || isDigit(c) || c == '_';
  });
}

const MigrationChain& formatMigrations() {
  // A change of the header or of how sections are stored adds a step here.
  static const MigrationChain chain = [] {
    MigrationChain steps;
    steps.add(addAutosaveReasons); // 1 -> 2
    steps.add(addSummary);         // 2 -> 3
    return steps;
  }();
  return chain;
}

core::Result<SaveFile> parseSaveFile(std::string_view text) {
  return parseSaveFile(text, formatMigrations());
}

core::Result<SaveFile> parseSaveFile(std::string_view text, const MigrationChain& format) {
  auto tree = core::parseYaml(text);
  if (!tree) {
    return core::fail(std::format(messages::InSaveFile, tree.error().message));
  }
  if (!isSave(*tree)) {
    return core::fail(std::format(messages::NotASave, FormatName));
  }
  YamlReader reader;
  const auto version = reader.integer<std::int32_t>(reader.field(*tree, save_key::FormatVersion));
  if (!reader.ok()) {
    return std::unexpected{reader.error(messages::SaveFileContext)};
  }
  auto root = format.migrate(std::move(*tree), version, messages::FormatWhat);
  if (!root) {
    return core::fail(std::format(messages::InSaveFile, root.error().message));
  }
  if (!reader.expectKeys(*root, messages::SaveWhat,
                         {save_key::Format, save_key::FormatVersion, save_key::GameVersion,
                          save_key::Ruleset, save_key::Label, save_key::Summary, save_key::Created,
                          save_key::Updated, save_key::PlaySeconds, save_key::AutosaveReasons,
                          save_key::Sections, save_key::Complete})) {
    return std::unexpected{reader.error(messages::SaveFileContext)};
  }
  checkComplete(reader, *root);
  SaveFile file{.header = readHeader(reader, *root), .sections = {}};
  if (reader.ok()) {
    readSections(reader, reader.field(*root, save_key::Sections), file);
  }
  return reader.finish(std::move(file), messages::SaveFileContext);
}

bool isNewerSave(std::string_view text) {
  auto tree = core::parseYaml(text);
  if (!tree || !isSave(*tree)) {
    return false;
  }
  YamlReader reader;
  const auto version = reader.integer<std::int32_t>(reader.field(*tree, save_key::FormatVersion));
  return reader.ok() && version > formatMigrations().current();
}

core::Result<std::string> writeSaveFile(const SaveFile& file) {
  for (const std::string& name : file.header.autosaveReasons) {
    if (!isValidName(name)) {
      return core::fail(
          std::format(messages::InSaveFile, std::format(messages::BadAutosaveReason, name)));
    }
  }
  for (const auto& [name, section] : file.sections) {
    if (!isValidName(name)) {
      return core::fail(
          std::format(messages::InSaveFile, std::format(messages::BadSectionName, name)));
    }
  }
  YamlNode root = YamlNode::mapping();
  root.set(std::string{save_key::Format}, plain(std::string{FormatName}));
  root.set(std::string{save_key::FormatVersion}, plain(std::to_string(FormatVersion)));
  root.set(std::string{save_key::GameVersion}, quoted(file.header.gameVersion));
  root.set(std::string{save_key::Ruleset}, quoted(file.header.rulesetId));
  root.set(std::string{save_key::Label}, quoted(file.header.label));
  root.set(std::string{save_key::Summary}, writeSummary(file.header.summary));
  root.set(std::string{save_key::Created}, quoted(formatTime(file.header.created)));
  root.set(std::string{save_key::Updated}, quoted(formatTime(file.header.updated)));
  root.set(std::string{save_key::PlaySeconds}, plain(std::to_string(file.header.playSeconds)));
  root.set(std::string{save_key::AutosaveReasons}, writeAutosaveReasons(file.header));
  root.set(std::string{save_key::Sections}, writeSections(file));
  root.set(std::string{save_key::Complete}, plain(std::string{True}));
  auto text = core::emitYaml(root);
  if (!text) {
    return core::fail(std::format(messages::InSaveFile, text.error().message));
  }
  return text;
}

} // namespace dwe::game::save
