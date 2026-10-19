#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/exe/tables.hpp>

#include <doctest/doctest.h>

#include <string>

#include "exe_fixture.hpp"

using dwe::formats::exe::ExecutableLayout;
using dwe::formats::exe::findExecutableLayout;
using dwe::formats::exe::JukeboxTrack;
using dwe::formats::exe::MapSoundPara;
using dwe::formats::exe::MemoryImage;
using dwe::formats::exe::readExecutableTables;
using dwe::formats::exe::TableLocation;
using dwe::formats::test::ImageBuilder;
using dwe::formats::test::makeExecutableFixture;

TEST_CASE("an executable maps its text to the address of its header") {
    ImageBuilder builder{0x80010000};
    const std::uint32_t address = builder.string("hello");
    const auto file = builder.executable();

    const auto image = MemoryImage::fromExecutable(file);
    REQUIRE(image.has_value());
    CHECK(image->baseAddress() == 0x80010000);
    CHECK(image->stringAt(address).value_or("") == "hello");
    CHECK(image->bytesAt(address, 5).has_value());
    CHECK_FALSE(image->bytesAt(0x8000FFFF, 1).has_value());
    CHECK(image->bytesAt(address, builder.text().size()).has_value());
    CHECK_FALSE(image->bytesAt(address, builder.text().size() + 1).has_value());
    CHECK_FALSE(image->stringAt(0x90000000).has_value());
}

TEST_CASE("an overlay maps its bytes to its load address") {
    const std::vector<std::byte> file = {std::byte{'a'}, std::byte{'b'}, std::byte{0}};
    const MemoryImage image = MemoryImage::fromOverlay(file, 0x80052AE0);
    CHECK(image.stringAt(0x80052AE0).value_or("") == "ab");
    CHECK(image.stringAt(0x80052AE1).value_or("") == "b");
    CHECK_FALSE(image.stringAt(0x80052AE3).has_value());
}

TEST_CASE("a file without the executable header or with a text larger than the file is refused") {
    CHECK_FALSE(MemoryImage::fromExecutable(std::vector<std::byte>(0x800)).has_value());
    CHECK_FALSE(MemoryImage::fromExecutable(std::vector<std::byte>(10)).has_value());

    ImageBuilder builder{0x80010000};
    builder.place(16);
    auto file = builder.executable();
    file.resize(file.size() - 1);
    CHECK_FALSE(MemoryImage::fromExecutable(file).has_value());
}

TEST_CASE("a string without its terminator inside the image is refused") {
    const std::vector<std::byte> file = {std::byte{'a'}, std::byte{'b'}};
    const MemoryImage image = MemoryImage::fromOverlay(file, 0x80000000);
    CHECK_FALSE(image.stringAt(0x80000000).has_value());

    const std::vector<std::byte> longText(64, std::byte{'x'});
    CHECK_FALSE(MemoryImage::fromOverlay(longText, 0x80000000).stringAt(0x80000000, 8).has_value());
}

TEST_CASE("every table of a synthetic executable is read field by field") {
    const auto fixture = makeExecutableFixture();
    const auto image = MemoryImage::fromExecutable(fixture.file);
    REQUIRE(image.has_value());
    const auto tables = readExecutableTables(*image, fixture.layout);
    REQUIRE_MESSAGE(tables.has_value(), tables.error().message);

    REQUIRE(tables->digimonData.size() == 3);
    const auto& digimon = tables->digimonData[0];
    CHECK(digimon.name == "Testmon");
    CHECK(digimon.boneCount == 1);
    CHECK(digimon.radius == -5);
    CHECK(digimon.height == 300);
    CHECK(digimon.special == std::array<std::uint8_t, 3>{1, 0xFF, 0xFF});
    CHECK(digimon.dropItem == 1);
    CHECK(digimon.moves[0] == 1);
    CHECK(digimon.moves[15] == 0xFF);
    CHECK(tables->digimonData[2].name == "Other mon");

    CHECK(tables->digimonFileNames == std::vector<std::string>{"TSTA", "TSTB", "OTHR"});
    REQUIRE(tables->digimonSkeletons.size() == 3);
    CHECK(tables->digimonSkeletons[0].size() == 1);
    REQUIRE(tables->digimonSkeletons[2].size() == 3);
    CHECK(tables->digimonSkeletons[2][2].objIndex == 2);
    CHECK(tables->digimonSkeletons[2][2].parentIndex == 1);
    CHECK(tables->digimonSkeletons[2][0].parentIndex == -1);

    REQUIRE(tables->moveData.size() == 2);
    CHECK(tables->moveData[0].distance == 1000);
    CHECK(tables->moveData[0].power == 120);
    CHECK(tables->moveData[0].mpCost == 30);
    CHECK(tables->moveData[0].unknown0F == 7);

    REQUIRE(tables->itemPara.size() == 2);
    CHECK(tables->itemPara[0].name == "Meat");
    CHECK(tables->itemPara[0].value == 50);
    CHECK(tables->itemPara[0].meritValue == 2);
    CHECK(tables->itemPara[0].sortingValue == -1);
    CHECK(tables->itemPara[0].unknown1E == 0xBEEF);
    CHECK(tables->itemPara[1].name == "Odd\x82\xA0\\");
    CHECK(tables->itemDescriptions[1] == "Strange.");

    CHECK(tables->evolutionRequirements[0].digimon == -1);
    CHECK(tables->evolutionRequirements[1].hp == 1000);
    CHECK(tables->evolutionRequirements[1].flags == 0x11);
    CHECK(tables->evolutionStatsGains[1].targetDigimon == 77);
    CHECK(tables->evolutionPaths[0].to[0] == 2);
    CHECK(tables->evolutionPaths[0].from[0] == -1);

    REQUIRE(tables->raiseData.size() == 1);
    CHECK(tables->raiseData[0].hungerTimes[0] == 6);
    CHECK(tables->raiseData[0].energyUsage == 9);
    CHECK(tables->raiseData[0].poopTimer == 400);
    CHECK(tables->raiseData[0].viewZ == -20);

    CHECK(tables->cardData[0].digimonId == 2);
    CHECK(tables->cardData[0].meritValue == 300);
    CHECK(tables->cardPrices[0] == 100);
    CHECK(tables->tournamentSchedule[0][0] == 1);
    CHECK(tables->tournamentSchedule[0][2] == 0xFF);
    CHECK(tables->shopMaps == std::vector<std::int16_t>{1, -1});
    CHECK(tables->medalDescriptions[1].empty());

    REQUIRE(tables->mapEntries.size() == 2);
    CHECK(tables->mapEntries[0].filename == "START");
    CHECK(tables->mapEntries[0].num8bppImages == 1);
    CHECK(tables->mapEntries[0].flags == 0xC3);
    CHECK(tables->mapEntries[0].doorsId == 1);
    CHECK(tables->mapEntries[1].filename.empty());
    CHECK(tables->doorMapData[0].modelId[0] == 3);
    CHECK(tables->doorMapData[0].modelId[1] == -1);
    CHECK(tables->doorMapData[0].posX[0] == -100);
    CHECK(tables->toiletData[0].posX1 == 10);
    CHECK(tables->mapLightUpdateData[0].trigger == 0xFFFF);
    CHECK(tables->digimonVlallSoundIds == std::vector<std::uint8_t>{0, 4, 0});
    CHECK(tables->digimonVballSoundIds == std::vector<std::uint8_t>{0, 0, 9});
    CHECK(tables->mapSoundPara ==
          std::vector<MapSoundPara>{{.sectorId = 0, .sectorCount = 0}, {.sectorId = 6, .sectorCount = 10}});
    CHECK(tables->jukeboxTracks == std::vector<JukeboxTrack>{{.font = 0x1D, .track = 0}, {.font = 0, .track = 2}});
}

TEST_CASE("tables outside the image, unlocated tables and bad pointers are errors") {
    auto fixture = makeExecutableFixture();
    const auto image = MemoryImage::fromExecutable(fixture.file);
    REQUIRE(image.has_value());

    SUBCASE("a count past the end of the image") {
        fixture.layout.moveData.count = 100000;
        CHECK_FALSE(readExecutableTables(*image, fixture.layout).has_value());
    }
    SUBCASE("a table that is not located") {
        fixture.layout.raiseData = TableLocation{};
        const auto tables = readExecutableTables(*image, fixture.layout);
        REQUIRE_FALSE(tables.has_value());
        CHECK(tables.error().message.find("RAISE_DATA") != std::string::npos);
    }
    SUBCASE("a string pointer outside the image") {
        fixture.layout.moveNames.address = fixture.layout.cardPrices.address; // 100 is not an address
        CHECK_FALSE(readExecutableTables(*image, fixture.layout).has_value());
    }
    SUBCASE("a version whose tables are not located yet") {
        const ExecutableLayout layout = findExecutableLayout("eu").value_or(ExecutableLayout{});
        CHECK(layout.version == "eu");
        CHECK_FALSE(layout.inlineNames);
        CHECK_FALSE(readExecutableTables(*image, layout).has_value());
    }
}

TEST_CASE("the US layout has the decomp's addresses and counts") {
    const ExecutableLayout layout = findExecutableLayout("us").value_or(ExecutableLayout{});
    CHECK(layout.version == "us");
    CHECK(layout.digimonData.address == 0x8012CEB4);
    CHECK(layout.digimonData.count == 180);
    CHECK(layout.mapEntries.count == 255);
    CHECK(layout.mapLightUpdateData.located());
    CHECK(layout.mapSoundPara.address == 0x80127FF4);
    CHECK(layout.jukeboxTracks.count == 64);
    CHECK_FALSE(findExecutableLayout("demo").has_value());
}

TEST_CASE("a Digimon without bones may have a null skeleton pointer, one with bones may not") {
    auto fixture = makeExecutableFixture();
    const auto fileOffset = [&](std::uint32_t address) { return address - 0x80010000U + 0x800U; };
    const std::uint32_t pointer = fileOffset(fixture.layout.digimonSkeletons.address);
    for (std::uint32_t i = 0; i < 4; ++i) {
        fixture.file[pointer + i] = std::byte{0};
    }
    const auto withBones = MemoryImage::fromExecutable(fixture.file);
    REQUIRE(withBones.has_value());
    CHECK_FALSE(readExecutableTables(*withBones, fixture.layout).has_value());

    const std::uint32_t boneCount = fileOffset(fixture.layout.digimonData.address + 0x14);
    fixture.file[boneCount] = std::byte{0};
    const auto withoutBones = MemoryImage::fromExecutable(fixture.file);
    REQUIRE(withoutBones.has_value());
    const auto tables = readExecutableTables(*withoutBones, fixture.layout);
    REQUIRE(tables.has_value());
    CHECK(tables->digimonSkeletons[0].empty());
}
