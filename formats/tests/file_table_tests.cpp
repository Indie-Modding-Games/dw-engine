#include <dwe/formats/disc/boot.hpp>
#include <dwe/formats/disc/file_table.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "disc_fixtures.hpp"

using namespace dwe::formats::disc;
using fixtures::FakeExecutable;
using fixtures::TableEntry;

namespace {

constexpr FileTableLocation TestLocation{.version = "test", .address = FakeExecutable::TableAddress, .capacity = 8};

} // namespace

TEST_CASE("the table is read up to its terminating entry") {
    const std::vector<TableEntry> entries{{.sector = 23, .size = 68, .name = "SYSTEM.CNF"},
                                          {.sector = 30, .size = 4096, .name = "A.BIN"}};
    const auto table = readFileTable(FakeExecutable::build(entries), TestLocation);
    REQUIRE(table.has_value());
    REQUIRE(table->size() == 2);
    CHECK((*table)[0].sector == 23);
    CHECK((*table)[0].size == 68);
    CHECK((*table)[0].filename == "SYSTEM.CNF");
    CHECK((*table)[1].filename == "A.BIN");
}

TEST_CASE("broken tables are rejected") {
    const std::vector<TableEntry> entries{{.sector = 23, .size = 68, .name = "SYSTEM.CNF"},
                                          {.sector = 30, .size = 4096, .name = "A.BIN"}};
    const auto exe = FakeExecutable::build(entries);

    CHECK_FALSE(
        readFileTable(exe, {.version = "test", .address = FakeExecutable::TableAddress, .capacity = 2}).has_value());
    CHECK_FALSE(readFileTable(exe, {.version = "test", .address = 0x80000000, .capacity = 8}).has_value());
    CHECK_FALSE(
        readFileTable(exe, {.version = "test", .address = FakeExecutable::TableAddress, .capacity = 400}).has_value());

    const std::size_t firstEntry =
        FakeExecutable::HeaderSize + (FakeExecutable::TableAddress - FakeExecutable::LoadAddress);
    auto badPointer = exe;
    fixtures::putU32(badPointer, firstEntry + 8, 0x80FF0000);
    CHECK_FALSE(readFileTable(badPointer, TestLocation).has_value());

    auto noNul = exe;
    fixtures::putU32(noNul, firstEntry + 8, FakeExecutable::LoadAddress + FakeExecutable::TextSize - 4);
    fixtures::putText(noNul, noNul.size() - 4, "ABCD");
    CHECK_FALSE(readFileTable(noNul, TestLocation).has_value());

    auto negative = exe;
    fixtures::putU32(negative, firstEntry, 0xFFFFFFFF);
    CHECK_FALSE(readFileTable(negative, TestLocation).has_value());

    CHECK_FALSE(readFileTable(std::span{exe}.first(0x100), TestLocation).has_value());
}

TEST_CASE("table locations are known per version") {
    const auto us = findFileTableLocation("us");
    if (!us) {
        FAIL("the US location is missing");
        return;
    }
    CHECK(us->address == 0x8011FE1C);
    CHECK(us->capacity == 819);
    CHECK_FALSE(findFileTableLocation("demo").has_value());
}

TEST_CASE("the check matches table entries by name, sector and size") {
    const std::vector<IsoFile> files{
        {.path = "A.BIN", .lba = 30, .size = 4096},       {.path = "DATA/A.BIN", .lba = 40, .size = 10},
        {.path = "DATA/B.BIN", .lba = 50, .size = 10},    {.path = "MOVIE/C.STR", .lba = 60, .size = 4096},
        {.path = "MOVIE/D.STR", .lba = 70, .size = 4096}, {.path = "BOOT.EXE", .lba = 90, .size = 2048}};
    const std::vector<FileEntry> table{
        {.sector = 40, .size = 10, .filename = "A.BIN"},       {.sector = 51, .size = 10, .filename = "B.BIN"},
        {.sector = 60, .size = 2 * 2336, .filename = "C.STR"}, {.sector = 70, .size = 3 * 2336, .filename = "D.STR"},
        {.sector = 80, .size = 10, .filename = "HIDDEN.BIN"},  {.sector = 90, .size = 2048, .filename = "OTHER.EXE"}};
    const auto check = checkFileTable(table, files);
    CHECK(check.inTable == std::vector<bool>{false, true, false, true, false, true});
    CHECK(check.aliases == std::vector<std::string>{"OTHER.EXE is BOOT.EXE"});
    REQUIRE(check.mismatches.size() == 2);
    CHECK(check.mismatches[0].starts_with("B.BIN"));
    CHECK(check.mismatches[1].starts_with("D.STR"));
    REQUIRE(check.tableOnly.size() == 1);
    CHECK(check.tableOnly[0].filename == "HIDDEN.BIN");
}

TEST_CASE("the boot executable is found through SYSTEM.CNF") {
    CHECK(parseBootPath("BOOT = cdrom:\\GAME_010.01;1\r\nTCB = 4\r\n").value_or("") == "GAME_010.01");
    CHECK(parseBootPath("TCB=4\nboot=cdrom:\\\\DIR\\GAME.EXE;1\n").value_or("") == "DIR/GAME.EXE");
    CHECK_FALSE(parseBootPath("TCB = 4\n").has_value());
    CHECK_FALSE(parseBootPath("BOOT = cdrom:\\;1\n").has_value());

    const std::string cnf = "BOOT = cdrom:\\GAME_010.01;1\r\n";
    fixtures::IsoBuilder builder;
    builder.addFile("SYSTEM.CNF", std::vector<std::byte>(std::from_range, std::as_bytes(std::span{cnf})));
    builder.addFile("GAME_010.01", FakeExecutable::build({}));
    const auto image = builder.build();
    MemorySectorSource source{image, SectorLayout::Cooked};
    const auto volume = readIsoVolume(source);
    REQUIRE(volume.has_value());
    const auto executable = findBootExecutable(source, *volume);
    REQUIRE(executable.has_value());
    CHECK(executable->path == "GAME_010.01");

    fixtures::IsoBuilder noExe;
    noExe.addFile("SYSTEM.CNF", std::vector<std::byte>(std::from_range, std::as_bytes(std::span{cnf})));
    const auto noExeImage = noExe.build();
    MemorySectorSource noExeSource{noExeImage, SectorLayout::Cooked};
    const auto noExeVolume = readIsoVolume(noExeSource);
    REQUIRE(noExeVolume.has_value());
    CHECK_FALSE(findBootExecutable(noExeSource, *noExeVolume).has_value());
}

TEST_CASE("a synthetic disc's table agrees with its volume") {
    const std::string cnf = "BOOT = cdrom:\\GAME.EXE;1\n";
    const auto buildDisc = [&](std::span<const TableEntry> entries, fixtures::IsoBuilder& builder) {
        builder.addFile("SYSTEM.CNF", std::vector<std::byte>(std::from_range, std::as_bytes(std::span{cnf})));
        builder.addFile("GAME.EXE", FakeExecutable::build(entries));
        builder.addFile("DATA/A.BIN", fixtures::patternBytes(3000, 1));
        builder.addFile("MOVIE/C.STR", fixtures::patternBytes(100, 2));
        return builder.build();
    };
    // The executable's size does not depend on the table, so a first build gives the sectors of the second.
    fixtures::IsoBuilder layout;
    buildDisc({}, layout);
    const std::vector<TableEntry> entries{
        {.sector = static_cast<std::int32_t>(layout.lbaOf("SYSTEM.CNF")),
         .size = static_cast<std::int32_t>(cnf.size()),
         .name = "SYSTEM.CNF"},
        {.sector = static_cast<std::int32_t>(layout.lbaOf("DATA/A.BIN")), .size = 3000, .name = "A.BIN"}};
    fixtures::IsoBuilder builder;
    const auto image = fixtures::toRawImage(buildDisc(entries, builder));

    MemorySectorSource source{image, SectorLayout::Raw};
    const auto volume = readIsoVolume(source);
    REQUIRE(volume.has_value());
    const auto executable = findBootExecutable(source, *volume);
    REQUIRE(executable.has_value());
    const auto bytes = readIsoFile(source, *executable);
    REQUIRE(bytes.has_value());
    const auto table = readFileTable(*bytes, TestLocation);
    REQUIRE(table.has_value());
    const auto check = checkFileTable(*table, volume->files);
    CHECK(check.mismatches.empty());
    CHECK(check.tableOnly.empty());
    CHECK(std::ranges::count(check.inTable, true) == 2);
}
