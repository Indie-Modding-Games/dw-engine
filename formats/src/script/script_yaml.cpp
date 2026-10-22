#include <dwe/formats/script/script_yaml.hpp>

#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <array>
#include <format>

#include "listing_vocabulary.hpp"

namespace dwe::formats::script {

namespace {

using core::YamlNode;

using vocabulary::CompareNames;
using vocabulary::PartnerEntity;
using vocabulary::TamerEntity;

YamlNode hex(std::uint32_t value, int digits) {
    return YamlNode::scalar(std::format("{:#0{}x}", value, digits + 2));
}

YamlNode argumentToYaml(const Argument& argument, bool isCondition) {
    switch (argument.kind) {
    case ArgumentKind::Label:
        return YamlNode::scalar(labelName(static_cast<std::uint32_t>(argument.value)));
    case ArgumentKind::Entity:
        if (argument.value == TamerEntity) {
            return YamlNode::scalar(std::string{vocabulary::TamerName});
        }
        if (argument.value == PartnerEntity) {
            return YamlNode::scalar(std::string{vocabulary::PartnerName});
        }
        break;
    default:
        if (isCondition && argument.name == vocabulary::CompareKey && argument.value < std::ssize(CompareNames)) {
            return YamlNode::scalar(std::string{CompareNames[static_cast<std::size_t>(argument.value)]});
        }
        break;
    }
    return yaml::number(argument.value);
}

void setArguments(YamlNode& node, const std::vector<Argument>& arguments, bool isCondition) {
    for (const Argument& argument : arguments) {
        node.set(std::string{argument.name}, argumentToYaml(argument, isCondition));
    }
}

YamlNode labelsToYaml(const std::vector<std::uint16_t>& targets) {
    YamlNode list = YamlNode::sequence();
    for (const std::uint16_t target : targets) {
        list.append(YamlNode::scalar(labelName(target)));
    }
    return list;
}

YamlNode instructionToYaml(const Instruction& instruction, bool labelled) {
    YamlNode node = YamlNode::mapping();
    node.set("at", hex(instruction.offset, 4));
    if (labelled) {
        node.set("label", YamlNode::scalar(labelName(instruction.offset)));
    }
    node.set("op", YamlNode::scalar(std::string{opcodeName(instruction.opcode)}));
    setArguments(node, instruction.arguments, false);
    if (!instruction.conditions.empty()) {
        YamlNode conditions = YamlNode::sequence();
        for (const Condition& condition : instruction.conditions) {
            conditions.append(conditionToYaml(condition));
        }
        node.set("conditions", std::move(conditions));
    }
    if (!instruction.targets.empty()) {
        node.set(instruction.text ? "options" : "cases", labelsToYaml(instruction.targets));
    }
    if (instruction.text) {
        node.set("text", yaml::quoted(*instruction.text));
    }
    if (instruction.selectionEndMarker) {
        node.set("cancel_marker", yaml::boolean(true));
    }
    if (!instruction.data.empty()) {
        std::string data;
        for (const std::uint8_t byte : instruction.data) {
            data += std::format("{}{:02x}", data.empty() ? "" : " ", byte);
        }
        node.set("data", yaml::quoted(std::move(data)));
    }
    if (!instruction.pads.empty()) {
        YamlNode pads = YamlNode::sequence();
        for (const PadByte& pad : instruction.pads) {
            YamlNode entry = YamlNode::mapping();
            entry.set("offset", yaml::number(pad.offset));
            entry.set("value", hex(pad.value, 2));
            pads.append(std::move(entry));
        }
        node.set("pads", std::move(pads));
    }
    return node;
}

} // namespace

YamlNode conditionToYaml(const Condition& condition) {
    const vocabulary::TestInfo info = vocabulary::testInfo(condition.test);
    YamlNode node = YamlNode::mapping();
    node.set(std::string{vocabulary::TestKey}, YamlNode::scalar(std::string{info.name}));
    const std::uint8_t low = condition.code & vocabulary::LowBits;
    std::uint8_t variant = 0;
    if (condition.test == ConditionTest::Trigger && low != 0) {
        node.set(std::string{vocabulary::ClearKey}, yaml::boolean(true));
        variant = vocabulary::ClearBit;
    }
    if (condition.test == ConditionTest::Pstat) {
        node.set(std::string{vocabulary::CompareKey}, YamlNode::scalar(std::string{CompareNames.at(low)}));
        variant = low;
    }
    if (condition.combine != Combine::Replace) {
        node.set(std::string{vocabulary::CombineKey},
                 YamlNode::scalar(std::string{vocabulary::combineName(condition.combine)}));
    }
    setArguments(node, condition.arguments, true);
    if (condition.code != vocabulary::entryCode(condition.test, variant, condition.combine)) {
        node.set(std::string{vocabulary::CodeKey}, hex(condition.code, 2));
    }
    return node;
}

std::string labelName(std::uint32_t offset) {
    return std::format("l_{:04x}", offset);
}

YamlNode scriptToYaml(const Disassembly& disassembly) {
    YamlNode root = YamlNode::mapping();
    root.set("code_start", hex(disassembly.header.codeStart, 4));
    root.set("code_end", hex(disassembly.codeEnd, 4));
    YamlNode sections = YamlNode::sequence();
    for (const ScriptSection& section : disassembly.header.sections) {
        YamlNode entry = YamlNode::mapping();
        entry.set("id", hex(section.id, 4));
        entry.set("at", YamlNode::scalar(labelName(section.offset)));
        sections.append(std::move(entry));
    }
    root.set("sections", std::move(sections));
    root.set("unreachable_instructions", yaml::number(disassembly.unreachable));
    if (!disassembly.unknowns.empty()) {
        YamlNode unknowns = YamlNode::sequence();
        for (const std::string& unknown : disassembly.unknowns) {
            unknowns.append(yaml::quoted(unknown));
        }
        root.set("unknowns", std::move(unknowns));
    }
    YamlNode instructions = YamlNode::sequence();
    for (const Instruction& instruction : disassembly.instructions) {
        const bool labelled = std::ranges::binary_search(disassembly.labels, instruction.offset);
        instructions.append(instructionToYaml(instruction, labelled));
    }
    root.set("instructions", std::move(instructions));
    return root;
}

} // namespace dwe::formats::script
