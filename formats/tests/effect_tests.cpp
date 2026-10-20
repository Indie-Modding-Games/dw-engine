#include <dwe/core/yaml.hpp>
#include <dwe/formats/effect/efe_analysis.hpp>
#include <dwe/formats/effect/efe_file.hpp>
#include <dwe/formats/effect/efe_script.hpp>
#include <dwe/formats/effect/efe_yaml.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "effect_fixtures.hpp"
#include "model_fixtures.hpp"

using namespace dwe::formats::effect;
using namespace dwe::formats::effect::testing;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

void putU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
    }
}

std::uint32_t getU32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
    }
    return value;
}

std::vector<std::byte> wordsToBytes(std::initializer_list<std::uint16_t> words) {
    std::vector<std::byte> bytes;
    for (const std::uint16_t word : words) {
        bytes.push_back(static_cast<std::byte>(word & 0xFFU));
        bytes.push_back(static_cast<std::byte>(word >> 8U));
    }
    return bytes;
}

struct Decoded {
    std::vector<std::byte> slot;
    EfeHeader header;
    EfeScripts scripts;
    EfeAnalysis analysis;
};

Decoded decodeSlot(const SlotParts& parts) {
    Decoded decoded{.slot = buildSlot(parts), .header = {}, .scripts = {}, .analysis = {}};
    const auto header = readEfeHeader(decoded.slot);
    REQUIRE_MESSAGE(header.has_value(), errorOf(header));
    decoded.header = *header;
    const auto payload = efePayload(decoded.slot, *header);
    const auto scripts = decodeEfeScripts(payload, *header);
    REQUIRE_MESSAGE(scripts.has_value(), errorOf(scripts));
    decoded.scripts = *scripts;
    const auto analysis = analyzeEfeScripts(payload, *header, *scripts);
    REQUIRE_MESSAGE(analysis.has_value(), errorOf(analysis));
    decoded.analysis = *analysis;
    return decoded;
}

const EfeDataItem* findItem(const EfeAnalysis& analysis, std::int16_t offset, EfeDataKind kind) {
    const auto found = std::ranges::find_if(analysis.data, [&](const EfeDataItem& item) {
        return std::cmp_equal(item.offset, offset) && item.kind == kind;
    });
    return found == analysis.data.end() ? nullptr : &*found;
}

const EfeDataItem* findArgument(const EfeAnalysis& analysis, std::int16_t offset, EfeArgKind kind) {
    const auto found = std::ranges::find_if(analysis.data, [&](const EfeDataItem& item) {
        return std::cmp_equal(item.offset, offset) && item.kind == EfeDataKind::Argument && item.argKind == kind;
    });
    return found == analysis.data.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("an effect file is whole slots of 0x5000 bytes") {
    CHECK(efeSlotCount(EfeSlotSize * 3).value_or(0) == 3);
    CHECK_FALSE(efeSlotCount((EfeSlotSize * 3) + 2).has_value());
    CHECK_FALSE(efeSlotCount(0).has_value());
    const std::vector<std::byte> file(EfeSlotSize * 2, std::byte{0xCD});
    const auto second = efeSlot(file, 1);
    REQUIRE_MESSAGE(second.has_value(), errorOf(second));
    CHECK(second->size() == EfeSlotSize);
    CHECK(isEmptyEfeSlot(*second));
    CHECK_FALSE(efeSlot(file, 2).has_value());
}

TEST_CASE("a slot's header gives each part its place, one after the other") {
    const SlotParts parts = sampleSlotParts();
    const std::vector<std::byte> slot = buildSlot(parts);
    CHECK_FALSE(isEmptyEfeSlot(slot));
    const auto header = readEfeHeader(slot);
    REQUIRE_MESSAGE(header.has_value(), errorOf(header));
    CHECK(header->startScript == static_cast<std::int32_t>(parts.data.size()));
    CHECK(header->initScript == header->startScript + static_cast<std::int32_t>(parts.startScript.size() * 2));
    CHECK(header->uvAnimsEnd - header->uvAnims == static_cast<std::int32_t>(EfeUvAnimSize));
    CHECK(header->tmdStart - header->subEffects == static_cast<std::int32_t>(EfeSubEffectSize));
    CHECK(header->tmdEnd - header->tmdStart == static_cast<std::int32_t>(parts.tmd.size()));
    CHECK(header->timEnd - header->timStart == static_cast<std::int32_t>(parts.tim.size()));
    CHECK(header->spawnCount == 1);
    CHECK(header->effectId == 0x100);
    CHECK(efePayload(slot, *header).size() == static_cast<std::size_t>(header->dataEnd));
}

TEST_CASE("a header whose parts are out of order, apart or past the slot is refused") {
    const std::vector<std::byte> good = buildSlot(sampleSlotParts());
    SUBCASE("data past the slot") {
        std::vector<std::byte> slot = good;
        putU32(slot, 0x20, 0x5000);
        putU32(slot, 0x24, 0x5000);
        CHECK_FALSE(readEfeHeader(slot).has_value());
    }
    SUBCASE("offsets out of order") {
        std::vector<std::byte> slot = good;
        putU32(slot, 0x04, getU32(slot, 0x00) - 2);
        CHECK_FALSE(readEfeHeader(slot).has_value());
    }
    SUBCASE("a negative offset") {
        std::vector<std::byte> slot = good;
        putU32(slot, 0x00, 0xFFFFFFFE);
        CHECK_FALSE(readEfeHeader(slot).has_value());
    }
    SUBCASE("a gap between the TMD and the TIM") {
        std::vector<std::byte> slot = good;
        putU32(slot, 0x1C, getU32(slot, 0x1C) + 4);
        CHECK_FALSE(readEfeHeader(slot).has_value());
    }
    SUBCASE("a UV animation room that is not whole records") {
        std::vector<std::byte> slot = good;
        putU32(slot, 0x08, getU32(slot, 0x08) + 2);
        CHECK_FALSE(readEfeHeader(slot).has_value());
    }
    SUBCASE("a sub-effect room of the wrong size") {
        std::vector<std::byte> slot = good;
        putU32(slot, 0x2C, 2);
        CHECK_FALSE(readEfeHeader(slot).has_value());
    }
    SUBCASE("a slot shorter than its header") {
        CHECK_FALSE(readEfeHeader(std::span{good}.first(0x20)).has_value());
    }
}

TEST_CASE("every opcode decodes with the length and operands its handler uses") {
    const SlotParts parts = sampleSlotParts();
    const Decoded decoded = decodeSlot(parts);
    const auto& instructions = decoded.scripts.instructions;
    std::vector<bool> seen(EfeOpcodeCount, false);
    for (const EfeInstruction& in : instructions) {
        seen[static_cast<std::size_t>(in.opcode)] = true;
        CHECK((in.operands.size() * 2) + 2 == efeInstructionSize(static_cast<std::uint8_t>(in.opcode)).value_or(0));
        for (const EfeIgnoredOperand& ignored : efeIgnoredOperands(in.opcode)) {
            CHECK_MESSAGE(in.isUsualIgnored(ignored), efeOpcodeName(in.opcode), " ", ignored.name);
        }
    }
    for (const EfeOpcode opcode :
         {EfeOpcode::LoadImmediate, EfeOpcode::LoadVariable, EfeOpcode::LoadRandom, EfeOpcode::LoadIndexed,
          EfeOpcode::ApplyOperator, EfeOpcode::Stop, EfeOpcode::BranchIf, EfeOpcode::Jump, EfeOpcode::PushImmediate,
          EfeOpcode::PushVariable, EfeOpcode::PushAddress, EfeOpcode::Call, EfeOpcode::SubOp, EfeOpcode::Return,
          EfeOpcode::PopToVariable, EfeOpcode::Spawn}) {
        CHECK_MESSAGE(seen[static_cast<std::size_t>(opcode)], efeOpcodeName(opcode));
    }

    const EfeInstruction& spawn = instructions.front();
    CHECK(spawn.opcode == EfeOpcode::Spawn);
    CHECK(spawn.count == 1);
    CHECK(spawn.target == layout::Instances);
    CHECK(spawn.stride == 8);
    CHECK(spawn.ordinal == 0);

    const auto indexed = std::ranges::find(instructions, EfeOpcode::LoadIndexed, &EfeInstruction::opcode);
    REQUIRE(indexed != instructions.end());
    CHECK(indexed->target == layout::Table);
    CHECK(indexed->width == EfeWidth::Int16);
    CHECK(indexed->indexWidth == EfeWidth::Int16);
    CHECK(indexed->variable == EfeVariable{.space = EfeSpace::Base, .offset = layout::Index, .flag = EfeBaseFlag});

    const auto pop = std::ranges::find(instructions, EfeOpcode::PopToVariable, &EfeInstruction::opcode);
    REQUIRE(pop != instructions.end());
    CHECK(pop->variable == EfeVariable{.space = EfeSpace::Instance, .offset = 4, .flag = 0});

    const auto immediate = std::ranges::find(instructions, EfeOpcode::LoadImmediate, &EfeInstruction::opcode);
    REQUIRE(immediate != instructions.end());
    CHECK(immediate->value == 5);
}

TEST_CASE("the operators, comparisons and stops of the variant byte") {
    const std::vector<std::byte> bytes = wordsToBytes({
        0x5204,
        0x0010,
        0x0000, // apply_operator: modulo on an int16 of the instance
        0x3407,
        0x0008,
        EfeBaseFlag,
        0x0000,
        EfeBaseFlag, // branch_if: less or equal on an int32 of the base
        0xCD06,
        0xCD08, // the other two stops
        0x0000,
        0xFFFF,
        0x0000, // load_immediate of -1 with a variant of 0: unusual, not wrong
    });
    const auto modulo = decodeEfeInstruction(bytes, 0, 6);
    REQUIRE_MESSAGE(modulo.has_value(), errorOf(modulo));
    CHECK(modulo->op == EfeOperator::Modulo);
    CHECK(modulo->width == EfeWidth::Int16);
    CHECK(efeOperatorName(modulo->op) == "modulo");
    const auto branch = decodeEfeInstruction(bytes, 6, 16);
    REQUIRE_MESSAGE(branch.has_value(), errorOf(branch));
    CHECK(branch->comparison == EfeComparison::LessOrEqual);
    CHECK(branch->target == 0);
    CHECK(efeComparisonName(branch->comparison) == "less_or_equal");
    CHECK(decodeEfeInstruction(bytes, 16, 18).value_or(EfeInstruction{}).opcode == EfeOpcode::Stop6);
    CHECK(decodeEfeInstruction(bytes, 18, 20).value_or(EfeInstruction{}).opcode == EfeOpcode::Stop8);
    const auto load = decodeEfeInstruction(bytes, 20, 26);
    REQUIRE_MESSAGE(load.has_value(), errorOf(load));
    CHECK(load->value == -1);
    CHECK_FALSE(load->isUsualIgnored(efeIgnoredOperands(EfeOpcode::LoadImmediate)[0]));
    CHECK_FALSE(load->isUsualIgnored(efeIgnoredOperands(EfeOpcode::LoadImmediate)[1]));
}

TEST_CASE("an instruction the interpreter has no handler for is refused") {
    SUBCASE("an opcode past the table") {
        CHECK_FALSE(decodeEfeInstruction(wordsToBytes({0x0012}), 0, 2).has_value());
    }
    SUBCASE("a sub-op past the table") {
        CHECK_FALSE(decodeEfeInstruction(wordsToBytes({0x610E}), 0, 2).has_value());
        CHECK(decodeEfeInstruction(wordsToBytes({0x600E}), 0, 2).has_value());
    }
    SUBCASE("an operator row past the table") {
        CHECK_FALSE(decodeEfeInstruction(wordsToBytes({0x0504, 0, 0}), 0, 6).has_value());
    }
    SUBCASE("a comparison past the table") {
        CHECK_FALSE(decodeEfeInstruction(wordsToBytes({0x6407, 0, 0, 0, 0}), 0, 10).has_value());
    }
    SUBCASE("an instruction cut by the end of its script") {
        CHECK_FALSE(decodeEfeInstruction(wordsToBytes({0xCD11, 1, 2}), 0, 6).has_value());
        CHECK_FALSE(decodeEfeInstruction(wordsToBytes({0x0401, 0}), 0, 6).has_value());
    }
}

TEST_CASE("a script that runs into the next part or holds an unknown opcode is refused") {
    SlotParts parts = sampleSlotParts();
    SUBCASE("truncated") {
        parts.startScript.pop_back();
        parts.startScript.pop_back();
        parts.startScript.push_back(0xCD0A);
        const std::vector<std::byte> slot = buildSlot(parts);
        const auto header = readEfeHeader(slot);
        REQUIRE_MESSAGE(header.has_value(), errorOf(header));
        CHECK_FALSE(decodeEfeScripts(efePayload(slot, *header), *header).has_value());
    }
    SUBCASE("unknown opcode") {
        parts.initScript = {0x0013};
        const std::vector<std::byte> slot = buildSlot(parts);
        const auto header = readEfeHeader(slot);
        REQUIRE_MESSAGE(header.has_value(), errorOf(header));
        CHECK_FALSE(decodeEfeScripts(efePayload(slot, *header), *header).has_value());
    }
    SUBCASE("fill after the last return is padding") {
        parts.initScript.push_back(EfeFillHalfword);
        const Decoded decoded = decodeSlot(parts);
        REQUIRE(decoded.scripts.padding.size() == 1);
        CHECK(std::cmp_equal(decoded.scripts.padding.front(), decoded.header.uvAnims - 2));
    }
}

TEST_CASE("every sub-op names its handler and its arguments") {
    for (unsigned code = 0; code < EfeSubOpCount; ++code) {
        const EfeSubOpSpec& spec = efeSubOp(static_cast<std::uint8_t>(code));
        CHECK_FALSE(spec.name.empty());
        CHECK_FALSE(spec.handler.empty());
        for (const EfeArg& arg : spec.args) {
            CHECK_FALSE(arg.name.empty());
            CHECK(arg.related < std::max<std::size_t>(spec.args.size(), 1));
        }
    }
    CHECK(efeSubOp(2).name == "set_sub_effect_scripts");
    CHECK(efeSubOp(2).args.size() == 2);
    CHECK(efeSubOp(65).args.size() == 12);
}

TEST_CASE("the analysis binds every argument and types the whole data area") {
    const Decoded decoded = decodeSlot(sampleSlotParts());
    const EfeAnalysis& analysis = decoded.analysis;
    for (const std::string& issue : analysis.issues) {
        MESSAGE(issue);
    }
    CHECK(analysis.issues.empty());
    const auto& instructions = decoded.scripts.instructions;
    for (std::size_t i = 0; i < instructions.size(); ++i) {
        if (instructions[i].opcode != EfeOpcode::SubOp) {
            continue;
        }
        const auto& args = analysis.arguments[i];
        CHECK(args.size() == efeSubOp(instructions[i].subOp).args.size());
        CHECK(std::ranges::all_of(args, [](const auto& push) { return push.has_value(); }));
    }
    // Branch, jump, call and the two sub-effect scripts.
    CHECK(analysis.labels.size() == 4);

    using enum EfeArgKind;
    using namespace layout;
    const auto sizeOf = [&](std::int16_t offset, EfeArgKind kind) {
        const EfeDataItem* item = findArgument(analysis, offset, kind);
        return item == nullptr ? 0U : item->size;
    };
    CHECK(sizeOf(Scale, Vector) == 12);
    CHECK(sizeOf(Tint, Color) == 12);
    CHECK(sizeOf(Target, Int32) == 4);
    CHECK(sizeOf(Bones, BoneOffsets) == 12);
    CHECK(sizeOf(Keyframes, UvKeyframes) == 8);
    CHECK(sizeOf(Parent, ParentField) == 4);
    CHECK(sizeOf(Radii, Int32Array) == 8);
    CHECK(sizeOf(Clut, ClutColors) == 8);
    CHECK(sizeOf(layout::ClutRow, EfeArgKind::ClutRow) == 32);
    CHECK(sizeOf(Tiles, TileMap) == 99);
    CHECK(sizeOf(layout::TileOffsets, EfeArgKind::TileOffsets) == 4);
    CHECK(sizeOf(Sprites, ParallaxSprites) == 12);
    CHECK(sizeOf(Ribbon, RibbonPoints) == 80);
    CHECK(sizeOf(Corners, QuadCorner) == 8);

    const EfeDataItem* instances = findItem(analysis, Instances, EfeDataKind::Instances);
    REQUIRE(instances != nullptr);
    CHECK(instances->size == 8);
    const EfeDataItem* table = findItem(analysis, Table, EfeDataKind::Table);
    REQUIRE(table != nullptr);
    CHECK(table->size == 6);
    CHECK(findItem(analysis, Counter, EfeDataKind::Variable) != nullptr);
    CHECK(findItem(analysis, Fill, EfeDataKind::Fill) != nullptr);
    CHECK(std::ranges::none_of(analysis.data,
                               [](const EfeDataItem& item) { return item.kind == EfeDataKind::Unreferenced; }));
}

TEST_CASE("data nothing reaches is unused, not an issue; fill is fill") {
    SlotParts parts = sampleSlotParts();
    parts.data[static_cast<std::size_t>(layout::Fill)] = std::byte{0x42};
    const Decoded decoded = decodeSlot(parts);
    CHECK(decoded.analysis.issues.empty());
    const EfeDataItem* unused = findItem(decoded.analysis, layout::Fill, EfeDataKind::Unused);
    REQUIRE(unused != nullptr);
    CHECK(unused->size == 1);

    const auto payload = efePayload(decoded.slot, decoded.header);
    const auto text = dwe::core::emitYaml(efeScriptToYaml(payload, decoded.header, decoded.scripts, decoded.analysis));
    REQUIRE_MESSAGE(text.has_value(), errorOf(text));
    CHECK(text->find("kind: unused") != std::string::npos);
    CHECK(text->find("bytes: \"42\"") != std::string::npos);
    CHECK(text->find("reason: ") != std::string::npos);
}

namespace {

// A slot whose only sub-op loads a CLUT row from Row at a colour index read from Index, which `computeIndex` writes;
// the 6 bytes after the colours the index can reach are not fill.
namespace clut {
constexpr std::int16_t Index = 0x00;
constexpr std::int16_t Row = 0x04;
constexpr std::int16_t End = 0x30;
} // namespace clut

template <typename Write>
Decoded decodeClutRowSlot(const Write& computeIndex) {
    SlotParts parts = sampleSlotParts();
    parts.data.assign(static_cast<std::size_t>(clut::End), std::byte{0x11});
    std::fill_n(parts.data.begin(), 4, std::byte{0xCD});
    ScriptWriter w{static_cast<std::uint32_t>(clut::End)};
    computeIndex(w);
    w.pushImmediate(10000);
    w.pushImmediate(2);
    w.pushAddress(base(clut::Row));
    w.pushVariable(base(clut::Index), 4);
    w.sub(80);
    w.ret();
    parts.startScript = w.words();
    ScriptWriter init{static_cast<std::uint32_t>(clut::End + (parts.startScript.size() * 2))};
    init.ret();
    parts.initScript = init.words();
    parts.subEffectCount = 0;
    parts.spawnCount = 0;
    return decodeSlot(parts);
}

constexpr unsigned Int32Row = 4;
constexpr unsigned SetOperation = 0;
constexpr unsigned ModuloOperation = 5;

} // namespace

TEST_CASE("a CLUT row at an index the scripts take modulo n spans the n rows it can start at") {
    const Decoded decoded = decodeClutRowSlot([](ScriptWriter& w) {
        w.loadVariable(instance(0), 4);
        w.applyOperator(Int32Row, SetOperation, base(clut::Index));
        w.loadImmediate(4);
        w.applyOperator(Int32Row, ModuloOperation, base(clut::Index));
    });
    CHECK(decoded.analysis.issues.empty());
    const EfeDataItem* row = findArgument(decoded.analysis, clut::Row, EfeArgKind::ClutRow);
    REQUIRE(row != nullptr);
    CHECK(row->size == 32 + (3 * 2));
    CHECK(row->count == 4);
    CHECK(row->bounded);
    const EfeDataItem* rest =
        findItem(decoded.analysis, static_cast<std::int16_t>(clut::Row + 38), EfeDataKind::Unused);
    REQUIRE(rest != nullptr);
    CHECK(rest->size == 6);

    const auto payload = efePayload(decoded.slot, decoded.header);
    const auto text = dwe::core::emitYaml(efeScriptToYaml(payload, decoded.header, decoded.scripts, decoded.analysis));
    REQUIRE_MESSAGE(text.has_value(), errorOf(text));
    CHECK(text->find("row_starts: 4") != std::string::npos);
}

TEST_CASE("a CLUT row at an index the scripts do not bound leaves the data after it unreferenced") {
    // A write after the modulo, or a label between it and the push, loses the bound.
    const auto check = [](const Decoded& decoded) {
        const EfeDataItem* row = findArgument(decoded.analysis, clut::Row, EfeArgKind::ClutRow);
        REQUIRE(row != nullptr);
        CHECK(row->size == 0);
        CHECK_FALSE(row->bounded);
        CHECK(findItem(decoded.analysis, clut::Row, EfeDataKind::Unreferenced) != nullptr);
        REQUIRE(decoded.analysis.issues.size() == 1);
        CHECK(decoded.analysis.issues.front().find("no known end") != std::string::npos);
    };
    check(decodeClutRowSlot([](ScriptWriter& w) {
        w.loadImmediate(4);
        w.applyOperator(Int32Row, ModuloOperation, base(clut::Index));
        w.loadImmediate(1);
        w.applyOperator(Int32Row, 1, base(clut::Index));
    }));
    check(decodeClutRowSlot([](ScriptWriter& w) {
        w.loadImmediate(4);
        w.applyOperator(Int32Row, ModuloOperation, base(clut::Index));
        w.jump(w.here() + 6);
    }));
}

TEST_CASE("addresses outside the data area and lists without an end are issues, not crashes") {
    SlotParts parts = sampleSlotParts();
    const auto start = static_cast<std::uint32_t>(parts.data.size());
    ScriptWriter w{start};
    w.pushAddress(base(0x7000));
    w.sub(0);
    w.pushAddress(base(layout::Fill));
    w.sub(0);
    w.ret();
    parts.startScript = w.words();
    ScriptWriter init{static_cast<std::uint32_t>(start + (parts.startScript.size() * 2))};
    init.ret();
    parts.initScript = init.words();
    const Decoded decoded = decodeSlot(parts);
    CHECK(decoded.analysis.issues.size() >= 2);
}

TEST_CASE("the scripts as YAML: names, labels and arguments") {
    const Decoded decoded = decodeSlot(sampleSlotParts());
    const auto payload = efePayload(decoded.slot, decoded.header);
    const auto text = dwe::core::emitYaml(efeScriptToYaml(payload, decoded.header, decoded.scripts, decoded.analysis));
    REQUIRE_MESSAGE(text.has_value(), errorOf(text));
    for (const std::string_view expected :
         {"label: \"start\"", "label: \"init\"", "op: spawn", "sub: set_sub_effect_scripts", "tick_script: \"l_",
          "bone_offsets: \"&base+0x0030\"", "kind: bone_offsets", "kind: tile_map", "kind: instances",
          "comparison: equal", "operator: set", "kind: fill", "semi_transparency: -1", "declared_width: 4"}) {
        CHECK_MESSAGE(text->find(expected) != std::string::npos, expected);
    }
    const auto header = dwe::core::emitYaml(efeHeaderToYaml(decoded.header));
    REQUIRE_MESSAGE(header.has_value(), errorOf(header));
    CHECK(header->find("effect_id: \"0x0100\"") != std::string::npos);
    CHECK(header->find("spawn_count: 1") != std::string::npos);
}

TEST_CASE("the VRAM cells of loaded effects") {
    const auto first = efeVramCell(0);
    REQUIRE_MESSAGE(first.has_value(), errorOf(first));
    CHECK(first->pixelPage == 0x19);
    CHECK(first->pixelX == 576);
    CHECK(first->pixelY == 256);
    CHECK(first->clutX == 128);
    CHECK(first->clutY == 488);
    const auto ninth = efeVramCell(9);
    REQUIRE_MESSAGE(ninth.has_value(), errorOf(ninth));
    CHECK(ninth->pixelPage == 0x1A);
    CHECK(ninth->pixelX == 640);
    CHECK(ninth->pixelY == 288);
    CHECK(ninth->clutX == 160);
    CHECK(ninth->clutY == 494);
    CHECK_FALSE(efeVramCell(EfeVramCellCount).has_value());
}

TEST_CASE("a TMD placed in a cell gets its page, UV offset and CLUT; untextured primitives end the walk") {
    std::vector<std::byte> tmd = dwe::formats::model::testing::buildSampleTmd();
    const std::vector<std::byte> stored = tmd;
    const auto cell = efeVramCell(9);
    REQUIRE(cell.has_value());
    const auto placed = placeEfeModel(tmd, *cell);
    REQUIRE_MESSAGE(placed.has_value(), errorOf(placed));
    // The first object's first primitive starts after its vertices and normals: 4 + 2 of 8 bytes.
    const std::size_t primitive = 12 + (2 * 28) + (6 * 8);
    CHECK(getU32(tmd, primitive + 4) == ((0x7A40U + 0x182U) << 16U | 0x2000U));
    CHECK(getU32(tmd, primitive + 8) == (0x001AU << 16U | 0x20FFU));
    CHECK(getU32(tmd, primitive + 12) == (0x7F00U + 0x2000U));
    // The second object starts with an untextured quad, so nothing of it moves.
    const std::size_t second = 12 + (2 * 28) + (6 * 8) + ((7 + 8) * 4) + (4 * 8) + (1 * 8);
    CHECK(std::ranges::equal(std::span{tmd}.subspan(second), std::span{stored}.subspan(second)));

    const auto first = efeVramCell(0);
    REQUIRE(first.has_value());
    std::vector<std::byte> truncated = std::vector<std::byte>(stored.begin(), stored.begin() + 40);
    CHECK_FALSE(placeEfeModel(truncated, *first).has_value());
}

TEST_CASE("a TMD's parts cover it; anything else is a gap") {
    const std::vector<std::byte> tmd = dwe::formats::model::testing::buildSampleTmd();
    const auto none = efeModelGaps(tmd);
    REQUIRE_MESSAGE(none.has_value(), errorOf(none));
    CHECK(none->empty());
    std::vector<std::byte> longer = tmd;
    longer.resize(tmd.size() + 6, std::byte{0});
    const auto gaps = efeModelGaps(longer);
    REQUIRE_MESSAGE(gaps.has_value(), errorOf(gaps));
    REQUIRE(gaps->size() == 1);
    CHECK(gaps->front() == EfeByteRange{.offset = static_cast<std::uint32_t>(tmd.size()), .size = 6});
    CHECK_FALSE(efeModelGaps(std::span{tmd}.first(30)).has_value());
}
