#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asset_names.hpp"
#include "base_pack.hpp"
#include "disc_fixtures.hpp"
#include "tim_builder.hpp"
#include "written_pack.hpp"

using namespace dwe::dwx;
using namespace dwe::formats::image;
using dwe::formats::image::test::appendTim;
using dwe::formats::image::test::bytesOf;
using dwe::formats::image::test::directTim;
using dwe::formats::image::test::indexedTim;
using dwe::formats::image::test::TimSpec;
using dwe::test_support::TempFolder;

namespace {

constexpr std::size_t AllTimStride = 0x4800;
constexpr std::size_t AllTimEntries = 180;
constexpr std::size_t CardStride = 0x7000;
constexpr std::size_t CardEntries = 66;

TimSpec indexed4() {
    return indexedTim(TimPixelMode::Indexed4, {.x = 0, .y = 480, .width = 16, .height = 2},
                      std::vector<Color15>(32, 0x7FFF), {.x = 320, .y = 0, .width = 1, .height = 2},
                      bytesOf({0x10, 0x32, 0x54, 0x76}));
}

TimSpec direct16() {
    return directTim(TimPixelMode::Direct16, {.x = 0, .y = 0, .width = 2, .height = 1}, bytesOf({0x1F, 0, 0, 0}));
}

// A pack of `count` entries, each with a TIM at its start.
std::vector<std::byte> pack(std::size_t stride, std::size_t count) {
    std::vector<std::byte> bytes(stride * count);
    for (std::size_t i = 0; i < count; ++i) {
        std::vector<std::byte> tim;
        appendTim(tim, indexed4());
        std::ranges::copy(tim, bytes.begin() + static_cast<std::ptrdiff_t>(i * stride));
    }
    return bytes;
}

// A disc with every kind of image file and a file no extractor takes.
struct ImageDisc {
    ImageDisc() {
        auto allTim = pack(AllTimStride, AllTimEntries);
        allTim[(5 * AllTimStride) + 1000] = std::byte{0xAA}; // not padding
        allTim[7 * AllTimStride] = std::byte{0x11};          // not a TIM
        fixtures::IsoBuilder builder;
        builder.addFile("CHDAT/ALLTIM.TIM", std::move(allTim));
        builder.addFile("CARD/CARD.ALL", pack(CardStride, CardEntries));

        std::vector<std::byte> stacked;
        appendTim(stacked, indexed4());
        appendTim(stacked, direct16());
        stacked.resize(stacked.size() + 16);
        builder.addFile("ETCDAT/ETCTIM.BIN", std::move(stacked));

        std::vector<std::byte> one;
        appendTim(one, direct16());
        builder.addFile("ETCDAT/ONE.TIM", std::move(one));

        std::vector<std::byte> several;
        appendTim(several, indexed4());
        appendTim(several, indexed4());
        several.push_back(std::byte{0x99});
        builder.addFile("STDDAT/SEVERAL.TIM", std::move(several));

        std::vector<std::byte> noClut;
        appendTim(noClut,
                  directTim(TimPixelMode::Indexed4, {.x = 0, .y = 0, .width = 1, .height = 1}, bytesOf({0x10, 0x32})));
        builder.addFile("STDDAT/NOCLUT.TIM", std::move(noClut));

        builder.addFile("SOUND/VHB/ALL.VHB", fixtures::patternBytes(64, 1));
        image = builder.build();
        source =
            std::make_unique<dwe::formats::disc::MemorySectorSource>(image, dwe::formats::disc::SectorLayout::Cooked);
        volume = *dwe::formats::disc::readIsoVolume(*source);
    }

    std::vector<std::byte> image;
    std::unique_ptr<dwe::formats::disc::MemorySectorSource> source;
    dwe::formats::disc::IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
    DiscInfo info{.volumeId = "FIXTURE", .executablePath = "", .executableSha1 = "", .version = ""};
};

const FileReport& reportOf(const ExtractionRun& run, std::string_view path) {
    const auto found = std::ranges::find(run.files, path, [](const FileReport& report) { return report.file.path; });
    REQUIRE(found != run.files.end());
    return *found;
}

bool hasOutput(const FileReport& report, std::string_view path) {
    return std::ranges::contains(report.outputs, path);
}

} // namespace

TEST_CASE("asset ids follow the scheme") {
    CHECK(names::segment("T_TOGI") == "t_togi");
    CHECK(names::segment("16TAISEN") == "16taisen");
    CHECK(names::segment("A-B.C") == "a_b_c");
    CHECK(names::digimonAsset(nullptr, 3, "texture")->str() == "digimon/003/texture");
    GameTables tables;
    tables.ids.digimon = {"first", "second"};
    CHECK(names::digimonSlug(&tables, 1) == "second");
    CHECK(names::digimonAsset(&tables, 1, "model")->str() == "digimon/second/model");
    CHECK(names::digimonAsset(&tables, 2, "texture")->str() == "digimon/002/texture"); // past the table
    CHECK(names::cardImage(65)->str() == "card/065");
    CHECK(names::discImage("STDDAT/GRADE/AN.TIM", std::nullopt)->str() == "texture/stddat/grade/an");
    CHECK(names::discImage("STDDAT/T_TOGI.TIM", 2)->str() == "texture/stddat/t_togi/002");
    CHECK(names::packEntry(nullptr, "CHDAT/ALLTIM.TIM", 10)->str() == "digimon/010/texture");
    CHECK(names::packEntry(&tables, "CHDAT/ALLTIM.TIM", 0)->str() == "digimon/first/texture");
    CHECK(names::packEntry(&tables, "CARD/CARD.ALL", 1)->str() == "card/001");
    CHECK(names::tableDocument("moves")->str() == "data/moves");
    CHECK(names::metadataOf(*names::cardImage(1))->str() == "card/001_meta");
}

TEST_CASE("every image file of the disc is extracted to PNG and metadata") {
    const ImageDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());
    // The engine loads the pack written from the run and dwx validate finds nothing wrong with it.
    CHECK(dwe::dwx::testing::problemsOfWrittenPack(*run, fixture.info) == "");

    const auto& allTim = reportOf(*run, "CHDAT/ALLTIM.TIM");
    CHECK(allTim.extractor == "tim_pack");
    CHECK(allTim.status == FileStatus::Partial);
    CHECK(allTim.outputs.size() == (AllTimEntries - 1) * 2);
    CHECK(hasOutput(allTim, "digimon/000/texture.png"));
    CHECK(hasOutput(allTim, "digimon/179/texture_meta.yaml"));
    CHECK_FALSE(hasOutput(allTim, "digimon/007/texture.png"));
    REQUIRE(allTim.unknowns.size() == 2);
    CHECK(allTim.unknowns[0].contains("entry 5"));
    CHECK(allTim.unknowns[1].contains("entry 7"));

    const auto& cards = reportOf(*run, "CARD/CARD.ALL");
    CHECK(cards.status == FileStatus::Extracted);
    CHECK(cards.outputs.size() == CardEntries * 2);
    CHECK(hasOutput(cards, "card/065.png"));

    const auto& stacked = reportOf(*run, "ETCDAT/ETCTIM.BIN");
    CHECK(stacked.extractor == "stacked_tim");
    CHECK(stacked.status == FileStatus::Extracted);
    CHECK(stacked.outputs ==
          std::vector<std::string>{"texture/etcdat/etctim/000.png", "texture/etcdat/etctim/000_meta.yaml",
                                   "texture/etcdat/etctim/001.png", "texture/etcdat/etctim/001_meta.yaml"});

    const auto& one = reportOf(*run, "ETCDAT/ONE.TIM");
    CHECK(one.extractor == "tim_file");
    CHECK(one.status == FileStatus::Extracted);
    CHECK(one.outputs == std::vector<std::string>{"texture/etcdat/one.png", "texture/etcdat/one_meta.yaml"});

    const auto& several = reportOf(*run, "STDDAT/SEVERAL.TIM");
    CHECK(several.status == FileStatus::Partial); // the byte after the second image
    CHECK(hasOutput(several, "texture/stddat/several/001.png"));

    const auto& noClut = reportOf(*run, "STDDAT/NOCLUT.TIM");
    CHECK(noClut.status == FileStatus::Partial);
    CHECK(noClut.outputs.empty()); // no PNG, so nothing for metadata to describe

    CHECK(reportOf(*run, "SOUND/VHB/ALL.VHB").status == FileStatus::Pending);

    const auto png = std::ranges::find(run->outputs, "texture/etcdat/one.png", outputPath);
    REQUIRE(png != run->outputs.end());
    CHECK(png->bytes.size() > 8);
    CHECK(png->bytes[1] == std::byte{'P'});

    const TempFolder temp;
    REQUIRE(prepareOutputFolder(temp.path() / "base", false).has_value());
    REQUIRE(writeBasePack(temp.path() / "base", *run, fixture.info).has_value());
    const auto assets = checkBasePack(temp.path() / "base", *run);
    REQUIRE(assets.has_value());
    CHECK(*assets == run->outputs.size() + 1);
}

TEST_CASE("the metadata keeps the video memory rectangles and every palette") {
    const ImageDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());
    const auto meta = std::ranges::find(run->outputs, "card/000_meta.yaml", outputPath);
    REQUIRE(meta != run->outputs.end());
    const std::string text(reinterpret_cast<const char*>(meta->bytes.data()), meta->bytes.size());
    const auto yaml = dwe::core::parseYaml(text);
    REQUIRE(yaml.has_value());
    CHECK(yaml->find("source")->text() == "CARD/CARD.ALL");
    CHECK(yaml->find("entry")->text() == "0");
    CHECK(yaml->find("mode")->text() == "indexed4");
    CHECK(yaml->find("width")->text() == "4");
    CHECK(yaml->find("image_rect")->find("x")->text() == "320");
    CHECK(yaml->find("clut_rect")->find("height")->text() == "2");
    CHECK(yaml->find("png")->text() == "indexed");
    REQUIRE(yaml->find("palettes")->items().size() == 2);
    CHECK(yaml->find("palettes")->items()[1].text().starts_with("7fff 7fff"));
    CHECK(yaml->find("warnings") == nullptr);
}

TEST_CASE("two extractions of the same disc write the same bytes") {
    // The image files go through several extractors, leave partial and pending files and repeat outputs, and the
    // run spreads them over several threads as dwx extract does: the pack must not depend on how the threads went.
    const ImageDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info};
    const TempFolder temp;
    std::vector<std::map<std::string, std::string>> packs;
    for (const auto* name : {"first", "second"}) {
        const auto run = runExtractors(disc, extractors());
        REQUIRE(run.has_value());
        const auto folder = temp.path() / name;
        REQUIRE(prepareOutputFolder(folder, false).has_value());
        REQUIRE(writeBasePack(folder, *run, fixture.info).has_value());
        packs.push_back(dwe::dwx::testing::readTree(folder));
    }
    CHECK(packs[0].size() > 400);
    CHECK(packs[0].contains("dwx/coverage.yaml"));
    CHECK(packs[0].contains("pack.yaml"));
    CHECK(packs[0] == packs[1]);
}
