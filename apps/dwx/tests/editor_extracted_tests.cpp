#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "editor_documents.hpp"
#include "script_listing_check.hpp"
#include "validate.hpp"
#include "yaml_edits.hpp"

// R4.7 on the user's extracted base pack (DWE_CONTENT): every document the map editor writes comes
// back byte for byte when it is opened and saved, an edit changes only its value, and a mods pack
// made of edited copies passes dwx validate and wins over the base. Nothing of the pack is kept:
// the mods pack lives in a temporary folder.

using namespace dwe::dwx::testing;

namespace {

// What the user's extracted base pack holds, from the US disc.
constexpr std::size_t ExtractedMapCount = 242;
constexpr std::size_t ExtractedListingCount = 222;
constexpr int ListingsWithTextCount = 212;

std::optional<std::filesystem::path> extractedBasePack() {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on extracted content");
    return std::nullopt;
  }
  return *content / BasePackId;
}

std::string readText(const std::filesystem::path& file) {
  std::ifstream stream{file, std::ios::binary};
  REQUIRE_MESSAGE(stream.good(), file.string());
  return std::string{std::istreambuf_iterator<char>{stream}, {}};
}

std::vector<std::filesystem::path> mapDocuments(const std::filesystem::path& base) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator{base / "map"}) {
    const std::filesystem::path data = entry.path() / "data.yaml";
    if (std::filesystem::is_regular_file(data)) {
      files.push_back(data);
    }
  }
  std::ranges::sort(files);
  return files;
}

std::vector<std::filesystem::path> scriptListings(const std::filesystem::path& base) {
  std::vector<std::filesystem::path> files{packFile(base, MapHeadScript, YamlExtension)};
  for (const auto& entry : std::filesystem::directory_iterator{base / "script/dg"}) {
    files.push_back(entry.path());
  }
  std::ranges::sort(files);
  return files;
}

// The folder of the species in a pack.
constexpr std::string_view SpeciesFolder = "digimon";

// The id of a file of the first species of the user's base pack that has a model:
// "digimon/<id>/<leaf>". The ids of the base pack are the user's, so the test finds one instead of
// naming it.
std::string extractedDigimonAsset(const std::filesystem::path& base, std::string_view leaf) {
  const std::filesystem::path folder = base / SpeciesFolder;
  REQUIRE(std::filesystem::is_directory(folder));
  std::vector<std::string> species;
  for (const auto& entry : std::filesystem::directory_iterator{folder}) {
    if (std::filesystem::is_regular_file(entry.path() / std::format("model.{}", GlbExtension))) {
      species.push_back(dwe::assets::pathText(entry.path().filename()));
    }
  }
  REQUIRE_FALSE(species.empty());
  std::ranges::sort(species);
  return std::format("{}/{}/{}", SpeciesFolder, species.front(), leaf);
}

// Writes the mods pack the tools would make from the user's base pack: the first spawn of a map
// moved, the first toilet of the map table removed and the first text of a script rewritten; a
// background and a model copied as they are.
void writeToolPack(const std::filesystem::path& base, const std::filesystem::path& pack) {
  const auto write = [](const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream stream{file, std::ios::binary};
    stream << text;
    REQUIRE_MESSAGE(stream.good(), file.string());
  };
  write(pack / "pack.yaml", manifestText(toolPackManifest()));
  for (const auto& [id, extension] : std::array<std::pair<std::string, std::string_view>, 2>{
           {{editedMapAsset("background_day"), PngExtension},
            {extractedDigimonAsset(base, "model"), GlbExtension}}}) {
    write(packFile(pack, id, extension), readText(packFile(base, id, extension)));
  }
  const std::string mapData = editedMapAsset("data");
  write(packFile(pack, mapData, YamlExtension),
        movedSpawn(readText(packFile(base, mapData, YamlExtension))));
  write(packFile(pack, MapTable, YamlExtension),
        withoutFirstToilet(readText(packFile(base, MapTable, YamlExtension))));
  write(packFile(pack, EditedScript, YamlExtension),
        withFirstTextRewritten(readText(packFile(base, EditedScript, YamlExtension)), "Edited"));
}

} // namespace

TEST_CASE(
    "the user's disc, extracted: every map the editor opens and saves keeps every byte and its edit") {
  const auto base = extractedBasePack();
  if (!base) {
    return;
  }
  const auto files = mapDocuments(*base);
  CHECK(files.size() == ExtractedMapCount);
  for (const std::filesystem::path& file : files) {
    const std::string original = readText(file);
    CHECK_MESSAGE(savedUnchanged(original) == original, file.string());
    const auto map = dwe::assets::parseMapDocument(original);
    REQUIRE_MESSAGE(map.has_value(), file.string(), ": ", errorOf(map));

    const std::string moved = movedSpawn(original);
    const auto edited = dwe::assets::parseMapDocument(moved);
    REQUIRE_MESSAGE(edited.has_value(), file.string(), ": ", errorOf(edited));
    CHECK_MESSAGE(edited->warps[0].spawn.x == map->warps[0].spawn.x + SpawnShift, file.string());
    CHECK_MESSAGE(emitted(withScalar(parsed(moved), firstSpawnX(),
                                     std::to_string(map->warps[0].spawn.x))) == original,
                  file.string());
  }
}

TEST_CASE(
    "the user's disc, extracted: the map table keeps every byte and its edit, and refuses a wrong key") {
  const auto base = extractedBasePack();
  if (!base) {
    return;
  }
  const std::string original = readText(packFile(*base, MapTable, YamlExtension));
  CHECK(savedUnchanged(original) == original);
  REQUIRE_MESSAGE(dwe::assets::parseMapTable(original).has_value(),
                  errorOf(dwe::assets::parseMapTable(original)));
  const auto edited = dwe::assets::parseMapTable(withoutFirstToilet(original));
  REQUIRE_MESSAGE(edited.has_value(), errorOf(edited));
  CHECK_FALSE(edited->maps[0].toilet.has_value());
  checkMapTableRefusesWrongKeys(original);
}

TEST_CASE(
    "the user's disc, extracted: every script listing the editor opens and saves keeps every byte and its edit") {
  const auto base = extractedBasePack();
  if (!base) {
    return;
  }
  const auto files = scriptListings(*base);
  CHECK(files.size() == ExtractedListingCount);
  int edited = 0;
  for (const std::filesystem::path& file : files) {
    const std::string original = readText(file);
    CHECK_MESSAGE(savedUnchanged(original) == original, file.string());
    const dwe::core::YamlNode listing = parsed(original);
    const auto problems = listingProblems(listing);
    CHECK_MESSAGE(problems.empty(), file.string(), ": ", problems.size(), " problems");

    const auto text = firstText(listing);
    if (!text) {
      continue;
    }
    ++edited;
    const std::string rewritten =
        withFirstTextRewritten(original, "Edited \"text\"\nwith two lines");
    CHECK_MESSAGE(listingProblems(parsed(rewritten)).empty(), file.string());
    CHECK_MESSAGE(emitted(withScalar(parsed(rewritten), *text, nodeAt(listing, *text).text())) ==
                      original,
                  file.string());
  }
  CHECK(edited == ListingsWithTextCount);
}

TEST_CASE(
    "the user's disc, extracted: a mods pack of edited copies passes dwx validate and wins over the base") {
  const auto base = extractedBasePack();
  if (!base) {
    return;
  }
  const dwe::test_support::TempFolder folder;
  const std::filesystem::path pack = folder.path() / ToolPackId;
  writeToolPack(*base, pack);

  const std::array<std::filesystem::path, 1> with{*base};
  const auto report =
      dwe::dwx::validatePacks(dwe::assets::DiskFileSystem{}, pack, DWE_SCHEMA_DIR, with);
  REQUIRE_MESSAGE(report.has_value(), errorOf(report));
  CHECK_MESSAGE(report->problems.empty(), problemsOf(*report));

  const dwe::assets::DiskFileSystem disk;
  // The extracted base is the engine's own; the edited copies are a mod.
  const std::array<dwe::assets::PackLocation, 2> locations{
      dwe::assets::PackLocation{.fileSystem = &disk,
                                .root = *base,
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Engine},
      dwe::assets::PackLocation{.fileSystem = &disk,
                                .root = pack,
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Mod}};
  const auto vfs = dwe::assets::VirtualFileSystem::mount(locations);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  for (const std::string& id :
       {editedMapAsset("data"), editedMapAsset("background_day"), std::string{MapTable},
        std::string{EditedScript}, extractedDigimonAsset(*base, "model")}) {
    const auto assetId = dwe::assets::AssetId::parse(id);
    REQUIRE_MESSAGE(assetId.has_value(), id);
    const dwe::assets::AssetSource* source = vfs->resolve(*assetId);
    REQUIRE_MESSAGE(source != nullptr, id);
    CHECK_MESSAGE(source->packId == ToolPackId, id);
  }
  const int originalX =
      firstSpawnXOf(readText(packFile(*base, editedMapAsset("data"), YamlExtension)));
  const auto moved = dwe::assets::loadMapDocument(*vfs, EditedMapId);
  REQUIRE_MESSAGE(moved.has_value(), errorOf(moved));
  CHECK(moved->warps[0].spawn.x == originalX + SpawnShift);
  const auto table = dwe::assets::loadMapTable(*vfs);
  REQUIRE_MESSAGE(table.has_value(), errorOf(table));
  CHECK_FALSE(table->maps[0].toilet.has_value());
}
