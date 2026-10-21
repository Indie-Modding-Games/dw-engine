#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/core/error.hpp>

#include <compare>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// dwx validate: checks a stack of packs the way the engine will use it, before it does.
namespace dwe::dwx {

// The annotation keyword of content/schemas that makes a string a reference to an asset. Its value is a template
// where {} stands for the string. A result whose last segment has an extension is a file of the pack, relative to the
// document's folder when the template starts with "./" ("./{}" for "bank.sf2") and to the pack's root otherwise;
// any other result is an asset id ("digimon/{}/model").
inline constexpr std::string_view AssetReferenceKeyword = "assetReference";

// The annotation keyword of content/schemas that makes a string the id of an entry of a content table: its value is
// the table's asset id ("data/items"). The id must be an entry of the table as the packs compose it.
inline constexpr std::string_view EntryReferenceKeyword = "entryReference";

// The schema pack.yaml follows.
inline constexpr std::string_view ManifestSchema = "pack";

// Something wrong with a file of a pack. line and column are 1-based places in a YAML document, or 0.
struct ValidationProblem {
    std::string file;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string message;

    friend auto operator<=>(const ValidationProblem&, const ValidationProblem&) = default;
};

// What dwx validate checked and what it found.
struct ValidationReport {
    std::size_t packs = 0;
    std::size_t assets = 0;
    std::size_t documents = 0;               // YAML documents checked against their schema, pack.yaml files included
    std::size_t references = 0;              // references to assets followed, from documents and from glTF models
    std::size_t contracts = 0;               // models checked against their contract
    std::vector<ValidationProblem> problems; // sorted by file, place and message
};

// Checks the packs at a folder, which findPacks takes: a pack or a folder of packs, folders and .pak archives. Every
// pack.yaml must follow pack.schema.json and every YAML document the schema its id matches in the schemas rules of the
// mounted manifests, exactly one, from schemaFolder. The references the schemas mark, the files glTF models use and the
// asset that each <name>_meta describes must be in the stack; a reference that names a file may name the type the
// file was cooked from (.wav for .opus, .png for .ktx2). Each model of names::ContractedModels needs a contract,
// and the model a contract names has its skeleton and its animations. Every asset must open with the engine's readers
// (assets::checkContent), the ones the game loads it with: a .glb as a model, with its skin, weights and clip extras,
// map/<id>/data and data/maps with the strict readers of the map and of the map table; a problem there names the
// asset id and why the engine refuses it.
// Fails only when there are no packs or they cannot be mounted; anything else is a problem of the report. The packs at
// each of withFolders (a pack or a folder of packs) are mounted too, as the engine's own, so a pack that needs another
// one, such as the game pack on the base pack, can be checked; they are checked with the rest. The packs at the folder
// have the given origin, a mod by default (deny by default): as mods they must declare a mod layer
// (assets::checkModLayer) and have none of the base's rights, as the game loads them from a mods folder. The game's
// own packs, such as the base pack, are checked with PackOrigin::Engine.
core::Result<ValidationReport> validatePacks(const assets::FileSystem& fileSystem, const std::filesystem::path& folder,
                                             const std::filesystem::path& schemaFolder,
                                             std::span<const std::filesystem::path> withFolders = {},
                                             assets::PackOrigin origin = assets::PackOrigin::Mod);

// Where the schemas of the packs at a folder are by default: a schemas folder next to the pack, or inside the
// folder of packs ("content/base" and "content" both give "content/schemas").
std::filesystem::path defaultSchemaFolder(const assets::FileSystem& fileSystem, const std::filesystem::path& folder);

// dwx validate: prints each problem as "file:line:column: message" and a summary. Returns ExitOk when there are
// none, ExitUnknown when there are and ExitFailed when the packs cannot be checked.
int validate(const std::filesystem::path& folder, const std::optional<std::filesystem::path>& schemaFolder,
             std::span<const std::filesystem::path> withFolders = {},
             assets::PackOrigin origin = assets::PackOrigin::Mod);

} // namespace dwe::dwx
