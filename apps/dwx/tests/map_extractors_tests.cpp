#include <dwe/assets/content_check.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>
#include <dwe/formats/exe/tables_yaml.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base_pack.hpp"
#include "cook.hpp"
#include "disc_fixtures.hpp"
#include "game_tables.hpp"
#include "map_fixtures.hpp"
#include "tim_builder.hpp"
#include "written_pack.hpp"

using namespace dwe::dwx;
using dwe::formats::exe::MapEntry;
using dwe::formats::image::Color15;
using dwe::formats::image::TimPixelMode;
using dwe::formats::image::test::buildTim;
using dwe::formats::image::test::indexedTim;

namespace {

constexpr std::uint8_t HasNpcs = 0x80;
constexpr std::uint8_t UnreadFlag = 0x20;
constexpr std::size_t PageTexels = std::size_t{256} * 256;

MapEntry entry(std::string filename, std::uint8_t flags) {
  MapEntry record;
  record.filename = std::move(filename);
  record.num8bppImages = 1;
  record.num4bppImages = 1;
  record.flags = flags;
  return record;
}

// A disc with one complete map, map files no record names, a record whose file is missing, a map
// whose sprites read an extra palette and two arena collision grids, one too short. Records 0 and 1
// name the same file.
struct MapDisc {
  MapDisc() {
    tables.tables.mapEntries = {entry("FIELD", HasNpcs | UnreadFlag), entry("FIELD", HasNpcs),
                                entry("GONE", HasNpcs), entry("GLOW", HasNpcs)};
    tables.ids = dwe::formats::exe::makeTableIds(tables.tables);

    // Full texture pages of 256 x 256 texels, as the game loads them: 128 and 64 halfwords wide.
    // The 8-bit one is the second page, where the sprite's texels are (u 256).
    dwe::formats::test::MapParts parts;
    parts.image8 = buildTim(indexedTim(
        TimPixelMode::Indexed8, {.x = 0, .y = 481, .width = 256, .height = 1},
        std::vector<Color15>(256, 0x001F), {.x = 512, .y = 0, .width = 128, .height = 256},
        std::vector<std::byte>(PageTexels, std::byte{1})));
    parts.image4 = buildTim(
        indexedTim(TimPixelMode::Indexed4, {.x = 0, .y = 480, .width = 16, .height = 1},
                   std::vector<Color15>(16, 0x7C00), {.x = 704, .y = 0, .width = 64, .height = 256},
                   std::vector<std::byte>(PageTexels / 2, std::byte{0x32})));
    fixtures::IsoBuilder builder;
    builder.addFile("MAP/MAP1/FIELD.MAP", dwe::formats::test::makeMap(parts));
    builder.addFile("MAP/MAP1/FIELD.TFS", dwe::formats::test::makeTfs(5, 1));
    builder.addFile("MAP/MAP2/LOST.MAP", dwe::formats::test::makeMap(parts));
    // A sprite with clut 17 reads video memory row 485, where palette 4 goes.
    parts.spriteClut = 17;
    builder.addFile("MAP/MAP1/GLOW.MAP", dwe::formats::test::makeMap(parts));
    builder.addFile("MAP/MAP1/GLOW.TFS", dwe::formats::test::makeTfs(5, 1));
    // Files no record names, without NPC data: one gets its options from its bytes, one has a
    // record's id.
    parts.spriteClut = -1;
    parts.npcCount = 0;
    builder.addFile("MAP/MAP2/ALONE.MAP", dwe::formats::test::makeMap(parts));
    builder.addFile("MAP/MAP2/ALONE.TFS", dwe::formats::test::makeTfs(1, 1));
    builder.addFile("MAP/MAP2/FIELD.MAP", dwe::formats::test::makeMap(parts));
    std::vector<std::byte> arena(10000, std::byte{0});
    arena[0] = std::byte{0x80};
    arena[9999] = std::byte{7};
    builder.addFile("STDDAT/B_TOGI.ATR", std::move(arena));
    builder.addFile("STDDAT/SHORT.ATR", std::vector<std::byte>(10, std::byte{0}));
    image = builder.build();
    source = std::make_unique<dwe::formats::disc::MemorySectorSource>(
        image, dwe::formats::disc::SectorLayout::Cooked);
    volume = *dwe::formats::disc::readIsoVolume(*source);
  }

  std::vector<std::byte> image;
  std::unique_ptr<dwe::formats::disc::MemorySectorSource> source;
  dwe::formats::disc::IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
  DiscInfo info{.volumeId = "FIXTURE", .executablePath = "", .executableSha1 = "", .version = ""};
  GameTables tables;
};

const FileReport& reportOf(const ExtractionRun& run, std::string_view path) {
  const auto found =
      std::ranges::find(run.files, path, [](const FileReport& report) { return report.file.path; });
  REQUIRE(found != run.files.end());
  return *found;
}

// Loads map "field" from a pack as the game does (render::MapScene: its data, then its images),
// after checking the pack as dwx validate and dwe-game --check-content do.
dwe::assets::MapImages loadedMap(const dwe::assets::FileSystem& files,
                                 const std::filesystem::path& root) {
  const std::vector<std::filesystem::path> roots{root};
  const auto vfs = dwe::assets::VirtualFileSystem::mount(files, roots);
  REQUIRE_MESSAGE(vfs.has_value(), vfs.error().message);
  for (const auto& problem : dwe::assets::checkContent(*vfs)) {
    FAIL_CHECK(problem.file, ": ", problem.message);
  }
  const auto document = dwe::assets::loadMapDocument(*vfs, "field");
  REQUIRE_MESSAGE(document.has_value(), document.error().message);
  auto images = dwe::assets::loadMapImages(*vfs, *document);
  REQUIRE_MESSAGE(images.has_value(), images.error().message);
  return std::move(*images);
}

// What the game draws a map with: one palette per background, the tiles they colour, the texture
// pages and the sprite that takes the time of day's palette (clut -1), which are the background
// palettes.
void checkSameMap(const dwe::assets::MapImages& loaded, const dwe::assets::MapImages& extracted) {
  CHECK(loaded.backgroundPalettes.size() == 5);
  CHECK(loaded.backgroundPalettes == extracted.backgroundPalettes);
  CHECK(loaded.background.indices == extracted.background.indices);
  CHECK(loaded.backgroundVariants == extracted.backgroundVariants);
  REQUIRE(loaded.pages.size() == 2);
  CHECK(loaded.pages[0].image.indices == extracted.pages[0].image.indices);
  CHECK(loaded.pages[1].cluts == extracted.pages[1].cluts);
  REQUIRE(loaded.sprites.placements.size() == 1);
  const std::size_t texture = loaded.sprites.placements[0].texture;
  REQUIRE(texture != dwe::assets::MapSpriteSheet::NoTexture);
  CHECK(loaded.sprites.textures.at(texture).timeOfDayPalette);
}

bool mentions(const std::vector<std::string>& texts, std::string_view text) {
  return std::ranges::any_of(texts,
                             [text](const std::string& each) { return each.contains(text); });
}

} // namespace

TEST_CASE("a map gives its data and its images") {
  const MapDisc fixture;
  const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
  const auto run = runExtractors(disc, extractors());
  REQUIRE(run.has_value());
  // The engine loads the pack written from the run and dwx validate only finds the warps to record
  // 1, which names the file of record 0 and so has no data of its own (the disc has no such
  // record).
  CHECK(
      dwe::dwx::testing::problemsOfWrittenPack(*run, fixture.info) ==
      "map/alone/data.yaml: $.warps[0].target_map_id: 'field_1' names map/field_1/data, which no pack has\n"
      "map/field/data.yaml: $.warps[0].target_map_id: 'field_1' names map/field_1/data, which no pack has\n"
      "map/glow/data.yaml: $.warps[0].target_map_id: 'field_1' names map/field_1/data, which no pack has\n");

  const auto& map = reportOf(*run, "MAP/MAP1/FIELD.MAP");
  CHECK(map.extractor == "map");
  CHECK(map.outputs == std::vector<std::string>{"map/field/data.yaml", "map/field/texture8/000.png",
                                                "map/field/texture8/000_meta.yaml",
                                                "map/field/texture4/000.png",
                                                "map/field/texture4/000_meta.yaml"});
  CHECK(map.status == FileStatus::Partial);
  REQUIRE(map.unknowns.size() == 1);
  CHECK(map.unknowns[0].contains("flag 0x20"));
  CHECK(mentions(map.notes, "MAP_ENTRIES[1] (field_1) names the same file"));

  const auto data = std::ranges::find(run->outputs, "map/field/data.yaml", outputPath);
  REQUIRE(data != run->outputs.end());
  const auto yaml = dwe::core::parseYaml(
      std::string(reinterpret_cast<const char*>(data->bytes.data()), data->bytes.size()));
  REQUIRE(yaml.has_value());
  CHECK(yaml->find("collision")->find("rows")->items().size() == 100);

  const auto& background = reportOf(*run, "MAP/MAP1/FIELD.TFS");
  CHECK(background.extractor == "map_background");
  CHECK(background.outputs == std::vector<std::string>{"map/field/background_day.png",
                                                       "map/field/background_dusk.png",
                                                       "map/field/background_night.png",
                                                       "map/field/background_palette3.png",
                                                       "map/field/background_palette4.png"});
  CHECK(background.status == FileStatus::Extracted);
  CHECK(mentions(background.notes, "palettes 3 and 4 are never shown"));

  const auto& glow = reportOf(*run, "MAP/MAP1/GLOW.TFS");
  CHECK(glow.status == FileStatus::Partial);
  CHECK(mentions(glow.notes, "palette 3 is never shown"));
  REQUIRE(glow.unknowns.size() == 1);
  CHECK(glow.unknowns[0].starts_with("palette 4: 1 sprite(s) read its video memory row 485"));

  const auto& lost = reportOf(*run, "MAP/MAP2/LOST.MAP");
  CHECK(lost.status == FileStatus::Partial);
  CHECK(lost.outputs.empty());
  CHECK(mentions(lost.unknowns, "no MAP_ENTRIES record names"));
  CHECK(mentions(lost.unknowns, "past its warps"));

  const auto& alone = reportOf(*run, "MAP/MAP2/ALONE.MAP");
  CHECK(alone.status == FileStatus::Extracted);
  CHECK(alone.outputs ==
        std::vector<std::string>{"map/alone/data.yaml", "map/alone/texture8/000.png",
                                 "map/alone/texture8/000_meta.yaml", "map/alone/texture4/000.png",
                                 "map/alone/texture4/000_meta.yaml"});
  CHECK(mentions(alone.notes, "no MAP_ENTRIES record names the file, so the game never loads it"));
  const auto& aloneBackground = reportOf(*run, "MAP/MAP2/ALONE.TFS");
  CHECK(aloneBackground.status == FileStatus::Extracted);
  CHECK(aloneBackground.outputs == std::vector<std::string>{"map/alone/background_day.png"});

  const auto& named = reportOf(*run, "MAP/MAP2/FIELD.MAP");
  CHECK(named.status == FileStatus::Partial);
  CHECK(mentions(named.unknowns, "is a record's id"));

  const dwe::test_support::TempFolder temp;
  REQUIRE(prepareOutputFolder(temp.path() / "base", false).has_value());
  REQUIRE(writeBasePack(temp.path() / "base", *run, fixture.info).has_value());
  CHECK(checkBasePack(temp.path() / "base", *run).has_value());
}

TEST_CASE(
    "a map loads from the pack dwx extract writes, from its cooked pack and from that pack's .pak") {
  const MapDisc fixture;
  const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
  const auto run = runExtractors(disc, extractors());
  REQUIRE(run.has_value());
  const dwe::test_support::TempFolder temp;
  const std::filesystem::path base = temp.path() / "base";
  REQUIRE(prepareOutputFolder(base, false).has_value());
  REQUIRE(writeBasePack(base, *run, fixture.info).has_value());
  const dwe::assets::DiskFileSystem disk;
  const dwe::assets::MapImages extracted = loadedMap(disk, base);
  checkSameMap(extracted, extracted);

  // dwx cook keeps the backgrounds and texture pages as PNG: the game picks their palettes while it
  // runs.
  const std::filesystem::path cooked = temp.path() / "cooked";
  const auto summary = cookPack(cookers(), base, cooked);
  REQUIRE_MESSAGE(summary.has_value(), summary.error().message);
  CHECK(std::filesystem::exists(cooked / "map/field/background_day.png"));
  checkSameMap(loadedMap(disk, cooked), extracted);

  const std::filesystem::path archive = temp.path() / "base.pak";
  REQUIRE(dwe::assets::writePak(disk, cooked, archive).has_value());
  const auto pak = dwe::assets::PakFileSystem::open(archive);
  REQUIRE(pak.has_value());
  checkSameMap(loadedMap(**pak, archive), extracted);
}

TEST_CASE("maps need the executable's tables") {
  const MapDisc fixture;
  const Disc disc{*fixture.source, fixture.volume, fixture.info};
  const auto run = runExtractors(disc, extractors());
  REQUIRE(run.has_value());
  for (const auto* path : {"MAP/MAP1/FIELD.MAP", "MAP/MAP1/FIELD.TFS"}) {
    const auto& report = reportOf(*run, path);
    CHECK(report.status == FileStatus::Partial);
    CHECK(mentions(report.unknowns, "tables are not known"));
  }
}

TEST_CASE("an arena collision grid becomes YAML") {
  const MapDisc fixture;
  const Disc disc{*fixture.source, fixture.volume, fixture.info};
  const auto run = runExtractors(disc, extractors());
  REQUIRE(run.has_value());

  const auto& arena = reportOf(*run, "STDDAT/B_TOGI.ATR");
  CHECK(arena.extractor == "arena_collision");
  CHECK(arena.status == FileStatus::Extracted);
  CHECK(arena.outputs == std::vector<std::string>{"arena/b_togi/collision.yaml"});
  const auto output = std::ranges::find(run->outputs, "arena/b_togi/collision.yaml", outputPath);
  REQUIRE(output != run->outputs.end());
  const auto yaml = dwe::core::parseYaml(
      std::string(reinterpret_cast<const char*>(output->bytes.data()), output->bytes.size()));
  REQUIRE(yaml.has_value());
  const auto& rows = yaml->find("rows")->items();
  REQUIRE(rows.size() == 100);
  CHECK(rows[0].text().starts_with("#."));
  CHECK(rows[99].text().ends_with("A"));

  CHECK(reportOf(*run, "STDDAT/SHORT.ATR").status == FileStatus::Partial);
}
