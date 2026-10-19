#include <dwe/formats/disc/sector.hpp>

#include <doctest/doctest.h>

#include <vector>

#include "disc_fixtures.hpp"

using namespace dwe::formats::disc;

using Bytes = std::vector<std::byte>;

TEST_CASE("raw sectors give the user data of their mode and form") {
    const auto form1 = fixtures::makeRawSector(Bytes(Form1DataSize, std::byte{0x11}), 2, SectorForm::Form1);
    const auto decoded1 = decodeSector(form1, SectorLayout::Raw);
    REQUIRE(decoded1.has_value());
    CHECK(decoded1->form == SectorForm::Form1);
    CHECK(decoded1->userData.size() == Form1DataSize);
    CHECK(decoded1->userData.data() == form1.data() + 24);

    const auto form2 = fixtures::makeRawSector(Bytes(Form2DataSize, std::byte{0x22}), 2, SectorForm::Form2);
    const auto decoded2 = decodeSector(form2, SectorLayout::Raw);
    REQUIRE(decoded2.has_value());
    CHECK(decoded2->form == SectorForm::Form2);
    CHECK(decoded2->userData.size() == Form2DataSize);

    const auto mode1 = fixtures::makeRawSector(Bytes(Form1DataSize, std::byte{0x33}), 1, SectorForm::Form1);
    const auto decodedMode1 = decodeSector(mode1, SectorLayout::Raw);
    REQUIRE(decodedMode1.has_value());
    CHECK(decodedMode1->userData.data() == mode1.data() + 16);
}

TEST_CASE("malformed raw sectors are rejected") {
    auto sector = fixtures::makeRawSector({}, 2, SectorForm::Form1);
    CHECK_FALSE(decodeSector(std::span{sector}.first(RawSectorSize - 1), SectorLayout::Raw).has_value());

    auto badMode = sector;
    badMode[15] = std::byte{0};
    CHECK_FALSE(decodeSector(badMode, SectorLayout::Raw).has_value());

    auto badSync = sector;
    badSync[5] = std::byte{0};
    CHECK_FALSE(decodeSector(badSync, SectorLayout::Raw).has_value());

    CHECK_FALSE(decodeSector(sector, SectorLayout::Cooked).has_value());
}

TEST_CASE("a memory source reads whole sectors and stops at the end") {
    const auto cooked = Bytes((Form1DataSize * 2) + 100, std::byte{0x44});
    MemorySectorSource cookedSource{cooked, SectorLayout::Cooked};
    CHECK(cookedSource.sectorCount() == 2);
    CHECK(cookedSource.readSector(1).has_value());
    CHECK_FALSE(cookedSource.readSector(2).has_value());

    const auto raw = fixtures::toRawImage(cooked);
    MemorySectorSource rawSource{raw, SectorLayout::Raw};
    REQUIRE(rawSource.sectorCount() == 2);
    const auto sector = rawSource.readSector(1);
    REQUIRE(sector.has_value());
    CHECK(sector->userData[0] == std::byte{0x44});
}
