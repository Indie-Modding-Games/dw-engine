#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>
#include <dwe/formats/exe/tables_yaml.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base_pack.hpp"
#include "disc_fixtures.hpp"
#include "game_tables.hpp"
#include "map_fixtures.hpp"
#include "temp_folder.hpp"
#include "tim_builder.hpp"

using namespace dwe::dwx;
using dwe::formats::exe::MapEntry;
using dwe::formats::image::Color15;
using dwe::formats::image::TimPixelMode;
using dwe::formats::image::test::buildTim;
using dwe::formats::image::test::bytesOf;
using dwe::formats::image::test::indexedTim;

namespace {

constexpr std::uint8_t HasNpcs = 0x80;
constexpr std::uint8_t UnreadFlag = 0x20;

MapEntry entry(std::string filename, std::uint8_t flags) {
    MapEntry record;
    record.filename = std::move(filename);
    record.num8bppImages = 1;
    record.num4bppImages = 1;
    record.flags = flags;
    return record;
}

// A disc with one complete map, a map file no record names, a record whose file is missing, a map whose sprites read
// an extra palette and two arena collision grids, one too short. Records 0 and 1 name the same file.
struct MapDisc {
    MapDisc() {
        tables.tables.mapEntries = {entry("FIELD", HasNpcs | UnreadFlag), entry("FIELD", HasNpcs),
                                    entry("GONE", HasNpcs), entry("GLOW", HasNpcs)};
        tables.ids = dwe::formats::exe::makeTableIds(tables.tables);

        dwe::formats::test::MapParts parts;
        parts.image8 = buildTim(indexedTim(TimPixelMode::Indexed8, {.x = 0, .y = 481, .width = 256, .height = 1},
                                           std::vector<Color15>(256, 0x001F),
                                           {.x = 640, .y = 0, .width = 1, .height = 1}, bytesOf({1, 2})));
        parts.image4 = buildTim(indexedTim(TimPixelMode::Indexed4, {.x = 0, .y = 480, .width = 16, .height = 1},
                                           std::vector<Color15>(16, 0x7C00),
                                           {.x = 704, .y = 0, .width = 1, .height = 1}, bytesOf({0x10, 0x32})));
        fixtures::IsoBuilder builder;
        builder.addFile("MAP/MAP1/FIELD.MAP", dwe::formats::test::makeMap(parts));
        builder.addFile("MAP/MAP1/FIELD.TFS", dwe::formats::test::makeTfs(5, 1));
        builder.addFile("MAP/MAP2/LOST.MAP", dwe::formats::test::makeMap(parts));
        // A sprite with clut 17 reads video memory row 485, where palette 4 goes.
        parts.spriteClut = 17;
        builder.addFile("MAP/MAP1/GLOW.MAP", dwe::formats::test::makeMap(parts));
        builder.addFile("MAP/MAP1/GLOW.TFS", dwe::formats::test::makeTfs(5, 1));
        std::vector<std::byte> arena(10000, std::byte{0});
        arena[0] = std::byte{0x80};
        arena[9999] = std::byte{7};
        builder.addFile("STDDAT/B_TOGI.ATR", std::move(arena));
        builder.addFile("STDDAT/SHORT.ATR", std::vector<std::byte>(10, std::byte{0}));
        image = builder.build();
        source =
            std::make_unique<dwe::formats::disc::MemorySectorSource>(image, dwe::formats::disc::SectorLayout::Cooked);
        volume = *dwe::formats::disc::readIsoVolume(*source);
    }

    std::vector<std::byte> image;
    std::unique_ptr<dwe::formats::disc::MemorySectorSource> source;
    dwe::formats::disc::IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
    DiscInfo info{.volumeId = "FIXTURE", .executablePath = "", .executableSha1 = "", .version = ""};
    GameTables tables;
};

const FileReport& reportOf(const ExtractionRun& run, std::string_view path) {
    const auto found = std::ranges::find(run.files, path, [](const FileReport& report) { return report.file.path; });
    REQUIRE(found != run.files.end());
    return *found;
}

bool mentions(const std::vector<std::string>& texts, std::string_view text) {
    return std::ranges::any_of(texts, [text](const std::string& each) { return each.contains(text); });
}

} // namespace

TEST_CASE("a map gives its data and its images") {
    const MapDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());

    const auto& map = reportOf(*run, "MAP/MAP1/FIELD.MAP");
    CHECK(map.extractor == "map");
    CHECK(map.outputs == std::vector<std::string>{"map/field/data.yaml", "map/field/texture8/000.png",
                                                  "map/field/texture8/000_meta.yaml", "map/field/texture4/000.png",
                                                  "map/field/texture4/000_meta.yaml"});
    CHECK(map.status == FileStatus::Partial);
    REQUIRE(map.unknowns.size() == 1);
    CHECK(map.unknowns[0].contains("flag 0x20"));
    CHECK(mentions(map.notes, "MAP_ENTRIES[1] (field_1) names the same file"));

    const auto data = std::ranges::find(run->outputs, "map/field/data.yaml", outputPath);
    REQUIRE(data != run->outputs.end());
    const auto yaml =
        dwe::core::parseYaml(std::string(reinterpret_cast<const char*>(data->bytes.data()), data->bytes.size()));
    REQUIRE(yaml.has_value());
    CHECK(yaml->find("collision")->find("rows")->items().size() == 100);

    const auto& background = reportOf(*run, "MAP/MAP1/FIELD.TFS");
    CHECK(background.extractor == "map_background");
    CHECK(background.outputs ==
          std::vector<std::string>{"map/field/background_day.png", "map/field/background_dusk.png",
                                   "map/field/background_night.png", "map/field/background_palette3.png",
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

    const dwe::assets::testing::TempFolder temp;
    REQUIRE(prepareOutputFolder(temp.path() / "base", false).has_value());
    REQUIRE(writeBasePack(temp.path() / "base", *run, fixture.info).has_value());
    CHECK(checkBasePack(temp.path() / "base", *run).has_value());
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
    const auto yaml =
        dwe::core::parseYaml(std::string(reinterpret_cast<const char*>(output->bytes.data()), output->bytes.size()));
    REQUIRE(yaml.has_value());
    const auto& rows = yaml->find("rows")->items();
    REQUIRE(rows.size() == 100);
    CHECK(rows[0].text().starts_with("#."));
    CHECK(rows[99].text().ends_with("A"));

    CHECK(reportOf(*run, "STDDAT/SHORT.ATR").status == FileStatus::Partial);
}
