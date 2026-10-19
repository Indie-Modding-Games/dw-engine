#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base_pack.hpp"
#include "disc_fixtures.hpp"
#include "exe_fixture.hpp"
#include "game_tables.hpp"

using namespace dwe::dwx;

namespace {

constexpr std::string_view ExecutablePath = "SLUS_000.01";

std::vector<std::byte> textBytes(std::string_view text) {
    const auto bytes = std::as_bytes(std::span{text});
    return {bytes.begin(), bytes.end()};
}

// A disc with the synthetic executable of the formats tests, its boot settings and an executable it does not boot.
struct TableDisc {
    TableDisc() {
        const auto executable = dwe::formats::test::makeExecutableFixture();
        fixtures::IsoBuilder builder;
        builder.addFile("SYSTEM.CNF", textBytes("BOOT = cdrom:\\SLUS_000.01;1\r\nTCB=4\r\n\r\nno setting here\r\n"));
        builder.addFile(std::string{ExecutablePath}, executable.file);
        builder.addFile("SLUS_000.02", executable.file);
        image = builder.build();
        source =
            std::make_unique<dwe::formats::disc::MemorySectorSource>(image, dwe::formats::disc::SectorLayout::Cooked);
        volume = *dwe::formats::disc::readIsoVolume(*source);
        auto read = readGameTables(executable.file, executable.layout);
        REQUIRE(read.has_value());
        tables = std::move(*read);
    }

    std::vector<std::byte> image;
    std::unique_ptr<dwe::formats::disc::MemorySectorSource> source;
    dwe::formats::disc::IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
    DiscInfo info{
        .volumeId = "FIXTURE", .executablePath = std::string{ExecutablePath}, .executableSha1 = "", .version = "test"};
    GameTables tables;
};

const FileReport& reportOf(const ExtractionRun& run, std::string_view path) {
    const auto found = std::ranges::find(run.files, path, [](const FileReport& report) { return report.file.path; });
    REQUIRE(found != run.files.end());
    return *found;
}

bool hasNote(const FileReport& report, std::string_view text) {
    return std::ranges::any_of(report.notes, [text](const std::string& note) { return note.contains(text); });
}

} // namespace

TEST_CASE("the executable gives the documents of its tables") {
    const TableDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());

    const auto& executable = reportOf(*run, ExecutablePath);
    CHECK(executable.extractor == "executable");
    CHECK(executable.status == FileStatus::Extracted);
    CHECK(executable.outputs.size() == 12);
    CHECK(std::ranges::contains(executable.outputs, "data/digimon.yaml"));
    CHECK(std::ranges::contains(executable.outputs, "data/maps.yaml"));
    CHECK(hasNote(executable, "only the data tables are taken"));
    CHECK(hasNote(executable, "tables: digimon, skeletons, moves"));
    // The map entries of the fixture name files that this disc does not have.
    CHECK(hasNote(executable, "MAP/MAP1/START.MAP, which is not on the disc"));

    const auto& other = reportOf(*run, "SLUS_000.02");
    CHECK(other.status == FileStatus::Partial);
    CHECK(other.outputs.empty());
}

TEST_CASE("without the tables the executable stays partial") {
    const TableDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());
    const auto& executable = reportOf(*run, ExecutablePath);
    CHECK(executable.status == FileStatus::Partial);
    REQUIRE(executable.unknowns.size() == 1);
    CHECK(executable.unknowns[0].contains("not known"));
}

TEST_CASE("the boot settings become a YAML mapping") {
    const TableDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());
    const auto& boot = reportOf(*run, "SYSTEM.CNF");
    CHECK(boot.extractor == "boot_config");
    CHECK(boot.status == FileStatus::Partial);
    REQUIRE(boot.unknowns.size() == 1);
    CHECK(boot.unknowns[0] == "line 4 is not KEY = VALUE");

    const auto output = std::ranges::find(run->outputs, "disc/boot.yaml", outputPath);
    REQUIRE(output != run->outputs.end());
    const std::string text(reinterpret_cast<const char*>(output->bytes.data()), output->bytes.size());
    const auto yaml = dwe::core::parseYaml(text);
    REQUIRE(yaml.has_value());
    CHECK(yaml->find("boot")->text() == "cdrom:\\\\SLUS_000.01;1");
    CHECK(yaml->find("tcb")->text() == "4");
}
