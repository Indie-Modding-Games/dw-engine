#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/content/dev_disc.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/platform/process.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using dwe::assets::DevAssetSources;
using dwe::content::DiscExtraction;
using dwe::platform::ProcessResult;
using dwe::test_support::TempFolder;

namespace {

// A dwx that is never run for real: it records the command and answers as told.
struct FakeDwx {
    ProcessResult answer;
    std::vector<std::vector<std::string>> commands;

    DiscExtraction extraction(const std::filesystem::path& cache) {
        return DiscExtraction{.dwx = "/tools/dwx",
                              .cache = cache,
                              .run = [this](std::span<const std::string> command) -> dwe::core::Result<ProcessResult> {
                                  commands.emplace_back(command.begin(), command.end());
                                  return answer;
                              }};
    }
};

DevAssetSources discAndFolders(std::filesystem::path disc, std::vector<std::filesystem::path> folders) {
    DevAssetSources sources;
    sources.discImage = std::move(disc);
    sources.extractedContent = std::move(folders);
    return sources;
}

void writePack(const TempFolder& folder, const std::filesystem::path& pack, std::string_view id, std::string_view file,
               std::string_view text) {
    folder.write(pack / "pack.yaml", std::format("id: {}\nversion: 1.0.0\nlayer: base\npriority: 0\n", id));
    folder.write(pack / file, text);
}

} // namespace

TEST_CASE("dwx is looked for next to the running executable, then in the build tree") {
    const std::filesystem::path folder = "/build/ci/apps/game";
    const std::string dwx = dwe::platform::executableFileName("dwx");
    CHECK(dwe::content::dwxCandidates(folder) ==
          std::vector<std::filesystem::path>{folder / dwx, std::filesystem::path{"/build/ci/apps/dwx"} / dwx});
}

TEST_CASE("the first dwx that is a file is used, and when none is, the error names every place looked at") {
    const TempFolder temp;
    temp.write("tools/dwx", "");
    const std::vector<std::filesystem::path> candidates{temp.path() / "missing" / "dwx", temp.path() / "tools",
                                                        temp.path() / "tools" / "dwx"};
    const auto found = dwe::content::findDwx(candidates);
    REQUIRE(found.has_value());
    CHECK(*found == temp.path() / "tools" / "dwx");

    const auto none = dwe::content::findDwx(std::span{candidates}.first(2));
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().message.contains("missing"));
    CHECK(none.error().message.contains(dwe::assets::pathText(temp.path() / "tools")));
}

TEST_CASE("the disc's extracted content is the folder dwx extract --cache prints, asked for with no shell between") {
    FakeDwx dwx{.answer = {.exitCode = 0, .output = "/data/dev-cache/ab-v1\n"}, .commands = {}};
    const auto folder = dwe::content::extractedDiscContent("/discs/my disc.cue", dwx.extraction("/data/dev-cache"));
    REQUIRE_MESSAGE(folder.has_value(), folder.error().message);
    CHECK(*folder == std::filesystem::path{"/data/dev-cache/ab-v1"});
    // The path with a space is one argument, as it is.
    CHECK(dwx.commands == std::vector<std::vector<std::string>>{
                              {"/tools/dwx", "extract", "--cache", "/data/dev-cache", "/discs/my disc.cue"}});
}

TEST_CASE("the folder dwx prints is read as UTF-8, whatever its line end") {
    FakeDwx dwx{.answer = {.exitCode = 0, .output = "/datos/cach\xC3\xA9/ab-v1\r\n"}, .commands = {}};
    const auto folder = dwe::content::extractedDiscContent("/disc.cue", dwx.extraction("/datos"));
    REQUIRE_MESSAGE(folder.has_value(), folder.error().message);
    CHECK(*folder == std::filesystem::path{u8"/datos/caché/ab-v1"});
}

TEST_CASE("a dwx that fails, or does not print exactly one folder, is an error") {
    for (const ProcessResult& answer :
         {ProcessResult{.exitCode = 2, .output = ""}, ProcessResult{.exitCode = 0, .output = ""},
          ProcessResult{.exitCode = 0, .output = "/one\n/two\n"}}) {
        FakeDwx dwx{.answer = answer, .commands = {}};
        CHECK_FALSE(dwe::content::extractedDiscContent("/disc.cue", dwx.extraction("/cache")).has_value());
    }
}

TEST_CASE("an error quotes at most the start of what dwx printed, cut at a whole character") {
    // Two lines, the second of two-byte characters with one across the cut: the quote never ends in half of one.
    std::string output = "\n";
    for (int i = 0; i < 300; ++i) {
        output += "\u00f1";
    }
    FakeDwx dwx{.answer = ProcessResult{.exitCode = 0, .output = output}, .commands = {}};
    const auto content = dwe::content::extractedDiscContent("/disc.cue", dwx.extraction("/cache"));
    REQUIRE_FALSE(content.has_value());
    const std::string& message = content.error().message;
    CHECK(message.size() < 300);
    CHECK(message.contains("...\""));
    CHECK_FALSE(message.contains("\xc3..."));
}

TEST_CASE("without a disc image dwx is not run and the dev assets stay as they are") {
    FakeDwx dwx{.answer = {.exitCode = 0, .output = "/cache/ab-v1\n"}, .commands = {}};
    DevAssetSources sources;
    sources.extractedContent = {"/extracted"};
    const auto result = dwe::content::withDiscContent(sources, dwx.extraction("/cache"));
    REQUIRE(result.has_value());
    CHECK(result->extractedContent == std::vector<std::filesystem::path>{"/extracted"});
    CHECK(dwx.commands.empty());
}

TEST_CASE("the disc's content goes under the configured extracted content, and both under the normal packs") {
    const TempFolder temp;
    // The normal content: its own game and base packs.
    writePack(temp, "content/game", "game", "ui/menu.txt", "game menu");
    writePack(temp, "content/base", "base", "data/table.txt", "normal");
    // A configured extracted content folder and the disc's cache entry, which share the extra pack.
    writePack(temp, "configured/extra", "extra", "data/extra.txt", "configured");
    writePack(temp, "cache/ab-v1/base", "base", "data/table.txt", "disc");
    writePack(temp, "cache/ab-v1/extra", "extra", "data/extra.txt", "disc");
    writePack(temp, "cache/ab-v1/disc_only", "disc_only", "data/disc_only.txt", "disc");

    FakeDwx dwx{.answer = {.exitCode = 0, .output = dwe::assets::pathText(temp.path() / "cache" / "ab-v1") + "\n"},
                .commands = {}};
    const auto sources = dwe::content::withDiscContent(
        discAndFolders(temp.path() / "disc.cue", {temp.path() / "configured"}), dwx.extraction(temp.path() / "cache"));
    REQUIRE_MESSAGE(sources.has_value(), sources.error().message);
    CHECK(sources->extractedContent ==
          std::vector<std::filesystem::path>{temp.path() / "configured", temp.path() / "cache" / "ab-v1"});

    const auto content = dwe::content::GameContent::mount(temp.path() / "content", {}, *sources);
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    // The normal base wins over the disc's, the configured extra over the disc's, and the disc fills what is left.
    std::vector<std::filesystem::path> devPacks = (*content)->devPacks();
    std::ranges::sort(devPacks);
    CHECK(devPacks == std::vector<std::filesystem::path>{temp.path() / "cache" / "ab-v1" / "disc_only",
                                                         temp.path() / "configured" / "extra"});
}
