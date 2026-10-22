#include <dwe/assets/asset_id.hpp>
#include <dwe/ruleset/messages.hpp>
#include <dwe/ruleset/ruleset_values.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::ruleset {

namespace {

using core::YamlNode;

constexpr char PathSeparator = '.';
// inlineYaml writes the value under this key and keeps what follows it.
constexpr std::string_view InlineKey = "v";
constexpr std::string_view InlineKeyPrefix = "v: ";
constexpr char LineEnd = '\n';
constexpr char FolderSeparator = '/';

bool isEmptyContainer(const YamlNode& node) {
  return (node.isSequence() && node.items().empty()) ||
         (node.isMapping() && node.entries().empty());
}

void collectValues(std::string_view section, const YamlNode& node, const std::string& path,
                   std::vector<RulesetValue>& values) {
  if (node.isScalar() || isEmptyContainer(node)) {
    values.push_back(RulesetValue{.section = std::string{section},
                                  .path = path,
                                  .yaml = inlineYaml(node).value_or(std::string{})});
    return;
  }
  if (node.isSequence()) {
    for (std::size_t index = 0; index < node.items().size(); ++index) {
      collectValues(section, node.items()[index], joinPath(path, std::to_string(index)), values);
    }
    return;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    collectValues(section, entry.value, joinPath(path, entry.key), values);
  }
}

// The values of a ruleset keyed by "<section>.<path>", for comparing two of them.
std::map<std::string, const RulesetValue*> valuesByKey(const std::vector<RulesetValue>& values) {
  std::map<std::string, const RulesetValue*> byKey;
  for (const RulesetValue& value : values) {
    byKey.emplace(joinPath(value.section, value.path), &value);
  }
  return byKey;
}

} // namespace

core::Result<std::string> inlineYaml(const YamlNode& value) {
  YamlNode holder = YamlNode::mapping();
  holder.set(std::string{InlineKey}, value);
  auto text = core::emitYaml(holder);
  if (!text) {
    return std::unexpected{text.error()};
  }
  std::string_view line = *text;
  if (line.ends_with(LineEnd)) {
    line.remove_suffix(1);
  }
  if (!line.starts_with(InlineKeyPrefix) || line.contains(LineEnd)) {
    return core::fail(messages::formatMessage(messages::NotOneLine, line));
  }
  line.remove_prefix(InlineKeyPrefix.size());
  return std::string{line};
}

std::string joinPath(std::string_view parent, std::string_view key) {
  if (parent.empty()) {
    return std::string{key};
  }
  std::string path{parent};
  path += PathSeparator;
  path += key;
  return path;
}

std::vector<RulesetValue> listValues(std::string_view section, const YamlNode& document) {
  std::vector<RulesetValue> values;
  collectValues(section, document, {}, values);
  return values;
}

std::vector<RulesetValue> listValues(const Ruleset& ruleset) {
  std::vector<RulesetValue> values;
  for (const auto& [name, section] : ruleset.sections()) {
    collectValues(name, section.node, {}, values);
  }
  return values;
}

std::vector<ValueDifference> diffValues(const Ruleset& before, const Ruleset& after) {
  const std::vector<RulesetValue> beforeValues = listValues(before);
  const std::vector<RulesetValue> afterValues = listValues(after);
  const auto beforeByKey = valuesByKey(beforeValues);
  const auto afterByKey = valuesByKey(afterValues);
  std::vector<ValueDifference> differences;
  const auto differenceOf = [](const RulesetValue& value) {
    return ValueDifference{.section = value.section, .path = value.path, .before = {}, .after = {}};
  };
  for (const auto& [key, value] : beforeByKey) {
    const auto other = afterByKey.find(key);
    if (other == afterByKey.end()) {
      ValueDifference removed = differenceOf(*value);
      removed.before = value->yaml;
      differences.push_back(std::move(removed));
    } else if (other->second->yaml != value->yaml) {
      ValueDifference changed = differenceOf(*value);
      changed.before = value->yaml;
      changed.after = other->second->yaml;
      differences.push_back(std::move(changed));
    }
  }
  for (const auto& [key, value] : afterByKey) {
    if (!beforeByKey.contains(key)) {
      ValueDifference added = differenceOf(*value);
      added.after = value->yaml;
      differences.push_back(std::move(added));
    }
  }
  std::ranges::sort(differences, {}, [](const ValueDifference& difference) {
    return std::pair<const std::string&, const std::string&>{difference.section, difference.path};
  });
  return differences;
}

std::vector<std::string> listRulesets(const assets::VirtualFileSystem& vfs) {
  std::vector<std::string> ids;
  std::string prefix{RulesFolder};
  prefix += FolderSeparator;
  std::string suffix(1, FolderSeparator);
  suffix += ManifestLeaf;
  for (const assets::AssetId& id : vfs.ids()) {
    const std::string_view text = id.str();
    if (text.size() <= prefix.size() + suffix.size() || !text.starts_with(prefix) ||
        !text.ends_with(suffix)) {
      continue;
    }
    const std::string_view rulesetId =
        text.substr(prefix.size(), text.size() - prefix.size() - suffix.size());
    if (!rulesetId.contains(FolderSeparator)) {
      ids.emplace_back(rulesetId);
    }
  }
  std::ranges::sort(ids);
  return ids;
}

} // namespace dwe::ruleset
