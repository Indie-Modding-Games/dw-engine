#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>
#include <dwe/formats/script/scn_file.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base_pack.hpp"
#include "disc_fixtures.hpp"
#include "written_pack.hpp"

using namespace dwe::dwx;

namespace {

constexpr std::uint32_t SlotSize = 0x800;

std::vector<std::byte> bytesOf(std::initializer_list<unsigned> values) {
  std::vector<std::byte> bytes;
  for (const unsigned value : values) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

// A script with one section (0xFE at 8) and the given code.
std::vector<std::byte> script(std::initializer_list<unsigned> code) {
  auto bytes = bytesOf({0x08, 0x00, 0xFE, 0x00, 0x08, 0x00, 0xFF, 0xFF});
  const auto rest = bytesOf(code);
  bytes.insert(bytes.end(), rest.begin(), rest.end());
  return bytes;
}

// A disc with MAPHEAD.SCN and a DG.SCN whose script 0 is a copy of it unless asked otherwise,
// script 1 shows a text and, when asked, script 2 has an opcode the interpreter does not have. Each
// script has a slot of its own, padded with junk.
struct ScriptDisc {
  explicit ScriptDisc(bool withUnknownOpcode, bool scriptZeroCopiesMapHead = true) {
    const auto mapHead = script({0xFE, 0x00, 0xFF, 0x00});
    std::vector<std::vector<std::byte>> scripts{
        scriptZeroCopiesMapHead ? mapHead : script({0xFE, 0x00, 0xFE, 0x00, 0xFF, 0x00}),
        script({0x1A, 0x00, 0x82, 0x60, 0x0D, 0x00, 0x00, 0x00, 0xFF, 0x00})};
    if (withUnknownOpcode) {
      scripts.push_back(script({0x40, 0x00, 0xFF, 0x00}));
    }
    std::vector<std::byte> pack(dwe::formats::script::ScriptTableSize);
    for (std::size_t id = 0; id <= scripts.size(); ++id) {
      const auto offset = static_cast<std::uint32_t>(pack.size() + (id * SlotSize));
      for (std::size_t i = 0; i < 4; ++i) {
        pack[(id * 4) + i] = static_cast<std::byte>(offset >> (8 * i) & 0xFFU);
      }
    }
    pack[0x1000] = std::byte{0x5A}; // a leftover in the table
    for (const auto& bytes : scripts) {
      auto slot = bytes;
      slot.resize(SlotSize, std::byte{0xAB});
      pack.insert(pack.end(), slot.begin(), slot.end());
    }
    fixtures::IsoBuilder builder;
    builder.addFile("SCN/MAPHEAD.SCN", mapHead);
    builder.addFile("SCN/DG.SCN", pack);
    image = builder.build();
    source = std::make_unique<dwe::formats::disc::MemorySectorSource>(
        image, dwe::formats::disc::SectorLayout::Cooked);
    volume = *dwe::formats::disc::readIsoVolume(*source);
  }

  std::vector<std::byte> image;
  std::unique_ptr<dwe::formats::disc::MemorySectorSource> source;
  dwe::formats::disc::IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
  DiscInfo info{.volumeId = "FIXTURE", .executablePath = "", .executableSha1 = "", .version = ""};
};

const FileReport& reportOf(const ExtractionRun& run, std::string_view path) {
  const auto found =
      std::ranges::find(run.files, path, [](const FileReport& report) { return report.file.path; });
  REQUIRE(found != run.files.end());
  return *found;
}

bool hasNote(const FileReport& report, std::string_view text) {
  return std::ranges::any_of(report.notes,
                             [text](const std::string& note) { return note.contains(text); });
}

} // namespace

TEST_CASE("both script files become one listing per script") {
  const ScriptDisc fixture{false};
  const Disc disc{*fixture.source, fixture.volume, fixture.info};
  const auto run = runExtractors(disc, extractors());
  REQUIRE(run.has_value());
  // The engine loads the pack written from the run and dwx validate finds nothing wrong with it.
  CHECK(dwe::dwx::testing::problemsOfWrittenPack(*run, fixture.info) == "");

  const auto& mapHead = reportOf(*run, "SCN/MAPHEAD.SCN");
  CHECK(mapHead.extractor == "event_script");
  CHECK(mapHead.status == FileStatus::Extracted);
  CHECK(mapHead.outputs ==
        std::vector<std::string>{"script/maphead.yaml", "story/000.lua", "story/000_lines.yaml"});

  const auto& pack = reportOf(*run, "SCN/DG.SCN");
  CHECK(pack.status == FileStatus::Extracted);
  CHECK(pack.outputs == std::vector<std::string>{"script/dg/001.yaml", "story/001.lua",
                                                 "story/001_lines.yaml", "text/en/story_001.yaml"});
  CHECK(hasNote(pack, "script 0 is a copy of SCN/MAPHEAD.SCN"));
  CHECK(hasNote(pack, "1 scripts, 2 instructions, 1 texts"));
  CHECK(hasNote(pack, "bytes after the end of the scripts' code are the padding"));
  CHECK(hasNote(pack, "1 of the"));
  CHECK(hasNote(pack, "1 Lua modules in story/"));

  const auto listing = std::ranges::find(run->outputs, std::string{"script/dg/001"},
                                         [](const OutputFile& output) { return output.id.str(); });
  REQUIRE(listing != run->outputs.end());
  const std::string text(reinterpret_cast<const char*>(listing->bytes.data()),
                         listing->bytes.size());
  CHECK(text.contains("op: show_textbox\n  text: \"A\""));
}

TEST_CASE("a script that does not decode leaves its file partial") {
  const ScriptDisc fixture{true};
  const Disc disc{*fixture.source, fixture.volume, fixture.info};
  const auto run = runExtractors(disc, extractors());
  REQUIRE(run.has_value());
  const auto& pack = reportOf(*run, "SCN/DG.SCN");
  CHECK(pack.status == FileStatus::Partial);
  // Script 2's listing is written; it has no Lua module, since its section starts where nothing
  // decoded.
  CHECK(pack.outputs.size() == 5);
  REQUIRE(pack.unknowns.size() == 2);
  CHECK(pack.unknowns[0].starts_with("script 2: 0x0008: opcode 0x40"));
  // Its section starts on no instruction, so the walk of the maps' scripts leaves it out instead of
  // structuring it.
  CHECK(hasNote(pack, "script 2 does not structure"));
  CHECK(pack.unknowns[1].starts_with("script/dg/002: no Lua module: section 0x0008"));
}

TEST_CASE("a script 0 of the pack that is not a copy of the map header is not extracted") {
  const ScriptDisc fixture{false, false};
  const Disc disc{*fixture.source, fixture.volume, fixture.info};
  const auto run = runExtractors(disc, extractors());
  REQUIRE(run.has_value());
  // It would be story/000 like the map header, which is what the game loads as script 0.
  const auto& pack = reportOf(*run, "SCN/DG.SCN");
  CHECK(pack.status == FileStatus::Partial);
  CHECK(pack.outputs.empty());
  REQUIRE(pack.unknowns.size() == 1);
  CHECK(pack.unknowns[0].contains("script 0 is not a copy of SCN/MAPHEAD.SCN"));
  CHECK(reportOf(*run, "SCN/MAPHEAD.SCN").status == FileStatus::Extracted);
}
