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

// A disc with one complete map, a map file no record names and a record whose file is missing. Records 0 and 1 name
// the same file.
struct MapDisc {
    MapDisc() {
        tables.tables.mapEntries = {entry("FIELD", HasNpcs | UnreadFlag), entry("FIELD", HasNpcs),
                                    entry("GONE", HasNpcs)};
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
    REQUIRE(background.unknowns.size() == 1);
    CHECK(background.unknowns[0].starts_with("palettes 3 to 4"));

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
