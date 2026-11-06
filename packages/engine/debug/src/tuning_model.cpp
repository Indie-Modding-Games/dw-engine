#include "tuning_model.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/number_text.hpp>
#include <dwe/debug/messages.hpp>
#include <dwe/ruleset/section_file.hpp>

#include <charconv>
#include <cstddef>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace dwe::debug {

namespace {

using core::YamlNode;

constexpr std::string_view Blanks = " \t";
constexpr std::string_view GameTimeFormat = "year {}, day {}, {:02}:{:02}";

std::string_view trimmed(std::string_view text) {
  const std::size_t first = text.find_first_not_of(Blanks);
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(Blanks) - first + 1);
}

bool isNumber(std::string_view text) {
  double value = 0.0;
  const auto [stop, error] = core::fromChars(text.data(), text.data() + text.size(), value);
  return !text.empty() && error == std::errc{} && stop == text.data() + text.size();
}

bool isFlag(std::string_view text) {
  return text == FlagOnText || text == FlagOffText;
}

TuningValueKind kindOf(const YamlNode& scalar) {
  if (scalar.style() == YamlNode::ScalarStyle::Quoted) {
    return TuningValueKind::Text;
  }
  if (isFlag(scalar.text())) {
    return TuningValueKind::Flag;
  }
  return isNumber(scalar.text()) ? TuningValueKind::Number : TuningValueKind::Text;
}

TuningNode buildNode(const YamlNode& node, std::string key, std::string path) {
  TuningNode built{.key = std::move(key),
                   .path = std::move(path),
                   .kind = TuningValueKind::Group,
                   .text = {},
                   .quoted = false,
                   .children = {}};
  if (node.isScalar()) {
    built.kind = kindOf(node);
    built.text = node.text();
    built.quoted = node.style() == YamlNode::ScalarStyle::Quoted;
    return built;
  }
  if (node.isSequence()) {
    for (std::size_t index = 0; index < node.items().size(); ++index) {
      std::string itemKey = std::to_string(index);
      std::string itemPath = ruleset::joinPath(built.path, itemKey);
      built.children.push_back(
          buildNode(node.items()[index], std::move(itemKey), std::move(itemPath)));
    }
    return built;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    built.children.push_back(
        buildNode(entry.value, entry.key, ruleset::joinPath(built.path, entry.key)));
  }
  return built;
}

} // namespace

TuningNode buildTuningTree(const YamlNode& document) {
  return buildNode(document, {}, {});
}

core::Result<std::string> yamlOfEdit(const TuningNode& node, std::string_view input) {
  switch (node.kind) {
  case TuningValueKind::Number: {
    const std::string_view number = trimmed(input);
    if (!isNumber(number)) {
      return core::fail(messages::formatMessage(messages::NotANumber, node.path, input));
    }
    return std::string{number};
  }
  case TuningValueKind::Flag: {
    const std::string_view flag = trimmed(input);
    if (!isFlag(flag)) {
      return core::fail(messages::formatMessage(messages::NotAFlag, node.path, input));
    }
    return std::string{flag};
  }
  case TuningValueKind::Text:
    return ruleset::inlineYaml(
        YamlNode::scalar(std::string{input}, node.quoted ? YamlNode::ScalarStyle::Quoted
                                                         : YamlNode::ScalarStyle::Plain));
  case TuningValueKind::Group:
    break;
  }
  return core::fail(messages::formatMessage(messages::NotAValue, node.path));
}

std::string describeTime(const ruleset::GameTime& time) {
  return std::format(GameTimeFormat, time.year, time.day, time.hour, time.minute);
}

TuningModel::TuningModel(const TuningTarget& target) : target_(target) {
  refreshRulesets();
}

void TuningModel::refreshRulesets() {
  rulesets_ = ruleset::listRulesets(*target_.files);
}

const std::vector<TuningSection>& TuningModel::sections() {
  if (sectionsRevision_ == revision()) {
    return sections_;
  }
  sections_.clear();
  for (const auto& [name, section] : target_.store->current().sections()) {
    sections_.push_back(TuningSection{.name = name,
                                      .owner = section.owner,
                                      .file = section.file,
                                      .root = buildTuningTree(section.node)});
  }
  sectionsRevision_ = revision();
  return sections_;
}

bool TuningModel::select(std::string_view id) {
  reloadChanges_.clear();
  return record(target_.store->select(id), messages::formatMessage(messages::Selected, id));
}

bool TuningModel::edit(std::string_view section, const TuningNode& node, std::string_view input) {
  auto yaml = yamlOfEdit(node, input);
  if (!yaml) {
    return record(std::unexpected{yaml.error()}, {});
  }
  return record(target_.store->setValue(section, node.path, *yaml),
                messages::formatMessage(messages::Tuned, section, node.path, *yaml));
}

bool TuningModel::previewReload() {
  auto files = ruleset::loadRuleset(*target_.files, active().id);
  if (!files) {
    reloadChanges_.clear();
    return record(std::unexpected{files.error()}, {});
  }
  reloadChanges_ = ruleset::diffValues(target_.store->current(), *files);
  return true;
}

bool TuningModel::reload() {
  reloadChanges_.clear();
  return record(target_.store->reload(), messages::formatMessage(messages::Reloaded, active().id));
}

bool TuningModel::save(std::string_view section) {
  const ruleset::Section* found = target_.store->current().section(section);
  if (found == nullptr) {
    return record(core::fail(messages::formatMessage(messages::NoSection, section)), {});
  }
  auto saved = ruleset::saveSection(*target_.files, *found);
  if (!saved) {
    return record(std::unexpected{saved.error()}, {});
  }
  return record({}, messages::formatMessage(messages::Saved, section, assets::pathText(*saved)));
}

bool TuningModel::record(const core::Result<void>& outcome, std::string status) {
  if (!outcome) {
    error_ = outcome.error().message;
    core::logWarning("{}", messages::formatMessage(messages::TuningLog, error_));
    return false;
  }
  error_.clear();
  status_ = std::move(status);
  core::logInfo("{}", messages::formatMessage(messages::TuningLog, status_));
  return true;
}

} // namespace dwe::debug
