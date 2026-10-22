#include <dwe/assets/content_id.hpp>
#include <dwe/assets/entry_class.hpp>
#include <dwe/assets/messages.hpp>

#include <string>
#include <utility>

namespace dwe::assets {

namespace {

constexpr std::string_view FromKey = "from";
constexpr std::string_view ToKey = "to";

std::string readId(YamlReader& reader, const core::YamlNode& node) {
  std::string id = reader.text(node);
  if (reader.ok()) {
    if (auto parsed = ContentId::parse(id); !parsed) {
      reader.fail(node, parsed.error().message);
    }
  }
  return id;
}

std::int32_t readNumber(YamlReader& reader, const core::YamlNode& node) {
  const auto number = reader.integer<std::int32_t>(node);
  if (reader.ok() && number < 0) {
    reader.fail(node, messages::formatMessage(messages::EntryRangeNegative, number));
  }
  return number;
}

} // namespace

void readEntryList(YamlReader& reader, const core::YamlNode& node, std::string_view what,
                   EntryClass& into) {
  for (const core::YamlNode& item : reader.sequence(node, what)) {
    if (item.isScalar()) {
      into.ids.push_back(readId(reader, item));
      continue;
    }
    if (!reader.expectKeys(item, messages::EntryRangeText, {FromKey, ToKey})) {
      return;
    }
    const IndexRange range{.from = readNumber(reader, reader.field(item, FromKey)),
                           .to = readNumber(reader, reader.field(item, ToKey))};
    if (reader.ok() && range.to < range.from) {
      reader.fail(item,
                  messages::formatMessage(messages::EntryRangeReversed, range.from, range.to));
    }
    into.ranges.push_back(range);
  }
}

void readEntryIds(YamlReader& reader, const core::YamlNode& node, std::string_view what,
                  EntryClass& into) {
  for (const core::YamlNode& item : reader.sequence(node, what)) {
    into.ids.push_back(readId(reader, item));
  }
}

core::Error entryClassNumberError(std::string_view rule, std::int32_t number,
                                  std::string_view what) {
  return core::Error{
      messages::formatMessage(messages::EntryClassNumberUnknown, rule, number, what)};
}

core::Error entryClassIdError(std::string_view rule, std::string_view id, std::string_view what) {
  return core::Error{messages::formatMessage(messages::EntryClassIdUnknown, rule, id, what)};
}

} // namespace dwe::assets
