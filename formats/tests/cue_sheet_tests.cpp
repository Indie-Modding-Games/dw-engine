#include <dwe/formats/disc/cue_sheet.hpp>

#include <doctest/doctest.h>

using namespace dwe::formats::disc;

TEST_CASE("a single data track sheet is parsed") {
  const auto sheet =
      parseCueSheet("\xEF\xBB\xBFREM a comment\r\nFILE \"My Game (Region).bin\" BINARY\r\n"
                    "  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
  REQUIRE(sheet.has_value());
  CHECK(sheet->dataFile == "My Game (Region).bin");
  CHECK(sheet->layout == SectorLayout::Raw);

  const auto iso = parseCueSheet("FILE game.iso BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n");
  REQUIRE(iso.has_value());
  CHECK(iso->dataFile == "game.iso");
  CHECK(iso->layout == SectorLayout::Cooked);
}

TEST_CASE("unsupported sheets are rejected") {
  CHECK_FALSE(parseCueSheet("").has_value());
  CHECK_FALSE(
      parseCueSheet("FILE \"a.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n").has_value());
  CHECK_FALSE(
      parseCueSheet("FILE a.bin WAVE\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n").has_value());
  CHECK_FALSE(parseCueSheet("FILE a.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n").has_value());
  CHECK_FALSE(
      parseCueSheet("FILE a.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:02:00\n").has_value());
  CHECK_FALSE(parseCueSheet("FILE a.bin BINARY\nTRACK 01 MODE2/2352\n").has_value());
  CHECK_FALSE(parseCueSheet("TRACK 01 MODE2/2352\nINDEX 01 00:00:00\n").has_value());
  CHECK_FALSE(parseCueSheet("FILE a.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n"
                            "TRACK 02 AUDIO\nINDEX 01 10:00:00\n")
                  .has_value());
  CHECK_FALSE(parseCueSheet(
                  "FILE a.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nFILE b.bin BINARY\n")
                  .has_value());
  CHECK_FALSE(
      parseCueSheet("FILE a.bin BINARY\nTRACK 01 MODE2/2352\nPREGAP 00:02:00\nINDEX 01 00:00:00\n")
          .has_value());

  const auto error = parseCueSheet("FILE a.bin BINARY\nTRACK 01 AUDIO\n");
  REQUIRE_FALSE(error.has_value());
  CHECK(error.error().message.find("line 2") != std::string::npos);
}
