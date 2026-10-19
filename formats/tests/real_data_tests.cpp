#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/exe/tables.hpp>
#include <dwe/formats/exe/tables_yaml.hpp>
#include <dwe/formats/map/map_file.hpp>
#include <dwe/formats/map/map_yaml.hpp>
#include <dwe/formats/map/tfs_file.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "schema_check.hpp"

// Checks against the user's own extracted files, never against anything in the repository. They run only when the
// variables point at the files: DWE_EXE_SAMPLE = the US executable, DWE_MAP_DIR = the MAP folder of the disc.
// Only counts are printed.

namespace exe = dwe::formats::exe;
namespace map = dwe::formats::map;

namespace {

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::vector<char> chars{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

const char* environment(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

std::optional<exe::ExecutableTables> readSampleTables() {
    const char* path = environment("DWE_EXE_SAMPLE");
    if (path == nullptr) {
        return std::nullopt;
    }
    const auto file = readFile(path).value_or(std::vector<std::byte>{});
    REQUIRE_FALSE(file.empty());
    const auto image = exe::MemoryImage::fromExecutable(file);
    REQUIRE_MESSAGE(image.has_value(), image.error().message);
    const auto layout = exe::findExecutableLayout("us").value_or(exe::ExecutableLayout{});
    auto tables = exe::readExecutableTables(*image, layout);
    REQUIRE_MESSAGE(tables.has_value(), tables.error().message);
    return std::move(*tables);
}

} // namespace

TEST_CASE("real data: every table of the executable is read and matches its schema") {
    const auto tables = readSampleTables();
    if (!tables) {
        MESSAGE("DWE_EXE_SAMPLE is not set; skipped");
        return;
    }
    CHECK(tables->digimonData.size() == 180);
    CHECK(tables->mapEntries.size() == 255);
    const exe::TablesYaml yaml = exe::tablesToYaml(*tables);
    std::size_t problems = 0;
    for (const exe::TableDocument& document : yaml.documents) {
        const auto found = dwe::formats::test::checkAgainstSchema(document.document, document.name);
        problems += found.size();
        CHECK_MESSAGE(found.empty(), document.name, ": ", found.empty() ? std::string{} : found.front());
    }
    std::size_t escaped = 0;
    for (const std::string& note : yaml.notes) {
        MESSAGE(note);
        escaped += note.contains("escaped") ? 1 : 0;
    }
    MESSAGE(std::format("{} documents, {} schema problems, {} notes ({} escaped texts, {} references outside their "
                        "table)",
                        yaml.documents.size(), problems, yaml.notes.size(), escaped, yaml.notes.size() - escaped));
}

TEST_CASE("real data: every map and background of the disc is read") {
    const char* mapDir = environment("DWE_MAP_DIR");
    const auto tables = mapDir != nullptr ? readSampleTables() : std::nullopt;
    if (!tables) {
        MESSAGE("DWE_EXE_SAMPLE or DWE_MAP_DIR is not set; skipped");
        return;
    }
    const exe::TableIds ids = exe::makeTableIds(*tables);
    std::size_t entries = 0;
    std::size_t missing = 0;
    std::size_t maps = 0;
    std::size_t backgrounds = 0;
    std::size_t npcMaps = 0;
    std::size_t flag20 = 0;
    std::size_t singlePaletteWithoutFixedTime = 0;
    std::size_t legendCollision = 0;
    std::map<std::size_t, std::size_t> paletteCounts;
    for (std::size_t id = 0; id < tables->mapEntries.size(); ++id) {
        const exe::MapEntry& entry = tables->mapEntries[id];
        if (entry.filename.empty()) {
            continue;
        }
        ++entries;
        flag20 += (entry.flags & 0x20U) != 0 ? 1 : 0;
        const auto folder = std::filesystem::path{mapDir} / std::format("MAP{}", (id / 15) + 1);
        const auto mapFile = readFile(folder / (entry.filename + ".MAP"));
        const auto tfsFile = readFile(folder / (entry.filename + ".TFS"));
        if (!mapFile || !tfsFile) {
            ++missing;
            continue;
        }
        CAPTURE(entry.filename);
        const auto file = map::readMapFile(*mapFile, {.imageCount8 = entry.num8bppImages,
                                                      .imageCount4 = entry.num4bppImages,
                                                      .hasNpcs = (entry.flags & exe::MapEntryFlags::HasNpcs) != 0});
        REQUIRE_MESSAGE(file.has_value(), file.error().message);
        ++maps;
        npcMaps += file->entities.npcs.empty() ? 0 : 1;
        const auto document = map::mapToYaml(*file, {.maps = ids.maps, .digimon = ids.digimon});
        CHECK(dwe::formats::test::checkAgainstSchema(document, "map").empty());
        legendCollision += document.find("collision")->find("encoding")->text() == "legend" ? 1 : 0;

        const auto tfs = map::readTfsFile(*tfsFile);
        REQUIRE_MESSAGE(tfs.has_value(), tfs.error().message);
        const auto images = map::composeBackgrounds(*tfs, file->setup);
        REQUIRE_MESSAGE(images.has_value(), images.error().message);
        backgrounds += images->size();
        ++paletteCounts[tfs->palettes.size()];
        if (tfs->palettes.size() == 1 && (entry.flags & exe::MapEntryFlags::NoTimeOfDay) == 0) {
            ++singlePaletteWithoutFixedTime;
        }
    }
    std::string histogram;
    for (const auto& [palettes, count] : paletteCounts) {
        histogram += std::format(" {}:{}", palettes, count);
    }
    MESSAGE(std::format("{} entries with a file, {} missing on disc, {} maps read ({} with NPCs, {} with a legend "
                        "collision), {} background images, palettes per map{}, {} with flag 0x20, {} single-palette "
                        "maps without the fixed-time flag",
                        entries, missing, maps, npcMaps, legendCollision, backgrounds, histogram, flag20,
                        singlePaletteWithoutFixedTime));
}
