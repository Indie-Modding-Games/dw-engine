#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/new_game_stats.hpp>
#include <dwe/formats/script/scn_file.hpp>
#include <dwe/formats/script/script_yaml.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "schema_check.hpp"

using dwe::formats::script::ArgumentKind;
using dwe::formats::script::Combine;
using dwe::formats::script::ConditionTest;
using dwe::formats::script::disassembleScript;
using dwe::formats::script::Disassembly;
using dwe::formats::script::opcodeName;
using dwe::formats::script::readScriptHeader;
using dwe::formats::script::readScriptTable;
using dwe::formats::script::ScriptSlot;
using dwe::formats::script::scriptToYaml;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// Builds a script: the section list, then code appended byte by byte.
class ScriptBuilder {
public:
    explicit ScriptBuilder(std::initializer_list<std::pair<std::uint16_t, std::uint16_t>> sections) {
        u16(static_cast<std::uint16_t>(2 + (4 * sections.size()) + 2));
        for (const auto& [id, offset] : sections) {
            u16(id);
            u16(offset);
        }
        u16(0xFFFF);
    }

    ScriptBuilder& u8(std::initializer_list<unsigned> values) {
        for (const unsigned value : values) {
            bytes_.push_back(static_cast<std::byte>(value));
        }
        return *this;
    }

    ScriptBuilder& u16(unsigned value) { return u8({value & 0xFFU, value >> 8U & 0xFFU}); }

    ScriptBuilder& u32(std::uint32_t value) { return u16(value & 0xFFFFU).u16(value >> 16U); }

    [[nodiscard]] std::size_t size() const { return bytes_.size(); }
    [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

const dwe::formats::script::Instruction* instructionAt(const Disassembly& disassembly, std::uint32_t offset) {
    const auto found = std::ranges::find(disassembly.instructions, offset, &dwe::formats::script::Instruction::offset);
    return found == disassembly.instructions.end() ? nullptr : &*found;
}

// A script with code at 12 that goes through every kind of operand, an IF, a SWITCH, a selection, a call, a
// SET_SCRIPT, both ends and packed DATA blocks after the first END_SCRIPT.
ScriptBuilder everyKindOfInstruction() {
    ScriptBuilder script{{{0xFE, 12}, {0x05, 92}}};
    script.u8({0x1E, 0x11, 5, 7});                         // 12 set_pstat, with a pad byte that is not 0
    script.u8({0x1C, 0x00}).u16(0x1234);                   // 16 set_trigger
    script.u8({0x2A, 0x00}).u32(0xFFFFFFFE);               // 20 add_money -2
    script.u8({0x4D, 0xFC}).u16(0xFF38);                   // 26 entity_set_rotation partner -200
    script.u8({0x34, 0x03}).u16(0x10);                     // 30 set_stat
    script.u8({0x19, 0x00, 0x0A, 0x00, 5, 2, 0x18, 0x00}); // 34 if pstat 5 >= 2, else goto 92
    script.u16(92).u8({0x19, 0x00});
    script.u8({0x1A, 0x00, 0x82, 0x60, 0x0D, 0x00, 0x00, 0x00}); // 46 show_textbox
    script.u8({0x13, 0x00}).u16(94);                             // 54 jump_and_link
    script.u8({0x18, 5}).u16(2).u16(66).u16(103);                // 58 switch
    script.u8({0x10, 2}).u16(92).u16(98);                        // 66 set_selection
    script.u8({0x1A, 0x00, 0x82, 0x60, 0x0D, 0x00, 0x00, 0x00, 0x12, 0x00});
    script.u8({0x16, 0x00}).u16(108);                // 82 jump_to
    script.u8({0xFB, 0x00}).u16(3).u16(4);           // 86 set_script
    script.u8({0xFE, 0x00, 0x15, 0x00, 0xFF, 0x00}); // 92 end_section, 94 jump_return, 96 end_script
    script.u8({0x6B, 0x00, 0x01, 0x02, 0xFF});       // 98 data
    script.u8({0x6B, 0x00, 0x03, 0xFF, 0x00});       // 103 data, then a pad
    script.u8({0xFF, 0x00});                         // 108 end_script
    return script;
}

// The unknowns of a script with the given code after a single section at 8.
std::vector<std::string> unknownsOf(std::initializer_list<unsigned> code) {
    ScriptBuilder script{{{0xFE, 8}}};
    script.u8(code);
    const auto disassembly = disassembleScript(script.bytes());
    REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
    return disassembly->unknowns;
}

} // namespace

TEST_CASE("the script table gives each script its bytes") {
    std::vector<std::byte> file(dwe::formats::script::ScriptTableSize + 0x14);
    const auto put = [&file](std::size_t index, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            file[(index * 4) + i] = static_cast<std::byte>(value >> (8 * i) & 0xFFU);
        }
    };
    put(0, 0x2000);
    put(1, 0x2010);
    put(2, 0x2014);
    put(3, 0xDEAD); // after the end entry: leftovers
    const auto slots = readScriptTable(file);
    REQUIRE_MESSAGE(slots.has_value(), errorOf(slots));
    CHECK(*slots == std::vector<ScriptSlot>{{.offset = 0x2000, .size = 0x10}, {.offset = 0x2010, .size = 4}});

    put(1, 0x1000);
    CHECK_FALSE(readScriptTable(file).has_value()); // backwards
    put(1, 0x3000);
    CHECK_FALSE(readScriptTable(file).has_value()); // past the end
    for (std::size_t i = 0; i < dwe::formats::script::ScriptTableSize / 4; ++i) {
        put(i, 0x2000);
    }
    CHECK_FALSE(readScriptTable(file).has_value()); // no entry ends the table
    CHECK_FALSE(readScriptTable(std::span{file}.first(0x100)).has_value());
}

TEST_CASE("a script header lists its sections") {
    ScriptBuilder script{{{0xFE, 12}, {0x31, 14}}};
    script.u8({0xFE, 0x00, 0xFF, 0x00});
    const auto header = readScriptHeader(script.bytes());
    REQUIRE_MESSAGE(header.has_value(), errorOf(header));
    CHECK(header->codeStart == 12);
    REQUIRE(header->sections.size() == 2);
    CHECK(header->sections[1].id == 0x31);
    CHECK(header->sections[1].offset == 14);

    CHECK_FALSE(readScriptHeader(std::span{script.bytes()}.first(5)).has_value());
    auto bytes = script.bytes();
    bytes[0] = std::byte{14};
    CHECK_FALSE(readScriptHeader(bytes).has_value()); // the code does not start after the list
    ScriptBuilder outside{{{0xFE, 4}}};
    outside.u8({0xFF, 0x00});
    CHECK_FALSE(readScriptHeader(outside.bytes()).has_value());
    ScriptBuilder past{{{0xFE, 10}}};
    past.u8({0xFF, 0x00});
    CHECK_FALSE(readScriptHeader(past.bytes()).has_value());
}

TEST_CASE("a script with every kind of instruction disassembles") {
    const auto script = everyKindOfInstruction();
    const auto disassembly = disassembleScript(script.bytes());
    REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
    CHECK(disassembly->unknowns.empty());
    CHECK(disassembly->codeEnd == script.size());
    CHECK(disassembly->instructions.size() == 18);
    CHECK(disassembly->labels == std::vector<std::uint16_t>{12, 66, 92, 94, 98, 103, 108});
    CHECK(disassembly->unreachable == 2); // the SET_SCRIPT after the jump and the END_SCRIPT after the return

    const auto* setPstat = instructionAt(*disassembly, 12);
    REQUIRE(setPstat != nullptr);
    CHECK(opcodeName(setPstat->opcode) == "set_pstat");
    CHECK(setPstat->size == 4);
    REQUIRE(setPstat->arguments.size() == 2);
    CHECK(setPstat->arguments[0].kind == ArgumentKind::Pstat);
    CHECK(setPstat->arguments[1].value == 7);
    REQUIRE(setPstat->pads.size() == 1);
    CHECK(setPstat->pads[0].offset == 1);
    CHECK(setPstat->pads[0].value == 0x11);

    CHECK(instructionAt(*disassembly, 16)->arguments[0].value == 0x1234);
    CHECK(instructionAt(*disassembly, 20)->arguments[0].value == -2);
    CHECK(instructionAt(*disassembly, 26)->arguments[1].value == -200);

    const auto* branch = instructionAt(*disassembly, 34);
    REQUIRE(branch != nullptr);
    CHECK(branch->size == 12);
    REQUIRE(branch->conditions.size() == 2);
    CHECK(branch->conditions[0].test == ConditionTest::Pstat);
    CHECK(branch->conditions[0].combine == Combine::Replace);
    CHECK(branch->conditions[1].test == ConditionTest::GotoIfFalse);
    CHECK(branch->conditions[1].arguments[0].value == 92);

    CHECK(instructionAt(*disassembly, 46)->text == "A");
    CHECK(instructionAt(*disassembly, 58)->targets == std::vector<std::uint16_t>{66, 103});
    const auto* selection = instructionAt(*disassembly, 66);
    REQUIRE(selection != nullptr);
    CHECK(selection->targets == std::vector<std::uint16_t>{92, 98});
    CHECK(selection->text == "A");
    CHECK(selection->selectionEndMarker);
    CHECK(selection->size == 16);
    CHECK(instructionAt(*disassembly, 86)->arguments[1].value == 4);
    CHECK(instructionAt(*disassembly, 98)->data == std::vector<std::uint8_t>{1, 2, 0xFF});
    CHECK(instructionAt(*disassembly, 103)->size == 5);
}

TEST_CASE("the listing of a script names its labels and keeps every byte's meaning") {
    const auto disassembly = disassembleScript(everyKindOfInstruction().bytes());
    REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
    const auto text = dwe::core::emitYaml(scriptToYaml(*disassembly));
    REQUIRE_MESSAGE(text.has_value(), errorOf(text));
    for (const std::string_view expected :
         {"code_start: 0x000c", "- id: 0x00fe\n  at: l_000c", "unreachable_instructions: 2", "label: l_005c",
          "op: set_pstat", "pads:\n  - offset: 1\n    value: 0x11", "entity: partner", "rotation: -200", "amount: -2",
          "- test: pstat\n    compare: ge\n    pstat: 5\n    value: 2", "target: l_005c",
          "cases:\n  - l_0042\n  - l_0067", "options:\n  - l_005c\n  - l_0062", "text: \"A\"", "cancel_marker: true",
          "data: \"01 02 ff\"", "op: set_script\n  script: 3\n  map: 4", "op: end_script"}) {
        CHECK_MESSAGE(text->find(expected) != std::string::npos, expected);
    }
    CHECK(text->find("unknowns") == std::string::npos);
}

TEST_CASE("what the interpreter would not run is an unknown, not a guess") {
    CHECK(unknownsOf({0x40, 0x00, 0xFF, 0x00}).size() == 1);             // no such opcode
    CHECK(unknownsOf({0x1E, 0x00, 0x05}).size() == 1);                   // operands past the end
    CHECK(unknownsOf({0x16, 0x00, 0x00, 0x02, 0xFF, 0x00}).size() == 1); // jump past the end: no END_SCRIPT after it
    CHECK(unknownsOf({0x16, 0x00, 0x09, 0x00, 0xFF, 0x00}).size() == 1); // jump into an instruction
    CHECK(unknownsOf({0xFE, 0x05, 0xFF, 0x00}).size() == 1);             // odd start
    CHECK(unknownsOf({0x19, 0x00, 0x28, 0x00, 0x00, 0x00, 0x19, 0x00, 0xFF, 0x00}).size() == 1); // IF test
    CHECK(unknownsOf({0x19, 0x00, 0x0E, 0x00, 0x05, 0x01, 0x19, 0x00, 0xFF, 0x00}).size() == 1); // compare 6
    CHECK(unknownsOf({0x19, 0x00, 0x20, 0x00, 0x01, 0x07, 0x00, 0x00, 0x19, 0x00, 0xFF, 0x00}).size() == 1);
    CHECK(unknownsOf({0x1A, 0x00, 0x82, 0x60}).size() == 1);                         // text that does not end
    CHECK(unknownsOf({0x10, 0x01, 0x08, 0x00, 0x00, 0x00, 0xFF, 0x00}).size() == 1); // no SHOW_TEXTBOX word
    CHECK(unknownsOf({0x6B, 0x00, 0x01, 0x02}).size() == 1);                         // DATA without its end
    CHECK(unknownsOf({0xFE, 0x00, 0xFF, 0x00}).empty());

    ScriptBuilder bad{{{0xFE, 8}}};
    bad.u8({0x40, 0x00});
    const auto disassembly = disassembleScript(bad.bytes());
    REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
    CHECK(disassembly->codeEnd == 8);
    CHECK(disassembly->instructions.empty());
    CHECK(disassembly->unknowns.front().find("0x40") != std::string::npos);
    ScriptBuilder badHeader{{{0xFE, 2}}};
    CHECK_FALSE(disassembleScript(badHeader.bytes()).has_value());
}

namespace {

// A new-game block after a single section at 8: the partner's type into variable 103, an IF on type 3 that sets
// every stat (in the order 7..0 for the first branch), and the other branch at 58; the code goes on at 90.
ScriptBuilder newGameBlock(unsigned type) {
    ScriptBuilder script{{{0xFE, 8}}};
    script.u8({0x22, 103});                                             // 8 store_digimon_type
    script.u8({0x19, 0x00, 0x08, 0x00, 103, type, 0x18, 0x00}).u16(58); // 10 if pstat 103 == type, else 58
    script.u8({0x19, 0x00});
    for (unsigned stat = 8; stat-- > 0;) {
        script.u8({0x34, stat}).u16(100 + stat); // 22 set_stat
    }
    script.u8({0x16, 0x00}).u16(90); // 54 jump_to
    for (unsigned stat = 0; stat < 8; ++stat) {
        script.u8({0x34, stat}).u16(200 + stat); // 58 set_stat
    }
    script.u8({0xFF, 0x00}); // 90 end_script
    return script;
}

} // namespace

TEST_CASE("the new-game script's stats are the SET_STAT runs of the IF on the partner's type") {
    const auto script = newGameBlock(3);
    const auto disassembly = disassembleScript(script.bytes());
    REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
    REQUIRE(disassembly->unknowns.empty());
    const auto stats = dwe::formats::script::findNewGameStats(*disassembly);
    REQUIRE_MESSAGE(stats.has_value(), errorOf(stats));
    CHECK(stats->at == 10);
    CHECK(stats->partnerType == 3);
    CHECK(stats->partner == dwe::formats::script::StartingStats{100, 101, 102, 103, 104, 105, 106, 107});
    CHECK(stats->others == dwe::formats::script::StartingStats{200, 201, 202, 203, 204, 205, 206, 207});

    const auto document = dwe::formats::script::newGameStatsToYaml(*stats, "hero");
    CHECK(dwe::formats::test::checkAgainstSchema(document, "new_game_stats").empty());
    const auto* partners = document.find("partners");
    REQUIRE(partners != nullptr);
    REQUIRE(partners->items().size() == 1);
    CHECK(partners->items()[0].find("digimon")->text() == "hero");
    CHECK(partners->items()[0].find("stats")->find("max_hp")->text() == "104");
    CHECK(document.find("others")->find("mp")->text() == "207");
}

TEST_CASE("a script without the new-game block has no new-game stats") {
    ScriptBuilder plain{{{0xFE, 8}}};
    plain.u8({0x34, 0x00}).u16(5).u8({0xFF, 0x00});
    const auto disassembly = disassembleScript(plain.bytes());
    REQUIRE_MESSAGE(disassembly.has_value(), errorOf(disassembly));
    CHECK_FALSE(dwe::formats::script::findNewGameStats(*disassembly).has_value());

    // A stat set twice in a branch leaves another unset: not the block.
    auto bytes = newGameBlock(3).bytes();
    bytes[23] = std::byte{6};
    const auto twice = disassembleScript(bytes);
    REQUIRE_MESSAGE(twice.has_value(), errorOf(twice));
    CHECK_FALSE(dwe::formats::script::findNewGameStats(*twice).has_value());
}
