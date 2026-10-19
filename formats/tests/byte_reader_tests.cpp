#include <dwe/formats/byte_reader.hpp>

#include <doctest/doctest.h>

#include <array>
#include <limits>

using dwe::formats::ByteReader;

namespace {

constexpr std::array<std::byte, 8> Bytes = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x84},
                                            std::byte{'A'},  std::byte{'B'},  std::byte{0x00}, std::byte{'C'}};

} // namespace

TEST_CASE("fields are read little-endian") {
    const ByteReader reader{Bytes};
    CHECK(reader.u8(3) == 0x84);
    CHECK(reader.u16(0) == 0x0201);
    CHECK(reader.u32(0) == 0x84030201U);
    CHECK(reader.s32(0) == static_cast<std::int32_t>(0x84030201U));
}

TEST_CASE("reads that do not fit return nothing") {
    const ByteReader reader{Bytes};
    CHECK(reader.u32(4).has_value());
    CHECK_FALSE(reader.u32(5).has_value());
    CHECK_FALSE(reader.u8(8).has_value());
    CHECK_FALSE(reader.u16(std::numeric_limits<std::size_t>::max()).has_value());
    CHECK_FALSE(reader.bytes(7, 2).has_value());
    CHECK(reader.bytes(8, 0).has_value());
    CHECK_FALSE(reader.bytes(9, 0).has_value());
    CHECK_FALSE(reader.contains(1, std::numeric_limits<std::size_t>::max()));
}

TEST_CASE("C strings need a NUL inside the buffer and the length limit") {
    const ByteReader reader{Bytes};
    CHECK(reader.cString(4, 8) == "AB");
    CHECK(reader.cString(4, 2) == "AB");
    CHECK_FALSE(reader.cString(4, 1).has_value());
    CHECK_FALSE(reader.cString(7, 8).has_value());
    CHECK_FALSE(reader.cString(9, 8).has_value());
    CHECK(reader.chars(4, 2) == "AB");
}
