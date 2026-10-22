#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>
#include <dwe/formats/exe/overlay.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base_pack.hpp"
#include "disc_fixtures.hpp"
#include "exe_fixture.hpp"
#include "overlay_extractor.hpp"
#include "overlay_fixture.hpp"

using namespace dwe::dwx;
using dwe::formats::exe::OverlayDataKind;
using dwe::formats::test::ImageBuilder;
using dwe::formats::test::makeOverlayFile;
using dwe::formats::test::OverlayFileSpec;
using dwe::formats::test::putU32;

namespace {

// A disc of synthetic overlays (and the loose files a battle overlay carries), read as a given
// version.
struct OverlayDisc {
  OverlayDisc(const std::map<std::string, std::vector<std::byte>>& files, std::string version) {
    fixtures::IsoBuilder builder;
    for (const auto& [path, contents] : files) {
      builder.addFile(path, contents);
    }
    image = builder.build();
    source = std::make_unique<dwe::formats::disc::MemorySectorSource>(
        image, dwe::formats::disc::SectorLayout::Cooked);
    volume = *dwe::formats::disc::readIsoVolume(*source);
    info.version = std::move(version);
  }

  std::vector<std::byte> image;
  std::unique_ptr<dwe::formats::disc::MemorySectorSource> source;
  dwe::formats::disc::IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
  DiscInfo info{.volumeId = "FIXTURE", .executablePath = "", .executableSha1 = "", .version = ""};
};

ExtractionRun runOn(const OverlayDisc& fixture) {
  const Disc disc{*fixture.source, fixture.volume, fixture.info};
  auto run = runExtractors(disc, extractors());
  REQUIRE(run.has_value());
  return std::move(*run);
}

const FileReport& reportOf(const ExtractionRun& run, std::string_view path) {
  const auto found =
      std::ranges::find(run.files, path, [](const FileReport& report) { return report.file.path; });
  REQUIRE(found != run.files.end());
  return *found;
}

bool mentions(const std::vector<std::string>& lines, std::string_view text) {
  return std::ranges::any_of(lines,
                             [text](const std::string& line) { return line.contains(text); });
}

// A plain overlay of the US layout: header at the start of the file, at its load address.
std::vector<std::byte> plainOverlay(std::uint32_t loadAddress, std::size_t codeSize,
                                    std::size_t dataSize) {
  return makeOverlayFile({.leading = {},
                          .id = 1,
                          .address = loadAddress,
                          .name = "TEST_REL.BIN",
                          .codeSize = codeSize,
                          .data = fixtures::patternBytes(dataSize, 3)});
}

constexpr std::uint32_t SharedLoad = 0x80080800; // DGET and SHOP
constexpr std::string_view ExecutablePath = "SLUS_000.01";
// A made-up executable large enough to hold the copies of SHOP_REL.BIN's tables.
constexpr std::uint32_t ExecutableBase = 0x80130000;
constexpr std::size_t ExecutableSize = 0x800;
constexpr std::uint32_t MovieLoad = 0x80010000;
constexpr std::uint32_t BattleLoad = 0x80052AE0;
constexpr std::size_t BattleHeader = 0x4188;
constexpr std::size_t BattleStoredMemory = 0x21374; // BTL

dwe::formats::exe::OverlayLayout battleLayout() {
  const auto layout = dwe::formats::exe::findOverlayLayout("us", "BTL_REL.BIN");
  REQUIRE(layout.has_value());
  return layout.value_or(dwe::formats::exe::OverlayLayout{});
}

// The five loose files a battle overlay carries, with made-up contents of the sizes the US layout
// gives.
std::map<std::string, std::vector<std::byte>> battleLooseFiles() {
  std::map<std::string, std::vector<std::byte>> files;
  const auto layout = battleLayout();
  std::uint8_t seed = 10;
  for (const auto& embedded : layout.embeddedFiles) {
    files[std::string{embedded.discPath}] = fixtures::patternBytes(embedded.size, seed++);
  }
  return files;
}

std::vector<std::byte> battleOverlay(const std::map<std::string, std::vector<std::byte>>& loose) {
  const auto layout = battleLayout();
  std::vector<std::byte> leading;
  for (const auto& embedded : layout.embeddedFiles) {
    const auto& bytes = loose.at(std::string{embedded.discPath});
    leading.insert(leading.end(), bytes.begin(), bytes.end());
  }
  constexpr std::size_t CodeBegin = BattleHeader + dwe::formats::exe::OverlayHeaderSize;
  constexpr std::size_t DataSize = 0x10;
  return makeOverlayFile({.leading = std::move(leading),
                          .id = 1,
                          .address = BattleLoad + BattleHeader,
                          .name = "BTL_REL.BIN",
                          .codeSize = BattleStoredMemory - DataSize - CodeBegin,
                          .data = fixtures::patternBytes(DataSize + 0x20, 1)});
}

} // namespace

TEST_CASE("the overlay extractor takes the *_REL.BIN files at the root") {
  CHECK(handlesOverlay("DGET_REL.BIN"));
  CHECK(handlesOverlay("ANY_REL.BIN"));
  CHECK_FALSE(handlesOverlay("_REL.BIN"));
  CHECK_FALSE(handlesOverlay("DIR/DGET_REL.BIN"));
  CHECK_FALSE(handlesOverlay("MMM.DAT"));
}

TEST_CASE("an overlay whose every part is understood is extracted, with each part noted") {
  const OverlayDisc fixture{{{"DGET_REL.BIN", plainOverlay(SharedLoad, 0x20, 0x10)}}, "us"};
  const auto run = runOn(fixture);
  const FileReport& report = reportOf(run, "DGET_REL.BIN");
  CHECK(report.extractor == "overlay");
  CHECK(report.status == FileStatus::Extracted);
  CHECK(report.outputs.empty());
  CHECK(report.unknowns.empty());
  CHECK(mentions(report.notes, "[0x0, 0x40): header, id 1"));
  CHECK(mentions(report.notes, "[0x40, 0x60): code; it is not an asset"));
  CHECK(mentions(report.notes, "[0x60, 0x70): data of the code"));
  CHECK(mentions(report.notes, "none of it is an asset"));
  CHECK(mentions(report.notes, "[0x70, 0x74): the zero word"));
}

TEST_CASE("an overlay that does not match its layout, or has none, is partial") {
  auto ended = plainOverlay(SharedLoad, 0x20, 0x10);
  ended.back() = std::byte{1};
  auto moved = plainOverlay(SharedLoad + 4, 0x20, 0x10);
  const OverlayDisc fixture{{{"DGET_REL.BIN", ended}, {"MURD_REL.BIN", moved}}, "us"};
  const auto run = runOn(fixture);
  CHECK(reportOf(run, "DGET_REL.BIN").status == FileStatus::Partial);
  CHECK(mentions(reportOf(run, "DGET_REL.BIN").unknowns, "last word"));
  CHECK(reportOf(run, "MURD_REL.BIN").status == FileStatus::Partial);

  const OverlayDisc unknownVersion{{{"DGET_REL.BIN", plainOverlay(SharedLoad, 0x20, 0x10)}},
                                   "other"};
  const auto other = runOn(unknownVersion);
  CHECK(reportOf(other, "DGET_REL.BIN").status == FileStatus::Partial);
  CHECK(mentions(reportOf(other, "DGET_REL.BIN").unknowns, "[0x0, 0x74): no layout"));

  const OverlayDisc noHeader{{{"DGET_REL.BIN", fixtures::patternBytes(0x80, 0)}}, "us"};
  CHECK(reportOf(runOn(noHeader), "DGET_REL.BIN").status == FileStatus::Partial);
}

TEST_CASE("an overlay with a data map has each range noted, and the executable's copies compared") {
  // A made-up SHOP_REL.BIN that satisfies the US data map: cases at the start of the code, zero
  // pointers, texts ending with a zero, numbers from a pattern.
  const auto found = dwe::formats::exe::findOverlayLayout("us", "SHOP_REL.BIN");
  REQUIRE(found.has_value());
  const auto layout = found.value_or(dwe::formats::exe::OverlayLayout{});
  REQUIRE_FALSE(layout.dataRanges.empty());
  const std::size_t dataBegin = layout.dataRanges.front().begin;
  const std::size_t dataEnd = layout.dataRanges.back().end;
  auto file = makeOverlayFile({.leading = {},
                               .id = 1,
                               .address = SharedLoad,
                               .name = "SHOP_REL.BIN",
                               .codeSize = dataBegin - dwe::formats::exe::OverlayHeaderSize,
                               .data = fixtures::patternBytes(dataEnd - dataBegin, 5)});
  for (const auto& data : layout.dataRanges) {
    for (std::size_t at = data.begin; at < data.end; at += 4) {
      if (data.kind == OverlayDataKind::JumpTable) {
        putU32(file, at, SharedLoad + dwe::formats::exe::OverlayHeaderSize);
      } else if (data.kind == OverlayDataKind::Pointers) {
        putU32(file, at, 0);
      }
    }
    if (data.kind == OverlayDataKind::Text) {
      file[data.end - 1] = std::byte{0};
    }
  }

  // An executable with a copy of every range that has one, all but one byte of the first.
  ImageBuilder image{ExecutableBase};
  image.place(ExecutableSize);
  std::uint32_t first = 0;
  for (const auto& data : layout.dataRanges) {
    if (data.executableAddress == 0) {
      continue;
    }
    first = first == 0 ? data.executableAddress : first;
    for (std::size_t at = data.begin; at < data.end; ++at) {
      image.u8(static_cast<std::uint32_t>(data.executableAddress + (at - data.begin)),
               std::to_integer<std::uint8_t>(file[at]));
    }
  }
  REQUIRE(first != 0);
  image.u8(first, 0xEE);

  OverlayDisc fixture{{{"SHOP_REL.BIN", file}, {std::string{ExecutablePath}, image.executable()}},
                      "us"};
  fixture.info.executablePath = ExecutablePath;
  const auto run = runOn(fixture);
  const FileReport& report = reportOf(run, "SHOP_REL.BIN");
  CHECK(report.status == FileStatus::Extracted);
  CHECK(report.unknowns.empty());
  CHECK(mentions(report.notes, "mapped from the code of the jp_rev1 build"));
  CHECK(mentions(report.notes, "never jumps here"));
  CHECK(
      mentions(report.notes, "[0x7860, 0x7888): jump table of openRecycleShop's switch, 10 cases"));
  CHECK(
      mentions(report.notes,
               std::format("1 of its bytes differ from the executable's copy at 0x{:08X}", first)));
  CHECK(mentions(report.notes, "; same bytes as the executable's copy at"));
  CHECK(std::ranges::count_if(report.notes, [](const std::string& note) {
          return note.contains("executable's copy");
        }) == std::ranges::count_if(layout.dataRanges, [](const auto& data) {
          return data.executableAddress != 0;
        }));

  // Without the executable, the ranges it has copies of cannot be compared.
  const OverlayDisc alone{{{"SHOP_REL.BIN", file}}, "us"};
  const auto unchecked = reportOf(runOn(alone), "SHOP_REL.BIN");
  CHECK(unchecked.status == FileStatus::Partial);
  CHECK(mentions(unchecked.unknowns, "could not be compared with the executable's copy"));
}

TEST_CASE(
    "an overlay whose data does not match its data map is partial, with the ranges that do not") {
  const OverlayDisc fixture{{{"SHOP_REL.BIN", plainOverlay(SharedLoad, 0x20, 0x10)}}, "us"};
  const auto run = runOn(fixture);
  const FileReport& report = reportOf(run, "SHOP_REL.BIN");
  CHECK(report.status == FileStatus::Partial);
  REQUIRE(report.unknowns.size() == 1);
  CHECK(report.unknowns[0].starts_with(
      "[0x60, 0x70): the data map does not cover the data end to end"));
  CHECK(mentions(report.notes, "code; it is not an asset"));
}

TEST_CASE(
    "the memory an overlay stores past its data is noted, as the decoder's buffers for the movie player") {
  // MOV_REL.BIN: its stored memory starts at 0x9EC.
  constexpr std::size_t CodeSize = 0x900;
  constexpr std::size_t DataSize = 0x9EC - 0x40 - CodeSize;
  const OverlayDisc fixture{{{"MOV_REL.BIN", plainOverlay(MovieLoad, CodeSize, DataSize + 0x30)}},
                            "us"};
  const auto run = runOn(fixture);
  const FileReport& report = reportOf(run, "MOV_REL.BIN");
  CHECK(report.status == FileStatus::Extracted);
  CHECK(mentions(report.notes, "[0x9EC, 0xA1C): the movie decoder's buffers"));
  CHECK(mentions(report.notes, "leftover bytes of the build"));
}

TEST_CASE("the files a battle overlay carries are checked against the loose files") {
  auto files = battleLooseFiles();
  files["BTL_REL.BIN"] = battleOverlay(files);
  {
    const OverlayDisc fixture{files, "us"};
    const auto run = runOn(fixture);
    const FileReport& report = reportOf(run, "BTL_REL.BIN");
    CHECK(report.status == FileStatus::Extracted);
    CHECK(mentions(report.notes, "[0x0, 0xD20): same bytes as ETCDAT/EX.TIM"));
    CHECK(mentions(report.notes, "same bytes as ETCDAT/DEFEFF.TMD"));
    CHECK(mentions(report.notes, "[0x4188, 0x41C8): header"));
    CHECK(mentions(report.notes, "[0x21374, 0x21394): memory the overlay only uses at run time"));
  }
  files["ETCHI/MAHI.TMD"][3] ^= std::byte{1};
  files.erase("ETCDAT/EX.TMD");
  const OverlayDisc fixture{files, "us"};
  const auto run = runOn(fixture);
  const FileReport& report = reportOf(run, "BTL_REL.BIN");
  CHECK(report.status == FileStatus::Partial);
  CHECK(mentions(report.unknowns, "not those of ETCHI/MAHI.TMD"));
  CHECK(mentions(report.unknowns, "should be ETCDAT/EX.TMD"));
}

TEST_CASE("an overlay with gameplay tables is partial when the executable's tables are not known") {
  // FISH_REL.BIN: its stored memory starts at 0xAB40.
  constexpr std::size_t CodeSize = 0xA120;
  const OverlayDisc fixture{
      {{"FISH_REL.BIN", plainOverlay(0x80070000, CodeSize, 0xAB40 - 0x40 - CodeSize + 8)}}, "us"};
  const auto run = runOn(fixture);
  const FileReport& report = reportOf(run, "FISH_REL.BIN");
  CHECK(report.status == FileStatus::Partial);
  CHECK(mentions(report.unknowns, "the gameplay tables in the data were not converted"));
  CHECK(mentions(report.notes, "only its gameplay tables are assets"));
}
