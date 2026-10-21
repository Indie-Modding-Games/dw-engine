#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/content/dev_assets.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using dwe::assets::AssetId;
using dwe::assets::DevAssetSources;
using dwe::content::GameContent;
using dwe::test_support::TempFolder;

namespace {

AssetId idOf(std::string_view text) {
    auto id = AssetId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

void writePack(const TempFolder& folder, const std::filesystem::path& pack, std::string_view id, std::string_view layer,
               int priority = 0) {
    folder.write(pack / "pack.yaml",
                 std::format("id: {}\nversion: 1.0.0\nlayer: {}\npriority: {}\n", id, layer, priority));
}

std::string fileText(const std::filesystem::path& file) {
    std::ifstream stream{file, std::ios::binary};
    REQUIRE_MESSAGE(stream.good(), file.string());
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

// A content folder with its own game pack, so the team's packs of the source tree stay out of the tests.
void writeContent(const TempFolder& folder) {
    writePack(folder, "content/game", "game", "base");
    folder.write("content/game/ui/menu.txt", "game menu");
    writePack(folder, "content/base", "base", "base");
    folder.write("content/base/data/table.txt", "content table");
}

DevAssetSources extractedOnly(std::vector<std::filesystem::path> folders) {
    DevAssetSources sources;
    sources.extractedContent = std::move(folders);
    return sources;
}

std::unique_ptr<GameContent> mounted(const TempFolder& folder, const DevAssetSources& devAssets) {
    auto content = GameContent::mount(folder.path() / "content", {}, devAssets);
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    return std::move(*content);
}

std::optional<std::string> packOf(const GameContent& content, std::string_view id) {
    const auto* source = content.files().resolve(idOf(id));
    return source != nullptr ? std::optional{source->packId} : std::nullopt;
}

} // namespace

TEST_CASE("without dev assets the stack has the normal packs only") {
    const TempFolder folder;
    writeContent(folder);
    writePack(folder, "extracted/extra", "extra", "base");
    const auto content = mounted(folder, {});
    CHECK(content->devPacks().empty());
    CHECK(content->files().packs().size() == 2);
}

TEST_CASE("the dev assets only fill what the normal packs lack, under all of them and the mods") {
    const TempFolder folder;
    writeContent(folder);
    writePack(folder, "mods/story", "story", "mods");
    folder.write("mods/story/data/story.txt", "mods story");
    writePack(folder, "extracted/base", "base", "base");
    folder.write("extracted/base/data/table.txt", "extracted table");
    // A dev pack that asks for the mods layer and a high priority still goes under every normal pack.
    writePack(folder, "extracted/extra", "extra", "mods", 99);
    folder.write("extracted/extra/data/table.txt", "extra table");
    folder.write("extracted/extra/data/story.txt", "extra story");
    folder.write("extracted/extra/data/only_extra.txt", "only extra");
    const std::array mods{folder.path() / "mods"};
    auto content = GameContent::mount(folder.path() / "content", mods, extractedOnly({folder.path() / "extracted"}));
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    CHECK((*content)->devPacks() == std::vector{folder.path() / "extracted" / "extra"});
    CHECK(packOf(**content, "data/table") == "base");
    CHECK(packOf(**content, "data/story") == "story");
    CHECK(packOf(**content, "data/only_extra") == "extra");
    CHECK(packOf(**content, "ui/menu") == "game");
}

TEST_CASE("a dev pack with a normal pack's id is left out, whatever its folder is called") {
    const TempFolder folder;
    writeContent(folder);
    writePack(folder, "extracted/base_old", "base", "base");
    const auto content = mounted(folder, extractedOnly({folder.path() / "extracted"}));
    CHECK(content->devPacks().empty());
    CHECK(content->files().packs().size() == 2);
}

TEST_CASE("GameContent takes a pack id from the earlier of two extracted content folders") {
    const TempFolder folder;
    writeContent(folder);
    writePack(folder, "newer/story", "story", "mods");
    folder.write("newer/story/data/story.txt", "newer");
    writePack(folder, "older/story", "story", "mods");
    folder.write("older/story/data/story.txt", "older");
    const auto content = mounted(folder, extractedOnly({folder.path() / "newer", folder.path() / "older"}));
    CHECK(content->devPacks() == std::vector{folder.path() / "newer" / "story"});
}

TEST_CASE("the game pack's dev assets banner shows the banner key, in English and Spanish") {
    const std::filesystem::path pack{DWE_GAME_PACK};
    CHECK(fileText(pack / "ui/dev_assets/document.rml")
              .contains(std::format("text-key=\"{}\"", dwe::content::DevAssetsBannerKey)));
    // The key is <table>.<entry>: the entry is in the table of each language.
    const std::string_view key = dwe::content::DevAssetsBannerKey;
    const std::size_t dot = key.find('.');
    REQUIRE(dot != std::string_view::npos);
    const std::string entry = std::format("\n{}:", key.substr(dot + 1));
    for (const std::string_view language : {"en", "es"}) {
        const std::filesystem::path table =
            pack / "localization" / language / std::format("{}.yaml", key.substr(0, dot));
        CHECK_MESSAGE(fileText(table).contains(entry), table.string());
    }
}

TEST_CASE("the user's disc, extracted: the dev assets mount the extracted content under the team's packs") {
    const std::optional<std::filesystem::path> extracted = dwe::test_support::userContent();
    if (!extracted) {
        MESSAGE("DWE_CONTENT is not set: skipping the dev assets on the extracted content");
        return;
    }
    // A content folder with no packs of its own: the team's packs come from the source tree, the base from the dev
    // assets, which is how a developer plays without packaging anything.
    const TempFolder folder;
    folder.write("content/.keep", "");
    const auto content = GameContent::mount(folder.path() / "content", {}, extractedOnly({*extracted}));
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    CHECK((*content)->devPacks().size() == 1);
    CHECK(packOf(**content, "data/digimon") == "base");
    CHECK(packOf(**content, "data/maps") == "base");

    // The same folder as the content and as dev assets: the normal pack wins and nothing comes from the dev assets.
    const auto twice = GameContent::mount(*extracted, {}, extractedOnly({*extracted}));
    REQUIRE_MESSAGE(twice.has_value(), twice.error().message);
    CHECK((*twice)->devPacks().empty());
}
