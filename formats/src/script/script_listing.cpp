#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/script_listing.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>
#include <utility>

#include "../messages.hpp"

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
constexpr std::string_view TestKey = "test";
constexpr std::string_view CasesKey = "cases";
constexpr std::string_view OptionsKey = "options";
constexpr std::string_view ScriptKey = "script";
constexpr std::string_view SectionKey = "section";
constexpr std::string_view GotoIfTrueTest = "goto_if_true";
constexpr std::string_view GotoIfFalseTest = "goto_if_false";
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
    const std::string_view text = node.isScalar() ? std::string_view{node.text()} : std::string_view{};
    const auto offset =
        text.starts_with(LabelPrefix) ? parseUnsigned(text.substr(LabelPrefix.size()), HexBase) : std::nullopt;
    if (!offset) {
        return core::fail(messages::formatMessage(messages::ListingBadLabel, text));
    }
    return *offset;
}

core::Result<std::uint32_t> readNumber(const YamlNode& mapping, std::string_view key, std::size_t index) {
    const YamlNode* node = mapping.find(key);
    if (node == nullptr || !node->isScalar()) {
        return core::fail(messages::formatMessage(messages::ListingMissingKey, index, key));
    }
    const auto value = parseNumber(node->text());
    if (!value) {
        return core::fail(messages::formatMessage(messages::ListingBadNumber, index, key, node->text()));
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

core::Result<std::vector<ConditionalJump>> readConditionalJumps(const YamlNode* conditions) {
    std::vector<ConditionalJump> jumps;
    if (conditions == nullptr) {
        return jumps;
    }
    const auto entries = conditions->items();
    for (std::size_t entry = 0; entry < entries.size(); ++entry) {
        const YamlNode* test = entries[entry].find(TestKey);
        const std::string_view name = test != nullptr && test->isScalar() ? std::string_view{test->text()} : "";
        if (name != GotoIfTrueTest && name != GotoIfFalseTest) {
            continue;
        }
        const YamlNode* targetNode = entries[entry].find(TargetKey);
        const auto target = targetNode != nullptr ? parseLabel(*targetNode) : parseLabel(YamlNode::scalar(""));
        if (!target) {
            return std::unexpected{target.error()};
        }
        jumps.push_back({.entry = entry, .whenTrue = name == GotoIfTrueTest, .target = *target});
    }
    return jumps;
}

core::Result<std::optional<ScriptReference>> readReference(const YamlNode& node, FlowRole role, std::size_t index) {
    if (role != FlowRole::FileCall && role != FlowRole::FileJump) {
        return std::nullopt;
    }
    const auto script = readNumber(node, ScriptKey, index);
    const auto section = script ? readNumber(node, SectionKey, index) : script;
    if (!section) {
        return std::unexpected{section.error()};
    }
    return ScriptReference{.script = static_cast<std::uint16_t>(*script),
                           .section = static_cast<std::uint16_t>(*section)};
}

core::Result<std::vector<std::uint32_t>> readTargets(const YamlNode& node, FlowRole role) {
    switch (role) {
    case FlowRole::Jump:
    case FlowRole::Call: {
        const YamlNode* target = node.find(TargetKey);
        if (target == nullptr) {
            return core::fail(messages::formatMessage(messages::ListingBadLabel, ""));
        }
        const auto label = parseLabel(*target);
        if (!label) {
            return std::unexpected{label.error()};
        }
        return std::vector<std::uint32_t>{*label};
    }
    case FlowRole::Switch:
        return readLabels(node.find(CasesKey));
    case FlowRole::Selection:
        return readLabels(node.find(OptionsKey));
    default:
        return std::vector<std::uint32_t>{};
    }
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
    ListingInstruction instruction{.offset = *offset,
                                   .op = op->text(),
                                   .role = flowRoleOf(op->text()),
                                   .targets = {},
                                   .conditionalJumps = {},
                                   .reference = std::nullopt};
    auto targets = readTargets(node, instruction.role);
    auto jumps = readConditionalJumps(node.find(ConditionsKey));
    auto reference = readReference(node, instruction.role, index);
    if (!targets) {
        return std::unexpected{targets.error()};
    }
    if (!jumps) {
        return std::unexpected{jumps.error()};
    }
    if (!reference) {
        return std::unexpected{reference.error()};
    }
    instruction.targets = std::move(*targets);
    instruction.conditionalJumps = std::move(*jumps);
    instruction.reference = *reference;
    return instruction;
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
            return core::fail(messages::formatMessage(messages::ListingNoInstruction, "section", section.entry));
        }
    }
    for (const ListingInstruction& instruction : listing.instructions) {
        std::vector<std::uint32_t> destinations = instruction.targets;
        for (const ConditionalJump& jump : instruction.conditionalJumps) {
            destinations.push_back(jump.target);
        }
        for (const std::uint32_t destination : destinations) {
            if (!listing.indexAt(destination)) {
                return core::fail(messages::formatMessage(messages::ListingNoInstruction, "target", destination));
            }
        }
    }
    return {};
}

} // namespace

std::optional<std::size_t> ScriptListing::indexAt(std::uint32_t offset) const {
    const auto found = std::ranges::lower_bound(instructions, offset, {}, &ListingInstruction::offset);
    if (found == instructions.end() || found->offset != offset) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - instructions.begin());
}

FlowRole flowRoleOf(std::string_view op) {
    const auto opcode = opcodeNamed(op);
    return opcode ? opcodeFlowRole(*opcode) : FlowRole::Plain;
}

core::Result<ScriptListing> readScriptListing(const core::YamlNode& listing) {
    const YamlNode* sections = listing.isMapping() ? listing.find(SectionsKey) : nullptr;
    const YamlNode* instructions = listing.isMapping() ? listing.find(InstructionsKey) : nullptr;
    if (sections == nullptr || instructions == nullptr || !sections->isSequence() || !instructions->isSequence()) {
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

} // namespace dwe::formats::script
