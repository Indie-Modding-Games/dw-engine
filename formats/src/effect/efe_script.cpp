#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/effect/efe_script.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace dwe::formats::effect {

namespace {

struct OpcodeSpec {
    std::string_view name;
    std::size_t size;
    std::span<const EfeIgnoredOperand> ignored;
};

using enum EfeIgnoredKind;

// The variant byte of the opcodes that do not read it.
constexpr EfeIgnoredOperand UnusedVariant{.index = 0, .name = "variant", .kind = Fill};

constexpr std::array IgnoredImmediate = {UnusedVariant,
                                         EfeIgnoredOperand{.index = 2, .name = "value_high", .kind = SignOfLow}};
constexpr std::array IgnoredVariant = {UnusedVariant};
constexpr std::array IgnoredIndexed = {EfeIgnoredOperand{.index = 2, .name = "table_flag", .kind = BaseFlag}};
constexpr std::array IgnoredBranch = {EfeIgnoredOperand{.index = 4, .name = "target_flag", .kind = BaseFlag}};
constexpr std::array IgnoredJump = {UnusedVariant,
                                    EfeIgnoredOperand{.index = 2, .name = "target_flag", .kind = BaseFlag}};
constexpr std::array IgnoredSpawn = {EfeIgnoredOperand{.index = 3, .name = "table_flag", .kind = BaseFlag},
                                     EfeIgnoredOperand{.index = 5, .name = "stride_high", .kind = Zero}};

// BTL_jtbl_80073604 and the length each handler moves the script pointer by.
constexpr std::array<OpcodeSpec, EfeOpcodeCount> Opcodes = {{
    {.name = "load_immediate", .size = 6, .ignored = IgnoredImmediate}, // BTL_loadEFEImmediate
    {.name = "load_variable", .size = 6, .ignored = {}},                // BTL_loadEFEVariable
    {.name = "load_random", .size = 2, .ignored = IgnoredVariant},      // BTL_loadEFERandomValue
    {.name = "load_indexed", .size = 10, .ignored = IgnoredIndexed},    // BTL_loadEFEIndexedVariable
    {.name = "apply_operator", .size = 6, .ignored = {}},               // BTL_applyEFEVariableOperator
    {.name = "stop", .size = 2, .ignored = IgnoredVariant},             // BTL_stopEFEScript
    {.name = "stop_6", .size = 2, .ignored = IgnoredVariant},           // BTL_stopEFEScript
    {.name = "branch_if", .size = 10, .ignored = IgnoredBranch},        // BTL_branchEFEOnComparison
    {.name = "stop_8", .size = 2, .ignored = IgnoredVariant},           // BTL_stopEFEScript
    {.name = "jump", .size = 6, .ignored = IgnoredJump},                // BTL_jumpEFEScript
    {.name = "push_immediate", .size = 6, .ignored = IgnoredImmediate}, // BTL_pushEFEImmediate
    {.name = "push_variable", .size = 6, .ignored = {}},                // BTL_pushEFEVariable
    {.name = "push_address", .size = 6, .ignored = {}},                 // BTL_pushEFEVariableAddress
    {.name = "call", .size = 6, .ignored = IgnoredJump},                // BTL_callEFESubroutine
    {.name = "sub", .size = 2, .ignored = {}},                          // BTL_dispatchEFESubOpcode
    {.name = "return", .size = 2, .ignored = IgnoredVariant},           // BTL_returnFromEFESubroutine
    {.name = "pop_to_variable", .size = 6, .ignored = IgnoredVariant},  // BTL_popEFEValueToVariable
    {.name = "spawn", .size = 12, .ignored = IgnoredSpawn},             // BTL_spawnEFESubEffect
}};

constexpr std::array<std::string_view, 8> OperatorNames = {"set",    "add",    "subtract",   "multiply",
                                                           "divide", "modulo", "shift_left", "shift_right"};
constexpr std::array<std::string_view, 6> ComparisonNames = {"equal",         "not_equal", "less",
                                                             "less_or_equal", "greater",   "greater_or_equal"};

// BTL_EFE_VARIABLE_OPERATORS has five rows: 0, 1 and 3 are the int8 operators, 2 the int16 and 4 the int32 ones.
constexpr std::array OperatorRowWidths = {EfeWidth::Int8, EfeWidth::Int8, EfeWidth::Int16, EfeWidth::Int8,
                                          EfeWidth::Int32};
constexpr std::uint8_t VariantFill = 0xCD;

EfeWidth widthOf(unsigned nibble) {
    switch (nibble) {
    case 4:
        return EfeWidth::Int32;
    case 2:
        return EfeWidth::Int16;
    default:
        return EfeWidth::Int8;
    }
}

EfeVariable variableAt(std::span<const std::uint16_t> operands, std::size_t first) {
    const std::uint16_t flag = operands[first + 1];
    return {.space = flag == 0 ? EfeSpace::Instance : EfeSpace::Base,
            .offset = static_cast<std::int16_t>(operands[first]),
            .flag = flag};
}

} // namespace

std::optional<std::size_t> efeInstructionSize(std::uint8_t opcode) {
    if (opcode >= EfeOpcodeCount) {
        return std::nullopt;
    }
    return Opcodes[opcode].size;
}

std::string_view efeOpcodeName(EfeOpcode opcode) {
    return Opcodes[static_cast<std::size_t>(opcode)].name;
}

std::string_view efeOperatorName(EfeOperator op) {
    return OperatorNames[static_cast<std::size_t>(op)];
}

std::string_view efeComparisonName(EfeComparison comparison) {
    return ComparisonNames[static_cast<std::size_t>(comparison)];
}

std::span<const EfeIgnoredOperand> efeIgnoredOperands(EfeOpcode opcode) {
    return Opcodes[static_cast<std::size_t>(opcode)].ignored;
}

std::uint16_t EfeInstruction::ignoredValue(std::size_t index) const {
    if (index == 0) {
        return variant();
    }
    return index <= operands.size() ? operands[index - 1] : 0;
}

bool EfeInstruction::isUsualIgnored(const EfeIgnoredOperand& ignored) const {
    const std::uint16_t stored = ignoredValue(ignored.index);
    switch (ignored.kind) {
    case EfeIgnoredKind::Fill:
        return stored == (ignored.index == 0 ? VariantFill : EfeFillHalfword);
    case EfeIgnoredKind::BaseFlag:
        return stored == EfeBaseFlag;
    case EfeIgnoredKind::SignOfLow:
        return stored == (value < 0 ? 0xFFFF : 0);
    case EfeIgnoredKind::Zero:
        return stored == 0;
    }
    return false;
}

core::Result<EfeInstruction> decodeEfeInstruction(std::span<const std::byte> payload, std::uint32_t offset,
                                                  std::uint32_t end) {
    const ByteReader reader{payload.first(std::min<std::size_t>(end, payload.size()))};
    const auto word = reader.u16(offset);
    if (!word) {
        return core::fail(std::format("{:#06x}: the script ends inside an instruction", offset));
    }
    const auto code = static_cast<std::uint8_t>(*word & 0xFFU);
    const auto size = efeInstructionSize(code);
    if (!size) {
        return core::fail(std::format("{:#06x}: {:#04x} is not an opcode", offset, code));
    }
    if (!reader.contains(offset, *size)) {
        return core::fail(std::format("{:#06x}: the script ends inside an instruction", offset));
    }
    EfeInstruction in;
    in.offset = offset;
    in.opcode = static_cast<EfeOpcode>(code);
    in.word = *word;
    for (std::size_t at = offset + 2; at < offset + *size; at += 2) {
        in.operands.push_back(reader.u16(at).value_or(0)); // inside the checked range
    }
    const std::span<const std::uint16_t> ops = in.operands;
    const unsigned low = in.variant() & 0xFU;
    const unsigned high = in.variant() >> 4U;
    switch (in.opcode) {
    case EfeOpcode::LoadImmediate:
    case EfeOpcode::PushImmediate:
        in.value = static_cast<std::int16_t>(ops[0]);
        break;
    case EfeOpcode::LoadVariable:
    case EfeOpcode::PushVariable:
    case EfeOpcode::PushAddress:
    case EfeOpcode::PopToVariable:
        in.width = widthOf(low);
        in.variable = variableAt(ops, 0);
        break;
    case EfeOpcode::LoadIndexed:
        in.width = widthOf(low);
        in.indexWidth = widthOf(high);
        in.target = static_cast<std::int16_t>(ops[0]);
        in.variable = variableAt(ops, 2);
        break;
    case EfeOpcode::ApplyOperator:
        if (low >= OperatorRowWidths.size() || high >= OperatorNames.size()) {
            return core::fail(
                std::format("{:#06x}: operator {:#04x} is past BTL_EFE_VARIABLE_OPERATORS", offset, in.variant()));
        }
        in.width = OperatorRowWidths[low];
        in.op = static_cast<EfeOperator>(high);
        in.variable = variableAt(ops, 0);
        break;
    case EfeOpcode::BranchIf:
        if (high >= ComparisonNames.size()) {
            return core::fail(std::format("{:#06x}: comparison {} is past BTL_EFE_COMPARISONS", offset, high));
        }
        in.width = widthOf(low);
        in.comparison = static_cast<EfeComparison>(high);
        in.variable = variableAt(ops, 0);
        in.target = static_cast<std::int16_t>(ops[2]);
        break;
    case EfeOpcode::Jump:
    case EfeOpcode::Call:
        in.target = static_cast<std::int16_t>(ops[0]);
        break;
    case EfeOpcode::SubOp:
        if (in.variant() >= EfeSubOpCount) {
            return core::fail(std::format("{:#06x}: sub-op {:#04x} is past BTL_EFE_SUB_OPCODES", offset, in.variant()));
        }
        in.subOp = in.variant();
        break;
    case EfeOpcode::Spawn:
        in.ordinal = in.variant();
        in.count = static_cast<std::int16_t>(ops[0]);
        in.target = static_cast<std::int16_t>(ops[1]);
        in.stride = static_cast<std::int16_t>(ops[3]);
        break;
    case EfeOpcode::LoadRandom:
    case EfeOpcode::Stop:
    case EfeOpcode::Stop6:
    case EfeOpcode::Stop8:
    case EfeOpcode::Return:
        break;
    }
    return in;
}

core::Result<EfeScripts> decodeEfeScripts(std::span<const std::byte> payload, const EfeHeader& header) {
    EfeScripts scripts;
    const std::array<std::pair<std::int32_t, std::int32_t>, 2> ranges = {
        {{header.startScript, header.initScript}, {header.initScript, header.uvAnims}}};
    const ByteReader reader{payload};
    for (const auto& [begin, end] : ranges) {
        auto at = static_cast<std::uint32_t>(begin);
        const auto stop = static_cast<std::uint32_t>(end);
        while (at < stop) {
            // The tool aligned a script that ends on a halfword with one fill halfword.
            if (at + 2 == stop && reader.u16(at) == EfeFillHalfword && !scripts.instructions.empty() &&
                scripts.instructions.back().opcode == EfeOpcode::Return) {
                scripts.padding.push_back(at);
                break;
            }
            auto in = decodeEfeInstruction(payload, at, stop);
            if (!in) {
                return std::unexpected{in.error()};
            }
            at += static_cast<std::uint32_t>(in->operands.size() * 2) + 2;
            scripts.instructions.push_back(std::move(*in));
        }
    }
    return scripts;
}

} // namespace dwe::formats::effect
