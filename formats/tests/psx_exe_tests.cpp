#include <dwe/formats/exe/psx_exe.hpp>

#include <doctest/doctest.h>

#include "disc_fixtures.hpp"

using namespace dwe::formats::exe;
using fixtures::FakeExecutable;

TEST_CASE("the header places the text segment in memory") {
    const auto exe = FakeExecutable::build({});
    const auto header = readPsxExeHeader(exe);
    REQUIRE(header.has_value());
    CHECK(header->pc0 == FakeExecutable::LoadAddress);
    CHECK(header->tAddr == FakeExecutable::LoadAddress);
    CHECK(header->tSize == FakeExecutable::TextSize);

    CHECK(fileOffsetOf(*header, FakeExecutable::LoadAddress, 4) == PsxExeHeaderSize);
    CHECK(fileOffsetOf(*header, FakeExecutable::LoadAddress + 0x10, 4) == PsxExeHeaderSize + 0x10);
    CHECK_FALSE(fileOffsetOf(*header, FakeExecutable::LoadAddress - 1, 1).has_value());
    CHECK(fileOffsetOf(*header, FakeExecutable::LoadAddress + 0xFFC, 4).has_value());
    CHECK_FALSE(fileOffsetOf(*header, FakeExecutable::LoadAddress + 0xFFD, 4).has_value());
    CHECK_FALSE(fileOffsetOf(*header, 0xFFFFFFFF, 4).has_value());
}

TEST_CASE("broken executables are rejected") {
    const auto exe = FakeExecutable::build({});
    CHECK_FALSE(readPsxExeHeader(std::span{exe}.first(0x7FF)).has_value());
    CHECK_FALSE(readPsxExeHeader(std::span{exe}.first(exe.size() - 1)).has_value());

    auto badMagic = exe;
    badMagic[3] = std::byte{'Y'};
    CHECK_FALSE(readPsxExeHeader(badMagic).has_value());
}
