#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

using dwe::assets::DevAssetSources;
using dwe::assets::DevSource;
using dwe::assets::DevSourceKind;
using dwe::assets::pathText;
using dwe::assets::validateDevAssetSources;
using dwe::test_support::TempFolder;

namespace {

std::string validationError(const DevAssetSources& sources) {
    const auto valid = validateDevAssetSources(sources);
    REQUIRE_FALSE(valid.has_value());
    return valid.error().message;
}

DevAssetSources discOnly(const std::filesystem::path& disc) {
    DevAssetSources sources;
    sources.discImage = disc;
    return sources;
}

DevAssetSources extractedOnly(const std::filesystem::path& folder) {
    DevAssetSources sources;
    sources.extractedContent.push_back(folder);
    return sources;
}

DevAssetSources decompOnly(const std::filesystem::path& root) {
    DevAssetSources sources;
    sources.decompRoot = root;
    return sources;
}

void writeDecompLayout(const TempFolder& folder) {
    folder.write("decomp/include/dw/script.h", "");
    folder.write("decomp/src/main/script_interp.c", "");
}

} // namespace

TEST_CASE("the dev assets are off by default") {
    const DevAssetSources sources;
    CHECK(sources.empty());
    CHECK(sources.list().empty());
    CHECK(validateDevAssetSources(sources).has_value());
}

TEST_CASE("the dev assets list their roots by kind, in a fixed order") {
    const DevAssetSources sources{
        .discImage = "disc.cue", .extractedContent = {"content-a", "content-b"}, .decompRoot = "decomp"};
    CHECK_FALSE(sources.empty());
    CHECK(sources.list() == std::vector<DevSource>{{.kind = DevSourceKind::DiscImage, .root = "disc.cue"},
                                                   {.kind = DevSourceKind::ExtractedContent, .root = "content-a"},
                                                   {.kind = DevSourceKind::ExtractedContent, .root = "content-b"},
                                                   {.kind = DevSourceKind::DecompRoot, .root = "decomp"}});
}

TEST_CASE("valid dev assets roots pass the check") {
    const TempFolder folder;
    folder.write("disc.cue", "FILE");
    folder.write("content/base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
    writeDecompLayout(folder);
    const DevAssetSources sources{.discImage = folder.path() / "disc.cue",
                                  .extractedContent = {folder.path() / "content"},
                                  .decompRoot = folder.path() / "decomp"};
    const auto valid = validateDevAssetSources(sources);
    CHECK_MESSAGE(valid.has_value(), (valid ? std::string{} : valid.error().message));
}

TEST_CASE("a missing dev assets root is an error that names it") {
    const TempFolder folder;
    const std::filesystem::path missing = folder.path() / "nowhere";
    const std::string disc = validationError(discOnly(missing));
    CHECK(disc.find("disc image") != std::string::npos);
    CHECK(disc.find(pathText(missing)) != std::string::npos);
    CHECK(validationError(extractedOnly(missing)).find("extracted content folder") != std::string::npos);
    CHECK(validationError(decompOnly(missing)).find("decomp root") != std::string::npos);
}

TEST_CASE("a dev assets root of the wrong kind is an error") {
    const TempFolder folder;
    folder.write("empty/.keep", "");
    folder.write("file.txt", "");
    CHECK(validationError(discOnly(folder.path())).find("is not a file") != std::string::npos);
    CHECK(validationError(extractedOnly(folder.path() / "file.txt")).find("is not a folder") != std::string::npos);
    CHECK(validationError(extractedOnly(folder.path() / "empty")).find("has no packs") != std::string::npos);
    CHECK(validationError(decompOnly(folder.path() / "empty")).find("include/dw") != std::string::npos);
}
