#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/content/dev_assets.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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
using dwe::content::SkippedMod;
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

// Adds a mod in a folder of mods/ named after it, with a manifest that may be anything.
void addMod(const TempFolder& folder, std::string_view name, std::string_view manifest) {
    folder.write(std::filesystem::path{"mods"} / std::string{name} / "pack.yaml", manifest);
}

std::vector<std::string> packIds(const GameContent& content) {
    std::vector<std::string> ids;
    for (const auto& pack : content.files().packs()) {
        ids.push_back(pack.manifest.id);
    }
    std::ranges::sort(ids);
    return ids;
}

const SkippedMod* skippedFolder(const GameContent& content, std::string_view name) {
    const auto found =
        std::ranges::find_if(content.skippedMods(), [&](const SkippedMod& mod) { return mod.path.filename() == name; });
    return found == content.skippedMods().end() ? nullptr : &*found;
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

TEST_CASE("mods written for the engine's Lua API are mounted") {
    const TempFolder folder;
    writeContent(folder);
    addMod(folder, "my_mod", "id: my_mod\nversion: 1.0.0\nlayer: mods\napi: \"1.x\"\n");
    addMod(folder, "exact_mod", "id: exact_mod\nversion: 1.0.0\nlayer: mods\napi: \"1.0\"\n");
    const std::array mods{folder.path() / "mods"};
    auto content = GameContent::mount(folder.path() / "content", mods);
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    CHECK(packIds(**content) == std::vector<std::string>{"base", "exact_mod", "game", "my_mod"});
    CHECK((*content)->skippedMods().empty());
}

TEST_CASE("a mod that cannot mount is left out with its reason, and the others still mount") {
    const TempFolder folder;
    writeContent(folder);
    addMod(folder, "good_mod", "id: good_mod\nversion: 1.0.0\nlayer: mods\napi: \"1.x\"\n");
    addMod(folder, "other_api", "id: other_api\nversion: 1.0.0\nlayer: mods\napi: \"2.x\"\n");
    addMod(folder, "newer_minor", "id: newer_minor\nversion: 1.0.0\nlayer: mods\napi: \"1.2\"\n");
    addMod(folder, "broken", "id: Broken\nversion: 1.0.0\nlayer: mods\n");
    addMod(folder, "lonely",
           "id: lonely\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: missing\n    min_version: 1.0.0\n");
    // Found before the mod it needs, so it mounts in a later round.
    addMod(folder, "a_needs_good",
           "id: a_needs_good\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: good_mod\n    min_version: 1.0.0\n");
    const std::array mods{folder.path() / "mods"};
    auto content = GameContent::mount(folder.path() / "content", mods);
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    CHECK(packIds(**content) == std::vector<std::string>{"a_needs_good", "base", "game", "good_mod"});
    REQUIRE((*content)->skippedMods().size() == 4);
    const SkippedMod* otherApi = skippedFolder(**content, "other_api");
    REQUIRE(otherApi != nullptr);
    CHECK(otherApi->reason == "pack 'other_api': the scripts need the Lua API 2.x, and this engine has 1.1.0");
    const SkippedMod* newerMinor = skippedFolder(**content, "newer_minor");
    REQUIRE(newerMinor != nullptr);
    CHECK(newerMinor->reason == "pack 'newer_minor': the scripts need the Lua API 1.2, and this engine has 1.1.0");
    const SkippedMod* broken = skippedFolder(**content, "broken");
    REQUIRE(broken != nullptr);
    CHECK(broken->reason.contains("pack id 'Broken' may only use a-z, 0-9 and '_'"));
    const SkippedMod* lonely = skippedFolder(**content, "lonely");
    REQUIRE(lonely != nullptr);
    CHECK(lonely->reason.contains("missing"));
}

TEST_CASE("a mods folder that does not exist is left out") {
    const TempFolder folder;
    writeContent(folder);
    const std::array mods{folder.path() / "no_such_folder"};
    auto content = GameContent::mount(folder.path() / "content", mods);
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    REQUIRE((*content)->skippedMods().size() == 1);
    CHECK((*content)->skippedMods()[0].path == mods[0]);
}

TEST_CASE("with dev assets, a mod whose manifest is broken is left out like without them") {
    const TempFolder folder;
    writeContent(folder);
    writePack(folder, "extracted/extra", "extra", "base");
    addMod(folder, "broken", "id: Broken\nversion: 1.0.0\nlayer: mods\n");
    addMod(folder, "good_mod", "id: good_mod\nversion: 1.0.0\nlayer: mods\n");
    const std::array mods{folder.path() / "mods"};
    auto content = GameContent::mount(folder.path() / "content", mods, extractedOnly({folder.path() / "extracted"}));
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    CHECK(packIds(**content) == std::vector<std::string>{"base", "extra", "game", "good_mod"});
    REQUIRE((*content)->skippedMods().size() == 1);
    CHECK((*content)->skippedMods()[0].path.filename() == "broken");
}

TEST_CASE("the dev packs that fill the game's gaps count when the game's packs are checked") {
    // The game pack needs the base pack, which only the dev assets have.
    const TempFolder folder;
    folder.write("content/game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\ndependencies:\n  - id: base\n"
                                           "    min_version: 1.0.0\n");
    writePack(folder, "extracted/base", "base", "base");
    addMod(folder, "my_mod", "id: my_mod\nversion: 1.0.0\nlayer: mods\napi: \"1.x\"\n");
    const std::array mods{folder.path() / "mods"};
    auto content = GameContent::mount(folder.path() / "content", mods, extractedOnly({folder.path() / "extracted"}));
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    CHECK(packIds(**content) == std::vector<std::string>{"base", "game", "my_mod"});
    CHECK((*content)->skippedMods().empty());
}

TEST_CASE("with dev assets, a mod with a dev pack's id is skipped and the dev pack stays, so the game still starts") {
    // The game pack needs the base pack, which only the dev assets have: a mod with its id used to take its place in
    // the game's stack, and the game did not start.
    const TempFolder folder;
    folder.write("content/game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\ndependencies:\n  - id: base\n"
                                           "    min_version: 1.0.0\n");
    writePack(folder, "extracted/base", "base", "base");
    folder.write("extracted/base/data/only_dev.txt", "only dev");
    addMod(folder, "my_base", "id: base\nversion: 1.0.0\nlayer: mods\napi: \"1.x\"\n");
    addMod(folder, "engine_layer_base", "id: base\nversion: 1.0.0\nlayer: base\napi: \"1.x\"\n");
    const std::array mods{folder.path() / "mods"};
    auto content = GameContent::mount(folder.path() / "content", mods, extractedOnly({folder.path() / "extracted"}));
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    CHECK(packIds(**content) == std::vector<std::string>{"base", "game"});
    CHECK((*content)->devPacks() == std::vector{folder.path() / "extracted" / "base"});
    CHECK(packOf(**content, "data/only_dev") == "base");
    CHECK((*content)->skippedMods().size() == 2);
    const SkippedMod* taken = skippedFolder(**content, "my_base");
    REQUIRE(taken != nullptr);
    CHECK(taken->reason.contains("base"));
    // Its layer is checked on the manifest it mounts with, so it is left out first as the id is taken.
    const SkippedMod* engineLayer = skippedFolder(**content, "engine_layer_base");
    REQUIRE(engineLayer != nullptr);
    CHECK(engineLayer->reason.contains("'base' is mounted twice"));
}

TEST_CASE("a mod that declares an engine layer is skipped, and only the engine's own packs are of engine origin") {
    const TempFolder folder;
    writeContent(folder);
    writePack(folder, "extracted/extra", "extra", "base");
    addMod(folder, "fake_base", "id: fake_base\nversion: 1.0.0\nlayer: base\n");
    addMod(folder, "fake_remaster", "id: fake_remaster\nversion: 1.0.0\nlayer: remaster\n");
    addMod(folder, "good_mod", "id: good_mod\nversion: 1.0.0\nlayer: mods\n");
    // A folder given with --mods is a mods folder like any other.
    writePack(folder, "elsewhere/path_mod", "path_mod", "user");
    const std::array mods{folder.path() / "mods", folder.path() / "elsewhere"};
    auto content = GameContent::mount(folder.path() / "content", mods, extractedOnly({folder.path() / "extracted"}));
    REQUIRE_MESSAGE(content.has_value(), content.error().message);
    CHECK(packIds(**content) == std::vector<std::string>{"base", "extra", "game", "good_mod", "path_mod"});
    REQUIRE((*content)->skippedMods().size() == 2);
    const SkippedMod* fakeBase = skippedFolder(**content, "fake_base");
    REQUIRE(fakeBase != nullptr);
    CHECK(fakeBase->reason ==
          "pack 'fake_base' is a mod and declares the base layer, which only the engine's own packs have: a mod's "
          "layer is mods or user");
    const SkippedMod* fakeRemaster = skippedFolder(**content, "fake_remaster");
    REQUIRE(fakeRemaster != nullptr);
    CHECK(fakeRemaster->reason.contains("declares the remaster layer"));
    for (const auto& pack : (*content)->files().packs()) {
        CAPTURE(pack.manifest.id);
        const bool engine = pack.manifest.id == "base" || pack.manifest.id == "game" || pack.manifest.id == "extra";
        CHECK(pack.origin == (engine ? dwe::assets::PackOrigin::Engine : dwe::assets::PackOrigin::Mod));
    }
}
