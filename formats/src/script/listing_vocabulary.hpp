#pragma once

#include <dwe/formats/script/disassembly.hpp>

#include <array>
#include <cstdint>
#include <string_view>

// The words and codes a listing uses for what is not a plain number (script_yaml.cpp writes them,
// script_listing.cpp reads them back).
namespace dwe::formats::script::vocabulary {

// script_value.c: scriptCompareValue, by the low bits of a variable test or a "compare" operand.
inline constexpr std::array<std::string_view, 6> CompareNames = {"eq", "ne", "ge", "le", "gt", "lt"};

// scriptIdToEntityId: these ids are not NPCs.
inline constexpr std::int32_t PartnerEntity = 0xFC;
inline constexpr std::int32_t TamerEntity = 0xFD;
inline constexpr std::string_view PartnerName = "partner";
inline constexpr std::string_view TamerName = "tamer";

// The bits of an IF entry's code besides the test (script_interp.c: scriptIfInstruction).
inline constexpr std::uint8_t AndBit = 0x80;
inline constexpr std::uint8_t OrBit = 0x40;
inline constexpr std::uint8_t LowBits = 0x07;
inline constexpr std::uint8_t ClearBit = 0x01; // a trigger test with low bits set tests that it is clear

struct TestInfo {
    std::string_view name;
    std::uint8_t kind = 0; // the code & 0x38 bits
    std::uint8_t low = 0;  // the low bits of an extended test
};

inline constexpr std::array<ConditionTest, 10> Tests = {
    ConditionTest::Trigger,   ConditionTest::Pstat, ConditionTest::GotoIfTrue, ConditionTest::GotoIfFalse,
    ConditionTest::Stat,      ConditionTest::Card,  ConditionTest::Move,       ConditionTest::Condition,
    ConditionTest::ItemCount, ConditionTest::Money};

constexpr TestInfo testInfo(ConditionTest test) {
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

// The keys of an IF entry.
inline constexpr std::string_view TestKey = "test";
inline constexpr std::string_view ClearKey = "clear";
inline constexpr std::string_view CompareKey = "compare";
inline constexpr std::string_view CombineKey = "combine";
inline constexpr std::string_view CodeKey = "code";
inline constexpr std::string_view AndName = "and";
inline constexpr std::string_view OrName = "or";

constexpr std::string_view combineName(Combine combine) {
    switch (combine) {
    case Combine::And:
        return AndName;
    case Combine::Or:
        return OrName;
    case Combine::Replace:
        break;
    }
    return {};
}

// The code an IF entry has when nothing but its fields sets it: the test, `variant` (ClearBit for a trigger test
// of a clear trigger, the comparison of a variable test, 0 otherwise) and the combination.
constexpr std::uint8_t entryCode(ConditionTest test, std::uint8_t variant, Combine combine) {
    const TestInfo info = testInfo(test);
    std::uint8_t code = info.kind | info.low | variant;
    if (combine == Combine::And) {
        code |= AndBit;
    } else if (combine == Combine::Or) {
        code |= OrBit;
    }
    return code;
}

} // namespace dwe::formats::script::vocabulary
