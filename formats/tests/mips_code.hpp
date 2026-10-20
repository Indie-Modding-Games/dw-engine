#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Encodes the few MIPS I instructions the constant switch tests need, so the tests write small functions by hand.
namespace dwe::formats::test::mips {

inline constexpr std::uint32_t Zero = 0;
inline constexpr std::uint32_t At = 1;
inline constexpr std::uint32_t V0 = 2;
inline constexpr std::uint32_t V1 = 3;
inline constexpr std::uint32_t A0 = 4;
inline constexpr std::uint32_t S0 = 16;
inline constexpr std::uint32_t Sp = 29;
inline constexpr std::uint32_t Ra = 31;

inline constexpr std::uint32_t Nop = 0;

constexpr std::uint32_t immediateType(std::uint32_t opcode, std::uint32_t rs, std::uint32_t rt, std::int32_t value) {
    return (opcode << 26U) | (rs << 21U) | (rt << 16U) | (static_cast<std::uint32_t>(value) & 0xFFFFU);
}

constexpr std::uint32_t registerType(std::uint32_t rs, std::uint32_t rt, std::uint32_t rd, std::uint32_t shift,
                                     std::uint32_t function) {
    return (rs << 21U) | (rt << 16U) | (rd << 11U) | (shift << 6U) | function;
}

constexpr std::uint32_t addiu(std::uint32_t rt, std::uint32_t rs, std::int32_t value) {
    return immediateType(0x09, rs, rt, value);
}
constexpr std::uint32_t li(std::uint32_t rt, std::int32_t value) {
    return addiu(rt, Zero, value);
}
constexpr std::uint32_t sltiu(std::uint32_t rt, std::uint32_t rs, std::int32_t value) {
    return immediateType(0x0B, rs, rt, value);
}
constexpr std::uint32_t lui(std::uint32_t rt, std::uint32_t value) {
    return immediateType(0x0F, 0, rt, static_cast<std::int32_t>(value));
}
constexpr std::uint32_t lw(std::uint32_t rt, std::int32_t offset, std::uint32_t base) {
    return immediateType(0x23, base, rt, offset);
}
constexpr std::uint32_t sw(std::uint32_t rt, std::int32_t offset, std::uint32_t base) {
    return immediateType(0x2B, base, rt, offset);
}
// A branch `offset` instructions after its delay slot.
constexpr std::uint32_t beq(std::uint32_t rs, std::uint32_t rt, std::int32_t offset) {
    return immediateType(0x04, rs, rt, offset);
}
constexpr std::uint32_t bne(std::uint32_t rs, std::uint32_t rt, std::int32_t offset) {
    return immediateType(0x05, rs, rt, offset);
}
constexpr std::uint32_t sll(std::uint32_t rd, std::uint32_t rt, std::uint32_t shift) {
    return registerType(0, rt, rd, shift, 0x00);
}
constexpr std::uint32_t addu(std::uint32_t rd, std::uint32_t rs, std::uint32_t rt) {
    return registerType(rs, rt, rd, 0, 0x21);
}
constexpr std::uint32_t jr(std::uint32_t rs) {
    return registerType(rs, 0, 0, 0, 0x08);
}
constexpr std::uint32_t jal(std::uint32_t target) {
    return (0x03U << 26U) | ((target >> 2U) & 0x03FFFFFFU);
}

// The halves of an address for lui and addiu, which adds its immediate sign-extended.
constexpr std::uint32_t high(std::uint32_t address) {
    return ((address + 0x8000U) >> 16U) & 0xFFFFU;
}
constexpr std::int32_t low(std::uint32_t address) {
    return static_cast<std::int16_t>(address & 0xFFFFU);
}

// Places the instructions in the image and returns the address of the first.
template <typename Builder>
std::uint32_t placeCode(Builder& image, std::span<const std::uint32_t> code) {
    const std::uint32_t address = image.place(code.size() * 4);
    for (std::size_t i = 0; i < code.size(); ++i) {
        image.u32(static_cast<std::uint32_t>(address + (i * 4)), code[i]);
    }
    return address;
}

} // namespace dwe::formats::test::mips
