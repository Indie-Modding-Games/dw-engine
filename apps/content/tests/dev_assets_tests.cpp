#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/content/dev_assets.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

using dwe::assets::DevAssetSources;
using dwe::content::loadDevAssets;
using dwe::content::parseDevAssetsConfig;
using dwe::test_support::TempFolder;

namespace {

std::string parseError(std::string_view yaml) {
    const auto parsed = parseDevAssetsConfig(yaml, "config");
    REQUIRE_FALSE(parsed.has_value());
    return parsed.error().message;
}

// A content folder whose base pack the dev assets can name.
void writeExtractedContent(const TempFolder& folder, std::string_view name) {
    folder.write(std::filesystem::path{name} / "base" / "pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
}

} // namespace

TEST_CASE("the dev assets config names each kind of root, relative to its folder") {
    const auto parsed = parseDevAssetsConfig("disc_image: disc/game.cue\n"
                                             "extracted_content: [content-f4, /abs/content-f3]\n"
                                             "decomp_root: ../decomp\n",
                                             "/home/dev/work");
    REQUIRE_MESSAGE(parsed.has_value(), parsed.error().message);
    CHECK(parsed->discImage == std::filesystem::path{"/home/dev/work/disc/game.cue"});
    CHECK(parsed->extractedContent ==
          std::vector<std::filesystem::path>{"/home/dev/work/content-f4", "/abs/content-f3"});
    CHECK(parsed->decompRoot == std::filesystem::path{"/home/dev/decomp"});
}

TEST_CASE("a dev assets config may name only some roots, but not none") {
    const auto parsed = parseDevAssetsConfig("extracted_content: [content]\n", "/work");
    REQUIRE(parsed.has_value());
    CHECK_FALSE(parsed->discImage.has_value());
    CHECK_FALSE(parsed->decompRoot.has_value());
    CHECK(parseError("{}\n").find("names no roots") != std::string::npos);
}

TEST_CASE("a wrong dev assets config is an error") {
    CHECK(parseError("disc: game.cue\n").find("disc") != std::string::npos);
    CHECK(parseError("extracted_content: content\n").find("extracted_content") != std::string::npos);
    CHECK_FALSE(parseError("- a list\n").empty());
}

TEST_CASE("the dev assets come from the first folder with a config, checked") {
    const TempFolder folder;
    writeExtractedContent(folder, "content");
    folder.write("worktree/dev-assets.yaml", "extracted_content: [../content]\n");
    folder.write("user/dev-assets.yaml", "extracted_content: [/nowhere]\n");
    const std::array folders{folder.path() / "worktree", folder.path() / "user"};
    const auto loaded = loadDevAssets(folders);
    REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
    CHECK(loaded->extractedContent == std::vector{(folder.path() / "content").lexically_normal()});
}

TEST_CASE("the dev assets fail without a config or with a root that is not there") {
    const TempFolder folder;
    const std::array none{folder.path()};
    const auto missing = loadDevAssets(none);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.find("--dev-assets") != std::string::npos);
    CHECK(missing.error().message.find("dev-assets.yaml") != std::string::npos);

    folder.write("dev-assets.yaml", "decomp_root: nowhere\n");
    const auto broken = loadDevAssets(none);
    REQUIRE_FALSE(broken.has_value());
    CHECK(broken.error().message.find("decomp root") != std::string::npos);
}
