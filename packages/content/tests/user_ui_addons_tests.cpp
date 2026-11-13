#include <dwe/assets/asset_id.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/content/user_ui_addons.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/test_support/zip_writer.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using dwe::assets::AssetId;
using dwe::content::GameContent;
using dwe::content::LeftOutUiAddonArchive;
using dwe::test_support::TempFolder;
using dwe::test_support::zipFile;
using dwe::test_support::ZipFileSpec;
using dwe::test_support::zipFolder;

// A content folder with its own game pack, and the player's data folder next to it.
void writeContent(const TempFolder& folder) {
  folder.write("content/game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\npriority: 0\n");
  folder.write("content/game/ui/menu.txt", "game menu");
}

std::unique_ptr<GameContent> mounted(const TempFolder& folder) {
  auto content = GameContent::mount(folder.path() / "content", {}, {}, folder.path() / "user");
  REQUIRE_MESSAGE(content.has_value(), content.error().message);
  return std::move(*content);
}

std::optional<std::string> readText(const GameContent& content, std::string_view id) {
  auto parsed = AssetId::parse(id);
  REQUIRE(parsed.has_value());
  auto bytes = content.files().read(*parsed);
  if (!bytes) {
    return std::nullopt;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the test files are text.
  return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

bool hasAsset(const GameContent& content, std::string_view id) {
  auto parsed = AssetId::parse(id);
  REQUIRE(parsed.has_value());
  return content.files().resolve(*parsed) != nullptr;
}

// A clock addon's files under a folder prefix ("" for the root).
std::vector<ZipFileSpec> clockFiles(std::string_view prefix, std::string_view id = "clock") {
  return {
      zipFile(std::format("{}addon.yaml", prefix), std::format("id: {}\n", id)),
      zipFile(std::format("{}clock/document.rml", prefix), "<rml/>"),
      zipFile(std::format("{}clock/logic.lua", prefix), "-- clock"),
  };
}

void writeZip(const TempFolder& folder, std::string_view name,
              const std::vector<ZipFileSpec>& files) {
  dwe::test_support::writeZip(folder.path() / "user/ui/addons" / std::string{name}, files);
}

const LeftOutUiAddonArchive* leftOut(const GameContent& content, std::string_view reason) {
  for (const LeftOutUiAddonArchive& archive : content.leftOutUiAddonArchives()) {
    if (archive.reason.contains(reason)) {
      return &archive;
    }
  }
  return nullptr;
}

} // namespace

TEST_CASE("a UI addon .zip with addon.yaml at its root shows as the folder of its id") {
  const TempFolder folder;
  writeContent(folder);
  // Named as a download may be: the id comes from addon.yaml.
  writeZip(folder, "Clock-1.2.ZIP", clockFiles(""));
  const auto content = mounted(folder);
  CHECK(readText(*content, "ui/addons/clock/addon") == std::optional<std::string>{"id: clock\n"});
  CHECK(readText(*content, "ui/addons/clock/clock/document") ==
        std::optional<std::string>{"<rml/>"});
  CHECK(hasAsset(*content, "ui/addons/clock/clock/logic"));
  CHECK(content->leftOutUiAddonArchives().empty());
  // The archive itself is no asset: only the files in it are.
  for (const AssetId& id : content->files().ids()) {
    CHECK_FALSE(id.str().contains("clock-1"));
  }
}

TEST_CASE("a UI addon .zip of one folder shows as the folder of its id, macOS extras left out") {
  const TempFolder folder;
  writeContent(folder);
  // As a source host packs a repository: the folder's name is not the id.
  std::vector<ZipFileSpec> files = clockFiles("clock-main/");
  files.insert(files.begin(), zipFolder("clock-main/"));
  files.push_back(zipFile("__MACOSX/clock-main/._addon.yaml", "fork"));
  files.push_back(zipFile("clock-main/.DS_Store", "finder"));
  writeZip(folder, "clock.zip", files);
  const auto content = mounted(folder);
  CHECK(readText(*content, "ui/addons/clock/addon") == std::optional<std::string>{"id: clock\n"});
  CHECK(hasAsset(*content, "ui/addons/clock/clock/document"));
  CHECK(content->leftOutUiAddonArchives().empty());
}

TEST_CASE("a folder and a .zip of one id: the folder is used and the .zip reported") {
  const TempFolder folder;
  writeContent(folder);
  folder.write("user/ui/addons/clock/addon.yaml", "id: clock\nname: folder\n");
  writeZip(folder, "clock.zip", clockFiles(""));
  // Two archives of one id: the first by name is used.
  writeZip(folder, "a_watch.zip", clockFiles("", "watch"));
  writeZip(folder, "b_watch.zip", clockFiles("", "watch"));
  const auto content = mounted(folder);
  CHECK(readText(*content, "ui/addons/clock/addon") ==
        std::optional<std::string>{"id: clock\nname: folder\n"});
  CHECK_FALSE(hasAsset(*content, "ui/addons/clock/clock/document"));
  CHECK(hasAsset(*content, "ui/addons/watch/clock/document"));
  REQUIRE(content->leftOutUiAddonArchives().size() == 2);
  const LeftOutUiAddonArchive* clock = leftOut(*content, "the folder ui/addons/clock has its id");
  REQUIRE(clock != nullptr);
  CHECK(clock->shadowed);
  CHECK(clock->folder == "ui/addons/clock");
  CHECK(clock->reason.contains("clock.zip"));
  const LeftOutUiAddonArchive* watch = leftOut(*content, "a_watch.zip has its id watch");
  REQUIRE(watch != nullptr);
  CHECK(watch->shadowed);
  CHECK(watch->reason.contains("b_watch.zip"));
}

TEST_CASE("a UI addon .zip that is not safe or not an addon gives none, and the rest still mount") {
  const TempFolder folder;
  writeContent(folder);
  folder.write("user/ui/addons/fine/addon.yaml", "id: fine\n");
  std::vector<ZipFileSpec> escape = clockFiles("", "escape");
  escape.push_back(zipFile("../../evil.lua", "os.exit()"));
  writeZip(folder, "escape.zip", escape);
  folder.write("user/ui/addons/broken.zip", "not an archive");
  std::vector<ZipFileSpec> two = clockFiles("one/", "two");
  two.push_back(zipFile("other/readme.txt", "x"));
  writeZip(folder, "two_folders.zip", two);
  writeZip(folder, "no_id.zip", clockFiles("", "Not An Id"));
  std::vector<ZipFileSpec> upper = clockFiles("", "upper");
  upper.push_back(zipFile("README.TXT", "x"));
  writeZip(folder, "upper.zip", upper);
  const auto content = mounted(folder);
  CHECK(hasAsset(*content, "ui/addons/fine/addon"));
  CHECK_FALSE(hasAsset(*content, "ui/addons/escape/addon"));
  CHECK_FALSE(hasAsset(*content, "ui/addons/upper/addon"));
  CHECK(content->leftOutUiAddonArchives().size() == 5);

  const LeftOutUiAddonArchive* unsafe = leftOut(*content, "not a safe path");
  REQUIRE(unsafe != nullptr);
  // Its addon.yaml is never read: the archive is refused whole first.
  CHECK(unsafe->folder == "ui/addons/escape.zip");
  CHECK_FALSE(unsafe->shadowed);
  CHECK(leftOut(*content, "not a .zip archive") != nullptr);
  CHECK(leftOut(*content, "no addon.yaml at its root or in its only folder") != nullptr);
  CHECK(leftOut(*content, "has no id of lowercase letters") != nullptr);
  const LeftOutUiAddonArchive* badName = leftOut(*content, "README.TXT cannot be an asset");
  REQUIRE(badName != nullptr);
  CHECK(badName->folder == "ui/addons/upper");
}

TEST_CASE("a user data folder with only bad UI addon archives mounts no pack of the player's") {
  const TempFolder folder;
  writeContent(folder);
  folder.write("user/ui/addons/broken.zip", "not an archive");
  const auto content = mounted(folder);
  CHECK(content->files().packs().size() == 1);
  CHECK(content->leftOutUiAddonArchives().size() == 1);
}
