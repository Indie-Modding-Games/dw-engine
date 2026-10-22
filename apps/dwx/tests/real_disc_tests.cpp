// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one.

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/text_table.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "base_pack.hpp"
#include "disc_description.hpp"
#include "schema_check.hpp"
#include "validate.hpp"

using namespace dwe::dwx;

namespace {

// The message of a failed result. doctest builds a message even when the check passes, so it cannot call error().
template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("the user's disc: the base pack the extractor writes mounts, validates and opens") {
    const std::optional<std::filesystem::path> mountedPath = dwe::test_support::userDisc();
    if (!mountedPath) {
        MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
        return;
    }
    const std::filesystem::path& path = *mountedPath;
    auto image = dwe::formats::disc::DiscImage::open(path);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));
    const auto volume = dwe::formats::disc::readIsoVolume(*image);
    REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));
    const DiscDescription description = describeDisc(*image, *volume);
    REQUIRE(description.warnings.empty());
    REQUIRE(description.tables.has_value());
    const DiscInfo& info = description.info;
    const Disc disc{*image, *volume, info, description.tables ? &*description.tables : nullptr};

    const auto run = runExtractors(disc, extractors());
    REQUIRE_MESSAGE(run.has_value(), errorOf(run));
    const CoverageSummary summary = summarize(*run);
    CHECK(summary.files > 500);
    CHECK(summary.extracted + summary.partial >= 50);
    // Every file of the disc has an extractor.
    CHECK(summary.pending == 0);
    const auto has = [&run](std::string_view prefix) {
        return std::ranges::any_of(run->outputs,
                                   [prefix](const OutputFile& output) { return output.id.str().starts_with(prefix); });
    };
    const auto taken = [&run](std::string_view extractor) {
        return std::ranges::count(run->files, extractor, &FileReport::extractor);
    };
    CHECK(taken("tim_pack") == 2);
    CHECK(taken("stacked_tim") == 2);
    CHECK(taken("tim_file") > 10);
    CHECK(taken("executable") == 1);
    CHECK(taken("boot_config") == 1);
    // Every map file but one has a MAP_ENTRIES record; the one without gives its options itself. Background palettes
    // past the third are never shown, so every map and background is extracted.
    CHECK(taken("map") > 200);
    CHECK(taken("map_background") == taken("map"));
    CHECK(std::ranges::none_of(run->files, [](const FileReport& report) {
        return report.extractor.starts_with("map") && report.status != FileStatus::Extracted;
    }));
    CHECK(has("map/mgen17/data"));
    // The new-game script sets the partner's starting stats (formats::script::findNewGameStats).
    CHECK(has("data/new_game_stats"));
    CHECK(taken("arena_collision") == 2);
    // The seven sound files, every bank and sequence understood.
    CHECK(taken("sound_bank") == 7);
    for (const auto& report : run->files) {
        if (report.extractor == "sound_bank") {
            CHECK_MESSAGE(report.status == FileStatus::Extracted, report.file.path);
        }
    }
    const auto statusOf = [&run](std::string_view path) {
        const auto found =
            std::ranges::find(run->files, path, [](const FileReport& report) { return report.file.path; });
        REQUIRE(found != run->files.end());
        return found->status;
    };
    // DG.SCN's script 0 is a copy of MAPHEAD.SCN, so the map header alone is story/000.
    const auto pack =
        std::ranges::find(run->files, "SCN/DG.SCN", [](const FileReport& report) { return report.file.path; });
    REQUIRE(pack != run->files.end());
    CHECK(std::ranges::any_of(
        pack->notes, [](const std::string& note) { return note.contains("script 0 is a copy of SCN/MAPHEAD.SCN"); }));
    CHECK(std::ranges::none_of(pack->unknowns,
                               [](const std::string& unknown) { return unknown.contains("is not a copy"); }));
    CHECK(has("story/000"));
    CHECK(has("story/001"));
    CHECK(taken("padding") == 1);
    CHECK(statusOf("MMM.DAT") == FileStatus::Extracted);
    CHECK(taken("movie") == 4);
    CHECK(statusOf("MOVIE/OP1.STR") == FileStatus::Extracted);
    // Every overlay is understood, SHOP_REL.BIN through its data map: its tables match the executable's copies but for
    // one row of characters.
    CHECK(taken("overlay") == 16);
    CHECK(std::ranges::all_of(run->files, [](const FileReport& report) {
        return report.extractor != "overlay" || report.status == FileStatus::Extracted;
    }));
    const auto shop =
        std::ranges::find(run->files, "SHOP_REL.BIN", [](const FileReport& report) { return report.file.path; });
    REQUIRE(shop != run->files.end());
    CHECK(std::ranges::count_if(shop->notes, [](const std::string& note) {
              return note.contains("same bytes as the executable's copy");
          }) == 8);
    CHECK(std::ranges::count_if(shop->notes,
                                [](const std::string& note) { return note.contains("1 of its bytes differ"); }) == 1);
    for (const std::string_view document : {"data/fishing", "data/stadium", "data/versus", "data/training"}) {
        CHECK(has(document));
    }
    // A new game spawns on a step cell of its first map that runs the opening at once (map.c: initializeMap, tamer.c:
    // checkMapInteraction): data/new_game_story keeps where it goes and what it sets.
    const auto story = std::ranges::find(run->outputs, "data/new_game_story",
                                         [](const OutputFile& output) { return output.id.str(); });
    REQUIRE(story != run->outputs.end());
    const std::string_view storyYaml{reinterpret_cast<const char*>(story->bytes.data()), story->bytes.size()};
    CHECK_MESSAGE(storyYaml.contains("opening:\n  exit: 9\n  section: 54\n  to:\n    map: topn01\n    exit: 9\n"
                                     "  sets:\n  - 54\n  - 55\n  - 75\n"),
                  storyYaml);
    // Every table document matches its schema.
    for (const OutputFile& output : run->outputs) {
        const std::string id = output.id.str();
        if (!id.starts_with("data/")) {
            continue;
        }
        const auto parsed = dwe::core::parseYaml(
            std::string_view{reinterpret_cast<const char*>(output.bytes.data()), output.bytes.size()});
        REQUIRE_MESSAGE(parsed.has_value(), id);
        CHECK_MESSAGE(dwe::formats::test::checkAgainstSchema(*parsed, id.substr(5)).empty(), id);
    }
    // The original's text is a flat table of "<block>.<slot>" keys (docs/formats/textos.md).
    const auto mainText =
        std::ranges::find(run->outputs, "text/en/main", [](const OutputFile& output) { return output.id.str(); });
    REQUIRE(mainText != run->outputs.end());
    const std::string_view mainYaml{reinterpret_cast<const char*>(mainText->bytes.data()), mainText->bytes.size()};
    const auto parsedText = dwe::core::parseYaml(mainYaml);
    REQUIRE(parsedText.has_value());
    CHECK(parsedText->find("strings") == nullptr);
    const auto mainTable = dwe::assets::parseTextTable(
        {.kind = dwe::assets::TextTableKind::Game, .language = "en", .name = "main"}, mainYaml);
    REQUIRE_MESSAGE(mainTable.has_value(), errorOf(mainTable));
    CHECK(mainTable->contains("main.map_names.0001"));
    CHECK(mainTable->contains("main.move_names.0000"));
    CHECK(has("data/digimon"));
    CHECK(has("data/maps"));
    CHECK(has("map/"));
    CHECK(has("music/033/font"));
    CHECK(has("sound/vball/070/bank"));
    // With the tables, Digimon textures are named, never numbered.
    CHECK(has("digimon/"));
    CHECK_FALSE(has("digimon/0"));
    CHECK_FALSE(has("digimon/1"));
    // ALLTIM's 180 Digimon textures and CARD.ALL's 66 cards, with their metadata.
    CHECK(summary.outputs > (180 + 66) * 2);

    const dwe::test_support::TempFolder temp;
    REQUIRE(prepareOutputFolder(temp.path() / "base", false).has_value());
    const auto written = writeBasePack(temp.path() / "base", *run, info);
    REQUIRE_MESSAGE(written.has_value(), errorOf(written));
    const auto assets = checkBasePack(temp.path() / "base", *run);
    REQUIRE_MESSAGE(assets.has_value(), errorOf(assets));
    CHECK(*assets == summary.outputs + 1);

    // dwx validate: every document follows its schema, every reference and contract holds and every asset opens with
    // the engine's readers.
    const dwe::assets::DiskFileSystem disk;
    const auto report = validatePacks(disk, temp.path() / "base", DWE_SCHEMA_DIR, {}, dwe::assets::PackOrigin::Engine);
    REQUIRE_MESSAGE(report.has_value(), errorOf(report));
    for (const ValidationProblem& problem : report->problems) {
        MESSAGE(std::format("{}:{}:{}: {}", problem.file, problem.line, problem.column, problem.message));
    }
    CHECK(report->problems.empty());
    CHECK(report->assets == *assets);
    CHECK(report->contracts > 170);
}
