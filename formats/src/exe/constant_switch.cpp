#include <dwe/formats/exe/constant_switch.hpp>

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>

#include "messages.hpp"

namespace dwe::formats::exe {

namespace {

// A stack pointer for the run. The frame's words are kept apart from the image, so its value only has to be known.
constexpr std::uint32_t StackPointerStart = 0x801FFF00;

// More than any case of a switch needs; a path this long is a loop.
constexpr std::size_t MaxSteps = 4096;

constexpr std::uint32_t InstructionSize = 4;
constexpr std::size_t RegisterCount = 32;

enum class Opcode : std::uint8_t {
    Special = 0x00,
    Beq = 0x04,
    Bne = 0x05,
    Addi = 0x08,
    Addiu = 0x09,
    Slti = 0x0A,
    Sltiu = 0x0B,
    Andi = 0x0C,
    Ori = 0x0D,
    Lui = 0x0F,
    Lw = 0x23,
    Sw = 0x2B,
};

enum class Function : std::uint8_t {
    Sll = 0x00,
    Srl = 0x02,
    Sra = 0x03,
    Jr = 0x08,
    Add = 0x20,
    Addu = 0x21,
    Sub = 0x22,
    Subu = 0x23,
    And = 0x24,
    Or = 0x25,
};

// The fields of a MIPS I instruction word.
struct Instruction {
    std::uint32_t word = 0;

    [[nodiscard]] Opcode opcode() const { return static_cast<Opcode>(word >> 26U); }
    [[nodiscard]] Function function() const { return static_cast<Function>(word & 0x3FU); }
    [[nodiscard]] std::size_t rs() const { return (word >> 21U) & 0x1FU; }
    [[nodiscard]] std::size_t rt() const { return (word >> 16U) & 0x1FU; }
    [[nodiscard]] std::size_t rd() const { return (word >> 11U) & 0x1FU; }
    [[nodiscard]] std::uint32_t shift() const { return (word >> 6U) & 0x1FU; }
    [[nodiscard]] std::uint32_t unsignedImmediate() const { return word & 0xFFFFU; }
    [[nodiscard]] std::uint32_t signedImmediate() const {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word & 0xFFFFU)));
    }
    [[nodiscard]] bool isBranch() const {
        return opcode() == Opcode::Beq || opcode() == Opcode::Bne ||
               (opcode() == Opcode::Special && function() == Function::Jr);
    }
};

using Word = std::optional<std::uint32_t>;

// Applies a binary operation to two words that are both known; unknown otherwise.
template <typename Operation>
Word combine(Word lhs, Word rhs, Operation operation) {
    if (!lhs || !rhs) {
        return std::nullopt;
    }
    return operation(*lhs, *rhs);
}

std::uint32_t shiftRightArithmetic(std::uint32_t value, std::uint32_t amount) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(value) >> amount);
}

// Runs one path of a switch: registers and the stack frame, with unknown values where the run cannot know them.
class SwitchRun {
public:
    SwitchRun(const MemoryImage& image, std::uint32_t entry, std::uint32_t argument) : image_(&image), pc_(entry) {
        registers_.fill(std::nullopt);
        registers_[static_cast<std::size_t>(MipsRegister::Zero)] = 0;
        registers_[static_cast<std::size_t>(MipsRegister::A0)] = argument;
        registers_[static_cast<std::size_t>(MipsRegister::Sp)] = StackPointerStart;
    }

    // Steps until the program counter reaches `join`.
    core::Result<void> runTo(std::uint32_t join) {
        for (std::size_t step = 0; step < MaxSteps; ++step) {
            if (pc_ == join) {
                return {};
            }
            if (auto stepped = stepAt(pc_); !stepped) {
                return std::unexpected{stepped.error()};
            }
        }
        return core::fail(messages::formatMessage(messages::NoJoin, MaxSteps));
    }

    // The value of a local at the join, as a signed number.
    [[nodiscard]] core::Result<std::int32_t> valueAt(const ValueLocation& location, std::size_t index) const {
        const Word value = location.kind == ValueLocation::Kind::Register
                               ? registers_[static_cast<std::size_t>(location.reg)]
                               : stackWord(stackPointer() + static_cast<std::uint32_t>(location.stackOffset));
        if (!value) {
            return core::fail(messages::formatMessage(messages::UnknownLocal, index));
        }
        return static_cast<std::int32_t>(*value);
    }

private:
    [[nodiscard]] std::uint32_t stackPointer() const {
        return registers_[static_cast<std::size_t>(MipsRegister::Sp)].value_or(StackPointerStart);
    }

    [[nodiscard]] Word stackWord(std::uint32_t address) const {
        const auto found = stack_.find(address);
        return found == stack_.end() ? std::nullopt : found->second;
    }

    [[nodiscard]] core::Result<Instruction> fetch(std::uint32_t address) const {
        auto bytes = image_->bytesAt(address, InstructionSize);
        if (!bytes) {
            return std::unexpected{bytes.error()};
        }
        return Instruction{.word = littleEndianWord(*bytes)};
    }

    static std::uint32_t littleEndianWord(std::span<const std::byte> bytes) {
        std::uint32_t word = 0;
        for (std::size_t i = 0; i < InstructionSize; ++i) {
            word |= std::to_integer<std::uint32_t>(bytes[i]) << (8U * i);
        }
        return word;
    }

    // Runs the instruction at `address`, and for a branch its delay slot, then moves the program counter on.
    core::Result<void> stepAt(std::uint32_t address) {
        auto instruction = fetch(address);
        if (!instruction) {
            return std::unexpected{instruction.error()};
        }
        if (!instruction->isBranch()) {
            if (auto done = execute(*instruction, address); !done) {
                return done;
            }
            pc_ = address + InstructionSize;
            return {};
        }
        auto target = branchTarget(*instruction, address);
        if (!target) {
            return std::unexpected{target.error()};
        }
        const std::uint32_t slot = address + InstructionSize;
        auto delayed = fetch(slot);
        if (!delayed) {
            return std::unexpected{delayed.error()};
        }
        if (delayed->isBranch()) {
            return core::fail(messages::formatMessage(messages::BranchInDelaySlot, slot));
        }
        if (auto done = execute(*delayed, slot); !done) {
            return done;
        }
        pc_ = target->value_or(slot + InstructionSize);
        return {};
    }

    // Where a branch goes: its target if it is taken, nothing if it falls through. Read before the delay slot runs.
    core::Result<std::optional<std::uint32_t>> branchTarget(const Instruction& instruction, std::uint32_t address) {
        if (instruction.opcode() == Opcode::Special) {
            const Word target = registers_[instruction.rs()];
            if (!target) {
                return core::fail(messages::formatMessage(messages::UnknownBranch, address));
            }
            return target;
        }
        const Word lhs = registers_[instruction.rs()];
        const Word rhs = registers_[instruction.rt()];
        if (!lhs || !rhs) {
            return core::fail(messages::formatMessage(messages::UnknownBranch, address));
        }
        const bool equal = *lhs == *rhs;
        const bool taken = instruction.opcode() == Opcode::Beq ? equal : !equal;
        if (!taken) {
            return std::optional<std::uint32_t>{};
        }
        return std::optional<std::uint32_t>{address + InstructionSize + (instruction.signedImmediate() << 2U)};
    }

    void write(std::size_t reg, Word value) {
        if (reg != static_cast<std::size_t>(MipsRegister::Zero)) {
            registers_[reg] = value;
        }
    }

    core::Result<void> execute(const Instruction& instruction, std::uint32_t address) {
        const Word rs = registers_[instruction.rs()];
        const Word rt = registers_[instruction.rt()];
        const Word immediate = instruction.signedImmediate();
        switch (instruction.opcode()) {
        case Opcode::Special:
            return executeSpecial(instruction, address);
        case Opcode::Addi:
        case Opcode::Addiu:
            write(instruction.rt(), combine(rs, immediate, [](auto a, auto b) { return a + b; }));
            return {};
        case Opcode::Slti:
            write(instruction.rt(), combine(rs, immediate, [](auto a, auto b) {
                      return static_cast<std::uint32_t>(static_cast<std::int32_t>(a) < static_cast<std::int32_t>(b));
                  }));
            return {};
        case Opcode::Sltiu:
            write(instruction.rt(),
                  combine(rs, immediate, [](auto a, auto b) { return static_cast<std::uint32_t>(a < b); }));
            return {};
        case Opcode::Andi:
            write(instruction.rt(),
                  combine(rs, Word{instruction.unsignedImmediate()}, [](auto a, auto b) { return a & b; }));
            return {};
        case Opcode::Ori:
            write(instruction.rt(),
                  combine(rs, Word{instruction.unsignedImmediate()}, [](auto a, auto b) { return a | b; }));
            return {};
        case Opcode::Lui:
            write(instruction.rt(), instruction.unsignedImmediate() << 16U);
            return {};
        case Opcode::Lw:
            return load(instruction, address);
        case Opcode::Sw:
            if (!rs) {
                return core::fail(messages::formatMessage(messages::UnknownAddress, address));
            }
            stack_[*rs + *immediate] = rt;
            return {};
        default:
            return core::fail(messages::formatMessage(messages::UnsupportedInstruction, instruction.word, address));
        }
    }

    core::Result<void> executeSpecial(const Instruction& instruction, std::uint32_t address) {
        const Word rs = registers_[instruction.rs()];
        const Word rt = registers_[instruction.rt()];
        const std::uint32_t shift = instruction.shift();
        switch (instruction.function()) {
        case Function::Sll:
            write(instruction.rd(), rt ? Word{*rt << shift} : std::nullopt);
            return {};
        case Function::Srl:
            write(instruction.rd(), rt ? Word{*rt >> shift} : std::nullopt);
            return {};
        case Function::Sra:
            write(instruction.rd(), rt ? Word{shiftRightArithmetic(*rt, shift)} : std::nullopt);
            return {};
        case Function::Add:
        case Function::Addu:
            write(instruction.rd(), combine(rs, rt, [](auto a, auto b) { return a + b; }));
            return {};
        case Function::Sub:
        case Function::Subu:
            write(instruction.rd(), combine(rs, rt, [](auto a, auto b) { return a - b; }));
            return {};
        case Function::And:
            write(instruction.rd(), combine(rs, rt, [](auto a, auto b) { return a & b; }));
            return {};
        case Function::Or:
            write(instruction.rd(), combine(rs, rt, [](auto a, auto b) { return a | b; }));
            return {};
        default:
            return core::fail(messages::formatMessage(messages::UnsupportedInstruction, instruction.word, address));
        }
    }

    // A word of the stack frame if the run wrote it, otherwise of the image (a jump table).
    core::Result<void> load(const Instruction& instruction, std::uint32_t address) {
        const Word base = registers_[instruction.rs()];
        if (!base) {
            return core::fail(messages::formatMessage(messages::UnknownAddress, address));
        }
        const std::uint32_t target = *base + instruction.signedImmediate();
        if (const auto found = stack_.find(target); found != stack_.end()) {
            write(instruction.rt(), found->second);
            return {};
        }
        auto bytes = image_->bytesAt(target, InstructionSize);
        if (!bytes) {
            return std::unexpected{bytes.error()};
        }
        write(instruction.rt(), littleEndianWord(*bytes));
        return {};
    }

    const MemoryImage* image_;
    std::uint32_t pc_;
    std::array<Word, RegisterCount> registers_{};
    std::map<std::uint32_t, Word> stack_;
};

} // namespace

core::Result<std::vector<std::int32_t>> evaluateConstantSwitch(const MemoryImage& image, const ConstantSwitch& code,
                                                               std::uint32_t argument) {
    if (!code.located()) {
        return core::fail(std::string{messages::SwitchNotLocated});
    }
    SwitchRun run{image, code.function, argument};
    if (auto reached = run.runTo(code.join); !reached) {
        return std::unexpected{reached.error()};
    }
    std::vector<std::int32_t> values;
    values.reserve(code.values.size());
    for (std::size_t i = 0; i < code.values.size(); ++i) {
        auto value = run.valueAt(code.values[i], i);
        if (!value) {
            return std::unexpected{value.error()};
        }
        values.push_back(*value);
    }
    return values;
}

} // namespace dwe::formats::exe
