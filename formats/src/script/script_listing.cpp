#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/script_listing.hpp>

#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "../messages.hpp"
#include "listing_vocabulary.hpp"

namespace dwe::formats::script {

namespace {

using core::YamlNode;

// The keys of a listing (script_yaml.cpp: scriptToYaml).
constexpr std::string_view SectionsKey = "sections";
constexpr std::string_view InstructionsKey = "instructions";
constexpr std::string_view IdKey = "id";
constexpr std::string_view AtKey = "at";
constexpr std::string_view OpKey = "op";
constexpr std::string_view TargetKey = "target";
constexpr std::string_view ConditionsKey = "conditions";
constexpr std::string_view CasesKey = "cases";
constexpr std::string_view OptionsKey = "options";
constexpr std::string_view ScriptKey = "script";
constexpr std::string_view SectionKey = "section";
constexpr std::string_view LabelPrefix = "l_";
constexpr std::string_view HexPrefix = "0x";
constexpr int HexBase = 16;
constexpr int DecimalBase = 10;

std::optional<std::uint32_t> parseUnsigned(std::string_view text, int base) {
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
    return std::nullopt;
  }
  return value;
}

// A number as the listing writes it: 0x-prefixed hex or decimal.
std::optional<std::uint32_t> parseNumber(std::string_view text) {
  if (text.starts_with(HexPrefix)) {
    return parseUnsigned(text.substr(HexPrefix.size()), HexBase);
  }
  return parseUnsigned(text, DecimalBase);
}

core::Result<std::uint32_t> parseLabel(const YamlNode& node) {
  const std::string_view text =
      node.isScalar() ? std::string_view{node.text()} : std::string_view{};
  const auto offset = text.starts_with(LabelPrefix)
                          ? parseUnsigned(text.substr(LabelPrefix.size()), HexBase)
                          : std::nullopt;
  if (!offset) {
    return core::fail(messages::formatMessage(messages::ListingBadLabel, text));
  }
  return *offset;
}

core::Result<std::uint32_t> readNumber(const YamlNode& mapping, std::string_view key,
                                       std::size_t index) {
  const YamlNode* node = mapping.find(key);
  if (node == nullptr || !node->isScalar()) {
    return core::fail(messages::formatMessage(messages::ListingMissingKey, index, key));
  }
  const auto value = parseNumber(node->text());
  if (!value) {
    return core::fail(
        messages::formatMessage(messages::ListingBadNumber, index, key, node->text()));
  }
  return *value;
}

core::Result<std::vector<std::uint32_t>> readLabels(const YamlNode* list) {
  std::vector<std::uint32_t> labels;
  if (list == nullptr) {
    return labels;
  }
  for (const YamlNode& item : list->items()) {
    const auto label = parseLabel(item);
    if (!label) {
      return std::unexpected{label.error()};
    }
    labels.push_back(*label);
  }
  return labels;
}

// A number as the listing writes it, with a minus sign for a negative operand.
std::optional<std::int32_t> parseSigned(std::string_view text) {
  const bool negative = text.starts_with('-');
  const auto magnitude = parseNumber(negative ? text.substr(1) : text);
  if (!magnitude ||
      *magnitude > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
    return std::nullopt;
  }
  const auto value = static_cast<std::int32_t>(*magnitude);
  return negative ? -value : value;
}

std::optional<std::int32_t> wordIndex(std::span<const std::string_view> words,
                                      std::string_view text) {
  const auto found = std::ranges::find(words, text);
  return found == words.end() ? std::nullopt
                              : std::optional{static_cast<std::int32_t>(found - words.begin())};
}

// An operand, read the way scriptToYaml writes its kind: a label, an entity's word or number, a
// comparison's word (in an IF entry) or a number.
core::Result<std::int32_t> readOperand(const YamlNode& mapping, const OperandField& field,
                                       bool inCondition, std::size_t index) {
  const YamlNode* node = mapping.find(field.name);
  if (node == nullptr || !node->isScalar()) {
    return core::fail(messages::formatMessage(messages::ListingMissingKey, index, field.name));
  }
  const std::string_view text = node->text();
  if (field.kind == ArgumentKind::Label) {
    const auto label = parseLabel(*node);
    return label ? core::Result<std::int32_t>{static_cast<std::int32_t>(*label)}
                 : std::unexpected{label.error()};
  }
  if (field.kind == ArgumentKind::Entity && text == vocabulary::TamerName) {
    return vocabulary::TamerEntity;
  }
  if (field.kind == ArgumentKind::Entity && text == vocabulary::PartnerName) {
    return vocabulary::PartnerEntity;
  }
  if (inCondition && field.name == vocabulary::CompareKey) {
    if (const auto compare = wordIndex(vocabulary::CompareNames, text)) {
      return *compare;
    }
  }
  const auto value = parseSigned(text);
  if (!value) {
    return core::fail(messages::formatMessage(messages::ListingBadNumber, index, field.name, text));
  }
  return *value;
}

core::Result<std::vector<Argument>> readArguments(const YamlNode& node,
                                                  std::span<const OperandField> fields,
                                                  bool inCondition, std::size_t index) {
  std::vector<Argument> arguments;
  for (const OperandField& field : fields) {
    const auto value = readOperand(node, field, inCondition, index);
    if (!value) {
      return std::unexpected{value.error()};
    }
    arguments.push_back({.name = field.name, .kind = field.kind, .value = *value});
  }
  return arguments;
}

// The position in `words` of the word under `key`.
core::Result<std::int32_t> readWord(const YamlNode& node, std::string_view key,
                                    std::span<const std::string_view> words, std::size_t index) {
  const YamlNode* word = node.find(key);
  const std::string_view text =
      word != nullptr && word->isScalar() ? std::string_view{word->text()} : "";
  const auto position = wordIndex(words, text);
  if (!position) {
    return core::fail(messages::formatMessage(messages::ListingBadWord, index, key, text));
  }
  return *position;
}

core::Result<Combine> readCombine(const YamlNode& entry, std::size_t index) {
  if (entry.find(vocabulary::CombineKey) == nullptr) {
    return Combine::Replace;
  }
  constexpr std::array<std::string_view, 2> Words = {vocabulary::AndName, vocabulary::OrName};
  const auto word = readWord(entry, vocabulary::CombineKey, Words, index);
  if (!word) {
    return std::unexpected{word.error()};
  }
  return Words.at(static_cast<std::size_t>(*word)) == vocabulary::AndName ? Combine::And
                                                                          : Combine::Or;
}

// What sets the low bits of an entry's code besides its test: whether a trigger test is for a clear
// trigger, or a variable test's comparison.
core::Result<std::uint8_t> readVariant(const YamlNode& entry, ConditionTest test,
                                       std::size_t index) {
  if (test == ConditionTest::Trigger) {
    const YamlNode* clear = entry.find(vocabulary::ClearKey);
    return clear != nullptr && clear->isScalar() && clear->text() == yaml::TrueText
               ? vocabulary::ClearBit
               : std::uint8_t{0};
  }
  if (test != ConditionTest::Pstat) {
    return std::uint8_t{0};
  }
  const auto word = readWord(entry, vocabulary::CompareKey, vocabulary::CompareNames, index);
  if (!word) {
    return std::unexpected{word.error()};
  }
  return static_cast<std::uint8_t>(*word);
}

core::Result<Condition> readCondition(const YamlNode& entry, std::size_t index) {
  const YamlNode* name = entry.find(vocabulary::TestKey);
  const std::string_view text =
      name != nullptr && name->isScalar() ? std::string_view{name->text()} : "";
  const auto test = std::ranges::find(vocabulary::Tests, text, [](ConditionTest candidate) {
    return vocabulary::testInfo(candidate).name;
  });
  if (test == vocabulary::Tests.end()) {
    return core::fail(messages::formatMessage(messages::ListingUnknownTest, index, text));
  }
  const auto combine = readCombine(entry, index);
  const auto variant = combine ? readVariant(entry, *test, index) : core::Result<std::uint8_t>{0};
  auto arguments = variant ? readArguments(entry, conditionOperands(*test), true, index)
                           : core::Result<std::vector<Argument>>{};
  if (!combine) {
    return std::unexpected{combine.error()};
  }
  if (!variant) {
    return std::unexpected{variant.error()};
  }
  if (!arguments) {
    return std::unexpected{arguments.error()};
  }
  std::uint32_t code = vocabulary::entryCode(*test, *variant, *combine);
  if (entry.find(vocabulary::CodeKey) != nullptr) {
    const auto written = readNumber(entry, vocabulary::CodeKey, index);
    if (!written) {
      return std::unexpected{written.error()};
    }
    code = *written;
  }
  return Condition{.code = static_cast<std::uint8_t>(code),
                   .test = *test,
                   .combine = *combine,
                   .arguments = std::move(*arguments)};
}

core::Result<std::vector<Condition>> readConditions(const YamlNode* list, std::size_t index) {
  std::vector<Condition> conditions;
  if (list == nullptr) {
    return conditions;
  }
  for (const YamlNode& entry : list->items()) {
    auto condition = readCondition(entry, index);
    if (!condition) {
      return std::unexpected{condition.error()};
    }
    conditions.push_back(std::move(*condition));
  }
  return conditions;
}

std::optional<std::int32_t> argumentOf(std::span<const Argument> arguments, std::string_view name) {
  const auto found = std::ranges::find(arguments, name, &Argument::name);
  return found == arguments.end() ? std::nullopt : std::optional{found->value};
}

// The jump entries of an IF.
std::vector<ConditionalJump> jumpsOf(std::span<const Condition> conditions) {
  std::vector<ConditionalJump> jumps;
  for (std::size_t entry = 0; entry < conditions.size(); ++entry) {
    const Condition& condition = conditions[entry];
    const auto target = argumentOf(condition.arguments, TargetKey);
    if ((condition.test == ConditionTest::GotoIfTrue ||
         condition.test == ConditionTest::GotoIfFalse) &&
        target) {
      jumps.push_back({.entry = entry,
                       .whenTrue = condition.test == ConditionTest::GotoIfTrue,
                       .target = static_cast<std::uint32_t>(*target)});
    }
  }
  return jumps;
}

// Fills in what the instruction does to the flow from its operands, its IF entries and, for a
// SWITCH or a SET_SELECTION, its cases or options.
ListingInstruction withFlow(ListingInstruction instruction, std::vector<std::uint32_t> choices) {
  const auto argument = [&](std::string_view name) {
    return argumentOf(instruction.arguments, name);
  };
  switch (instruction.role) {
  case FlowRole::Jump:
  case FlowRole::Call:
    if (const auto target = argument(TargetKey)) {
      instruction.targets = {static_cast<std::uint32_t>(*target)};
    }
    break;
  case FlowRole::Switch:
  case FlowRole::Selection:
    instruction.targets = std::move(choices);
    break;
  case FlowRole::FileCall:
  case FlowRole::FileJump:
    if (const auto script = argument(ScriptKey), section = argument(SectionKey);
        script && section) {
      instruction.reference = ScriptReference{.script = static_cast<std::uint16_t>(*script),
                                              .section = static_cast<std::uint16_t>(*section)};
    }
    break;
  default:
    break;
  }
  instruction.conditionalJumps = jumpsOf(instruction.conditions);
  return instruction;
}

core::Result<std::vector<std::uint32_t>> readChoices(const YamlNode& node, FlowRole role) {
  if (role == FlowRole::Switch) {
    return readLabels(node.find(CasesKey));
  }
  if (role == FlowRole::Selection) {
    return readLabels(node.find(OptionsKey));
  }
  return std::vector<std::uint32_t>{};
}

// The text of an instruction that carries one; nothing for the others.
core::Result<std::optional<std::string>>
readText(const YamlNode& node, std::optional<std::uint8_t> opcode, std::size_t index) {
  if (!opcode || !vocabulary::carriesText(*opcode)) {
    return std::nullopt;
  }
  const YamlNode* text = node.find(vocabulary::TextKey);
  if (text == nullptr) {
    return core::fail(
        messages::formatMessage(messages::ListingMissingKey, index, vocabulary::TextKey));
  }
  if (!text->isScalar()) {
    return core::fail(
        messages::formatMessage(messages::ListingNotString, index, vocabulary::TextKey));
  }
  return std::optional<std::string>{text->text()};
}

core::Result<ListingInstruction> readInstruction(const YamlNode& node, std::size_t index) {
  const auto offset = readNumber(node, AtKey, index);
  if (!offset) {
    return std::unexpected{offset.error()};
  }
  const YamlNode* op = node.find(OpKey);
  if (op == nullptr || !op->isScalar()) {
    return core::fail(messages::formatMessage(messages::ListingMissingKey, index, OpKey));
  }
  const auto opcode = opcodeNamed(op->text());
  const FlowRole role = opcode ? opcodeFlowRole(*opcode) : FlowRole::Plain;
  auto arguments = opcode ? readArguments(node, opcodeOperands(*opcode), false, index)
                          : core::Result<std::vector<Argument>>{};
  auto conditions = arguments ? readConditions(node.find(ConditionsKey), index)
                              : core::Result<std::vector<Condition>>{};
  auto choices = conditions ? readChoices(node, role) : core::Result<std::vector<std::uint32_t>>{};
  auto text = choices ? readText(node, opcode, index) : core::Result<std::optional<std::string>>{};
  if (!arguments) {
    return std::unexpected{arguments.error()};
  }
  if (!conditions) {
    return std::unexpected{conditions.error()};
  }
  if (!choices) {
    return std::unexpected{choices.error()};
  }
  if (!text) {
    return std::unexpected{text.error()};
  }
  return withFlow({.offset = *offset,
                   .op = op->text(),
                   .role = role,
                   .targets = {},
                   .conditionalJumps = {},
                   .reference = std::nullopt,
                   .arguments = std::move(*arguments),
                   .conditions = std::move(*conditions),
                   .text = std::move(*text)},
                  std::move(*choices));
}

core::Result<std::vector<ListingSection>> readSections(const YamlNode& sections) {
  std::vector<ListingSection> result;
  for (std::size_t index = 0; index < sections.items().size(); ++index) {
    const YamlNode& node = sections.items()[index];
    const auto id = readNumber(node, IdKey, index);
    const YamlNode* at = node.find(AtKey);
    const auto entry = at != nullptr ? parseLabel(*at) : parseLabel(YamlNode::scalar(""));
    if (!id || !entry) {
      return std::unexpected{!id ? id.error() : entry.error()};
    }
    result.push_back({.id = static_cast<std::uint16_t>(*id), .entry = *entry});
  }
  return result;
}

// Every section and target must be the start of an instruction.
core::Result<void> checkDestinations(const ScriptListing& listing) {
  for (const ListingSection& section : listing.sections) {
    if (!listing.indexAt(section.entry)) {
      return core::fail(
          messages::formatMessage(messages::ListingNoInstruction, "section", section.entry));
    }
  }
  for (const ListingInstruction& instruction : listing.instructions) {
    std::vector<std::uint32_t> destinations = instruction.targets;
    for (const ConditionalJump& jump : instruction.conditionalJumps) {
      destinations.push_back(jump.target);
    }
    for (const std::uint32_t destination : destinations) {
      if (!listing.indexAt(destination)) {
        return core::fail(
            messages::formatMessage(messages::ListingNoInstruction, "target", destination));
      }
    }
  }
  return {};
}

} // namespace

std::vector<ListingSection> ScriptListing::runnableSections() const {
  std::vector<ListingSection> runnable;
  for (const ListingSection& section : sections) {
    if (!std::ranges::contains(runnable, section.id, &ListingSection::id)) {
      runnable.push_back(section);
    }
  }
  return runnable;
}

std::optional<std::size_t> ScriptListing::indexAt(std::uint32_t offset) const {
  const auto found =
      std::ranges::lower_bound(instructions, offset, {}, &ListingInstruction::offset);
  if (found == instructions.end() || found->offset != offset) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - instructions.begin());
}

std::optional<std::int32_t> ListingInstruction::argument(std::string_view name) const {
  return argumentOf(arguments, name);
}

FlowRole flowRoleOf(std::string_view op) {
  const auto opcode = opcodeNamed(op);
  return opcode ? opcodeFlowRole(*opcode) : FlowRole::Plain;
}

core::Result<ScriptListing> readScriptListing(const core::YamlNode& listing) {
  const YamlNode* sections = listing.isMapping() ? listing.find(SectionsKey) : nullptr;
  const YamlNode* instructions = listing.isMapping() ? listing.find(InstructionsKey) : nullptr;
  if (sections == nullptr || instructions == nullptr || !sections->isSequence() ||
      !instructions->isSequence()) {
    return core::fail(std::string{messages::ListingNotMapping});
  }
  ScriptListing result;
  auto readSectionList = readSections(*sections);
  if (!readSectionList) {
    return std::unexpected{readSectionList.error()};
  }
  result.sections = std::move(*readSectionList);
  for (std::size_t index = 0; index < instructions->items().size(); ++index) {
    auto instruction = readInstruction(instructions->items()[index], index);
    if (!instruction) {
      return std::unexpected{instruction.error()};
    }
    if (!result.instructions.empty() && instruction->offset <= result.instructions.back().offset) {
      return core::fail(messages::formatMessage(messages::ListingOutOfOrder, instruction->offset));
    }
    result.instructions.push_back(std::move(*instruction));
  }
  if (auto checked = checkDestinations(result); !checked) {
    return std::unexpected{checked.error()};
  }
  return result;
}

ScriptListing listingOf(const Disassembly& disassembly) {
  ScriptListing listing;
  for (const ScriptSection& section : disassembly.header.sections) {
    listing.sections.push_back({.id = section.id, .entry = section.offset});
  }
  for (const Instruction& instruction : disassembly.instructions) {
    std::vector<std::uint32_t> choices(instruction.targets.begin(), instruction.targets.end());
    listing.instructions.push_back(withFlow({.offset = instruction.offset,
                                             .op = std::string{opcodeName(instruction.opcode)},
                                             .role = opcodeFlowRole(instruction.opcode),
                                             .targets = {},
                                             .conditionalJumps = {},
                                             .reference = std::nullopt,
                                             .arguments = instruction.arguments,
                                             .conditions = instruction.conditions,
                                             .text = instruction.text},
                                            std::move(choices)));
  }
  return listing;
}

} // namespace dwe::formats::script
