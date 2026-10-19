#include <dwe/assets/pack_manifest.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base_pack.hpp"
#include "disc_fixtures.hpp"
#include "temp_folder.hpp"

using namespace dwe::dwx;
using dwe::assets::AssetId;
using dwe::assets::testing::TempFolder;
using dwe::formats::disc::IsoFile;
using dwe::formats::disc::IsoVolume;
using dwe::formats::disc::MemorySectorSource;
using dwe::formats::disc::SectorLayout;

namespace {

// A test extractor for the files of folder A: it copies each one to test/<name>.bin, reports an unknown and a note
// for TWO and fails on BAD.
bool handlesFolderA(std::string_view path) {
    return path.starts_with("A/");
}

dwe::core::Result<Extraction> copyFile(const Disc& disc, const IsoFile& file) {
    if (file.path == "A/BAD.BIN") {
        return dwe::core::fail("cannot read this one");
    }
    auto bytes = dwe::formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const std::string name = file.path == "A/ONE.BIN" ? "one" : "two";
    Extraction extraction;
    extraction.outputs.push_back(
        OutputFile{.id = *AssetId::parse("test/" + name), .extension = "bin", .bytes = std::move(*bytes)});
    if (file.path == "A/TWO.BIN") {
        extraction.unknowns.emplace_back("the last byte means nothing yet");
        extraction.notes.emplace_back("a note for the reader");
    }
    return extraction;
}

dwe::core::Result<Extraction> clashingFile(const Disc& /*disc*/, const IsoFile& /*file*/) {
    Extraction extraction;
    extraction.outputs.push_back(OutputFile{.id = *AssetId::parse("dwx/coverage"), .extension = "bin", .bytes = {}});
    return extraction;
}

// A test extractor that gives the same bytes for every file of folder A, once as an image and once as text.
dwe::core::Result<Extraction> twinFile(const Disc& /*disc*/, const IsoFile& file) {
    std::string name = file.path;
    std::ranges::replace(name, '/', '_');
    std::ranges::transform(name, name.begin(),
                           [](char c) { return c == '.' ? '_' : static_cast<char>(std::tolower(c)); });
    Extraction extraction;
    for (const auto* extension : {"png", "yaml"}) {
        const std::string suffix = std::string_view{extension} == "png" ? "" : "_text";
        extraction.outputs.push_back(
            OutputFile{.id = *AssetId::parse("twin/" + name.substr(0, name.size() - 4) + suffix),
                       .extension = extension,
                       .bytes = fixtures::patternBytes(16, 7)});
    }
    return extraction;
}

constexpr std::array TestRegistry = {Extractor{.name = "copy", .handles = handlesFolderA, .extract = copyFile}};
constexpr std::array TwinRegistry = {Extractor{.name = "twin", .handles = handlesFolderA, .extract = twinFile}};
constexpr std::array ClashingRegistry = {
    Extractor{.name = "clash", .handles = handlesFolderA, .extract = clashingFile}};

// A cooked disc with three files the test extractor takes and one it does not.
struct TestDisc {
    TestDisc() {
        fixtures::IsoBuilder builder;
        builder.addFile("A/ONE.BIN", fixtures::patternBytes(3000, 1));
        builder.addFile("A/TWO.BIN", fixtures::patternBytes(10, 2));
        builder.addFile("A/BAD.BIN", fixtures::patternBytes(10, 3));
        builder.addFile("B/OTHER.DAT", fixtures::patternBytes(100, 4));
        image = builder.build();
        source = std::make_unique<MemorySectorSource>(image, SectorLayout::Cooked);
        volume = *dwe::formats::disc::readIsoVolume(*source);
    }

    [[nodiscard]] Disc disc() const { return Disc{*source, volume, info}; }

    std::vector<std::byte> image;
    std::unique_ptr<MemorySectorSource> source;
    IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
    DiscInfo info{.volumeId = "FIXTURE", .executablePath = "", .executableSha1 = "", .version = ""};
};

std::map<std::string, std::string> readTree(const std::filesystem::path& root) {
    std::map<std::string, std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
        if (entry.is_regular_file()) {
            std::ifstream stream{entry.path(), std::ios::binary};
            files[entry.path().lexically_relative(root).generic_string()] =
                std::string{std::istreambuf_iterator<char>{stream}, {}};
        }
    }
    return files;
}

} // namespace

TEST_CASE("files get the status of their extraction") {
    const TestDisc fixture;
    const auto run = runExtractors(fixture.disc(), TestRegistry);
    REQUIRE(run.has_value());
    REQUIRE(run->files.size() == 4);
    // The volume lists files by path.
    CHECK(run->files[0].file.path == "A/BAD.BIN");
    CHECK(run->files[0].status == FileStatus::Partial);
    CHECK(run->files[0].unknowns.size() == 1);
    CHECK(run->files[1].status == FileStatus::Extracted);
    CHECK(run->files[1].outputs == std::vector<std::string>{"test/one.bin"});
    CHECK(run->files[2].status == FileStatus::Partial);
    CHECK(run->files[3].status == FileStatus::Pending);
    CHECK(run->files[3].extractor.empty());
    CHECK(run->files[3].sha1 == dwe::core::toHex(dwe::core::sha1(fixtures::patternBytes(100, 4))));

    const CoverageSummary summary = summarize(*run);
    CHECK(summary.files == 4);
    CHECK(summary.extracted == 1);
    CHECK(summary.partial == 2);
    CHECK(summary.pending == 1);
    CHECK(summary.bytes == 3120);
    CHECK(summary.bytesCovered == 3020);
    CHECK(summary.outputs == 2);
}

TEST_CASE("outputs with the same bytes are reported as duplicates") {
    const TestDisc fixture;
    const auto run = runExtractors(fixture.disc(), TwinRegistry);
    REQUIRE(run.has_value());
    // Each file of folder A gives the same image and the same text; an image and a text are never the same output.
    REQUIRE(run->duplicates.size() == 2);
    CHECK(run->duplicates[0] == std::vector<std::string>{"twin/a_bad.png", "twin/a_one.png", "twin/a_two.png"});
    CHECK(run->duplicates[1].size() == 3);
    CHECK(summarize(*run).duplicates == 4);
    const auto text = coverageYaml(*run, fixture.info);
    REQUIRE(text.has_value());
    const auto yaml = dwe::core::parseYaml(*text);
    REQUIRE(yaml.has_value());
    CHECK(yaml->find("summary")->find("duplicate_outputs")->text() == "4");
    CHECK(yaml->find("duplicates")->items()[0].items().size() == 3);
}

TEST_CASE("an output id that is reserved or repeated is an error") {
    const TestDisc fixture;
    CHECK_FALSE(runExtractors(fixture.disc(), ClashingRegistry).has_value());
}

TEST_CASE("the coverage report lists every file") {
    const TestDisc fixture;
    const auto run = runExtractors(fixture.disc(), TestRegistry);
    REQUIRE(run.has_value());
    const auto text = coverageYaml(*run, fixture.info);
    REQUIRE(text.has_value());
    const auto yaml = dwe::core::parseYaml(*text);
    REQUIRE(yaml.has_value());
    CHECK(yaml->find("summary")->find("pending")->text() == "1");
    REQUIRE(yaml->find("files")->items().size() == 4);
    const auto& two = yaml->find("files")->items()[2];
    CHECK(two.find("path")->text() == "A/TWO.BIN");
    CHECK(two.find("status")->text() == "partial");
    CHECK(two.find("extractor")->text() == "copy");
    CHECK(two.find("unknowns")->items().size() == 1);
    CHECK(two.find("notes")->items()[0].text() == "a note for the reader");
    CHECK(yaml->find("files")->items()[3].find("extractor") == nullptr);
}

TEST_CASE("the base pack is written, mounts and is the same on every run") {
    const TestDisc fixture;
    const auto run = runExtractors(fixture.disc(), TestRegistry);
    REQUIRE(run.has_value());
    const TempFolder temp;
    const auto first = temp.path() / "first";
    const auto second = temp.path() / "second" / "nested";
    for (const auto& folder : {first, second}) {
        REQUIRE(prepareOutputFolder(folder, false).has_value());
        REQUIRE(writeBasePack(folder, *run, fixture.info).has_value());
    }
    const auto files = readTree(first);
    CHECK(files == readTree(second));
    CHECK(files.contains("pack.yaml"));
    CHECK(files.contains("dwx/coverage.yaml"));
    CHECK(files.contains(".dwx-extract"));
    CHECK(files.at("test/one.bin").size() == 3000);

    const auto manifest = dwe::assets::parsePackManifest(files.at("pack.yaml"));
    REQUIRE(manifest.has_value());
    CHECK(manifest->id == "base");
    CHECK(manifest->layer == dwe::assets::Layer::Base);

    const auto assets = checkBasePack(first, *run);
    REQUIRE(assets.has_value());
    CHECK(*assets == 3); // two outputs and the coverage report
}

TEST_CASE("the output folder is only emptied when dwx wrote it") {
    const TempFolder temp;
    temp.write("foreign/notes.txt", "mine");
    CHECK_FALSE(prepareOutputFolder(temp.path() / "foreign", false).has_value());
    REQUIRE(prepareOutputFolder(temp.path() / "foreign", true).has_value());
    CHECK(std::filesystem::exists(temp.path() / "foreign" / "notes.txt"));

    temp.write("previous/.dwx-extract", "");
    temp.write("previous/old/stale.png", "old");
    REQUIRE(prepareOutputFolder(temp.path() / "previous", false).has_value());
    CHECK(std::filesystem::is_empty(temp.path() / "previous"));
    CHECK(std::filesystem::exists(temp.path()));

    temp.write("file.txt", "not a folder");
    CHECK_FALSE(prepareOutputFolder(temp.path() / "file.txt", true).has_value());
}
