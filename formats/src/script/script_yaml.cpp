#include <dwe/formats/script/script_yaml.hpp>

#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <array>
#include <format>

namespace dwe::formats::script {

namespace {

using core::YamlNode;

constexpr std::array<std::string_view, 6> CompareNames = {"eq", "ne", "ge", "le", "gt", "lt"};

// scriptIdToEntityId: these ids are not NPCs.
constexpr std::int32_t PartnerEntity = 0xFC;
constexpr std::int32_t TamerEntity = 0xFD;

YamlNode hex(std::uint32_t value, int digits) {
    return YamlNode::scalar(std::format("{:#0{}x}", value, digits + 2));
}

YamlNode argumentToYaml(const Argument& argument, bool isCondition) {
    switch (argument.kind) {
    case ArgumentKind::Label:
        return YamlNode::scalar(labelName(static_cast<std::uint32_t>(argument.value)));
    case ArgumentKind::Entity:
        if (argument.value == TamerEntity) {
            return YamlNode::scalar("tamer");
        }
        if (argument.value == PartnerEntity) {
            return YamlNode::scalar("partner");
        }
        break;
    default:
        if (isCondition && argument.name == "compare" && argument.value < std::ssize(CompareNames)) {
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

struct TestInfo {
    std::string_view name;
    std::uint8_t kind = 0; // the cond & 0x38 bits
    std::uint8_t low = 0;  // the low bits of an extended test
};

TestInfo testInfo(ConditionTest test) {
    switch (test) {
    case ConditionTest::Trigger:
        return {.name = "trigger", .kind = 0x00};
    case ConditionTest::Pstat:
        return {.name = "pstat", .kind = 0x08};
    case ConditionTest::GotoIfTrue:
        return {.name = "goto_if_true", .kind = 0x10};
    case ConditionTest::GotoIfFalse:
        return {.name = "goto_if_false", .kind = 0x18};
    case ConditionTest::Stat:
        return {.name = "stat", .kind = 0x20, .low = 0};
    case ConditionTest::Card:
        return {.name = "card", .kind = 0x20, .low = 1};
    case ConditionTest::Move:
        return {.name = "move", .kind = 0x20, .low = 2};
    case ConditionTest::Condition:
        return {.name = "condition", .kind = 0x20, .low = 3};
    case ConditionTest::ItemCount:
        return {.name = "item_count", .kind = 0x20, .low = 4};
    case ConditionTest::Money:
        return {.name = "money", .kind = 0x20, .low = 5};
    }
    return {};
}

YamlNode conditionToYaml(const Condition& condition) {
    const TestInfo info = testInfo(condition.test);
    YamlNode node = YamlNode::mapping();
    node.set("test", YamlNode::scalar(std::string{info.name}));
    std::uint8_t canonical = info.kind | info.low;
    const unsigned low = condition.code & 0x07U;
    if (condition.test == ConditionTest::Trigger && low != 0) {
        node.set("clear", yaml::boolean(true));
        canonical |= 0x01U;
    }
    if (condition.test == ConditionTest::Pstat) {
        node.set("compare", YamlNode::scalar(std::string{CompareNames[low]}));
        canonical |= low;
    }
    if (condition.combine == Combine::And) {
        node.set("combine", YamlNode::scalar("and"));
        canonical |= 0x80U;
    } else if (condition.combine == Combine::Or) {
        node.set("combine", YamlNode::scalar("or"));
        canonical |= 0x40U;
    }
    setArguments(node, condition.arguments, true);
    if (condition.code != canonical) {
        node.set("code", hex(condition.code, 2));
    }
    return node;
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
