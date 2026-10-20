#include <dwe/formats/exe/constant_switch.hpp>
#include <dwe/formats/exe/memory_image.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <vector>

#include "exe_fixture.hpp"
#include "mips_code.hpp"

using dwe::formats::exe::ConstantSwitch;
using dwe::formats::exe::evaluateConstantSwitch;
using dwe::formats::exe::MemoryImage;
using dwe::formats::exe::MipsRegister;
using dwe::formats::exe::ValueLocation;
using dwe::formats::test::ImageBuilder;
using namespace dwe::formats::test::mips;

namespace {

constexpr std::uint32_t Base = 0x80010000;
constexpr std::array V0Only{ValueLocation::inRegister(MipsRegister::V0)};
constexpr std::array S0Only{ValueLocation::inRegister(MipsRegister::S0)};

// A function of `code` in an image of its own, with its join `joinIndex` instructions in.
struct SwitchFixture {
    std::vector<std::byte> file;
    ConstantSwitch code;
};

SwitchFixture makeSwitch(std::span<const std::uint32_t> code, std::size_t joinIndex,
                         std::span<const ValueLocation> values) {
    ImageBuilder image{Base};
    const std::uint32_t entry = placeCode(image, code);
    return {.file = image.executable(),
            .code = {.function = entry, .join = static_cast<std::uint32_t>(entry + (joinIndex * 4)), .values = values}};
}

std::vector<std::int32_t> run(const SwitchFixture& fixture, std::uint32_t argument) {
    const auto image = MemoryImage::fromExecutable(fixture.file);
    REQUIRE(image.has_value());
    const auto values = evaluateConstantSwitch(*image, fixture.code, argument);
    REQUIRE_MESSAGE(values.has_value(), (values ? std::string{} : values.error().message));
    return *values;
}

bool fails(const SwitchFixture& fixture, std::uint32_t argument) {
    const auto image = MemoryImage::fromExecutable(fixture.file);
    REQUIRE(image.has_value());
    return !evaluateConstantSwitch(*image, fixture.code, argument).has_value();
}

} // namespace

TEST_CASE("a chain of compares picks the argument's case, and a branch's delay slot runs before it lands") {
    const std::array code{
        li(V0, 5),          // 0: the default
        li(At, 7),          // 1
        beq(A0, At, 3),     // 2: to 6
        Nop,                // 3
        beq(Zero, Zero, 3), // 4: to the join
        Nop,                // 5
        beq(Zero, Zero, 1), // 6: case 7, to the join
        li(V0, 40),         // 7: its delay slot
        jr(Ra),             // 8: the join
        Nop,
    };
    const auto fixture = makeSwitch(code, 8, V0Only);
    CHECK(run(fixture, 7) == std::vector<std::int32_t>{40});
    CHECK(run(fixture, 3) == std::vector<std::int32_t>{5});
}

TEST_CASE("a jump table in the image picks the argument's case, and an argument outside it takes the default") {
    ImageBuilder image{Base};
    const std::uint32_t table = image.place(8);
    const std::uint32_t entry = image.place(std::size_t{20} * 4);
    const std::array code{
        addiu(V0, A0, -10),   // 0: the first case is 10
        sltiu(At, V0, 2),     // 1: two cases
        beq(At, Zero, 9),     // 2: to the default at 12
        Nop,                  // 3
        lui(V1, high(table)), // 4
        addiu(V1, V1, low(table)),
        sll(V0, V0, 2),
        addu(V0, V0, V1),
        lw(V0, 0, V0), // 8
        Nop,
        jr(V0), // 10
        Nop,
        beq(Zero, Zero, 5), // 12: the default, to the join
        li(S0, -1),
        beq(Zero, Zero, 3), // 14: case 10
        li(S0, 100),
        beq(Zero, Zero, 1), // 16: case 11
        li(S0, 200),
        jr(Ra), // 18: the join
        Nop,
    };
    for (std::size_t i = 0; i < code.size(); ++i) {
        image.u32(static_cast<std::uint32_t>(entry + (i * 4)), code[i]);
    }
    image.u32(table, entry + (14 * 4));
    image.u32(table + 4, entry + (16 * 4));
    const SwitchFixture fixture{.file = image.executable(),
                                .code = {.function = entry, .join = entry + (18 * 4), .values = S0Only}};
    CHECK(run(fixture, 10) == std::vector<std::int32_t>{100});
    CHECK(run(fixture, 11) == std::vector<std::int32_t>{200});
    CHECK(run(fixture, 5) == std::vector<std::int32_t>{-1});
}

TEST_CASE("a local kept in the stack frame is read at its offset from the stack pointer at the join") {
    const std::array code{
        addiu(Sp, Sp, -16), li(V0, 9), sw(V0, 8, Sp), lw(V1, 8, Sp), sw(V1, 12, Sp), jr(Ra), Nop,
    };
    constexpr std::array Locations{ValueLocation::onStack(8), ValueLocation::onStack(12),
                                   ValueLocation::inRegister(MipsRegister::V1)};
    CHECK(run(makeSwitch(code, 5, Locations), 0) == std::vector<std::int32_t>{9, 9, 9});
}

TEST_CASE("a case that does more than set values is reported, never guessed") {
    SUBCASE("it calls a function") {
        const std::array code{li(V0, 1), jal(Base), Nop, jr(Ra), Nop};
        CHECK(fails(makeSwitch(code, 3, V0Only), 0));
    }
    SUBCASE("it branches on a register the run does not know") {
        const std::array code{bne(S0, A0, 1), Nop, li(V0, 1), jr(Ra), Nop};
        CHECK(fails(makeSwitch(code, 3, V0Only), 0));
    }
    SUBCASE("a local it reads was never set") {
        const std::array code{li(V0, 1), jr(Ra), Nop};
        CHECK(fails(makeSwitch(code, 1, S0Only), 0));
    }
    SUBCASE("it never reaches the join") {
        const std::array code{beq(Zero, Zero, -1), Nop, jr(Ra), Nop};
        CHECK(fails(makeSwitch(code, 2, V0Only), 0));
    }
    SUBCASE("the switch is not located") {
        const std::array code{li(V0, 1), jr(Ra), Nop};
        auto fixture = makeSwitch(code, 1, V0Only);
        fixture.code = {};
        CHECK(fails(fixture, 0));
    }
}
