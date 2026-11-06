#include "validate.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/content_check.hpp>
#include <dwe/assets/content_id.hpp>
#include <dwe/assets/lua_api.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_patch.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/map_warps.hpp>
#include <dwe/assets/model.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/assets/table_kinds.hpp>
#include <dwe/assets/text_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/json_schema.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include "asset_names.hpp"
#include "cook.hpp"
#include "dwx.hpp"
#include "messages.hpp"

namespace dwe::dwx {

namespace {

using assets::AssetId;
using assets::MapTable;
using assets::VirtualFileSystem;
using core::YamlNode;

constexpr std::array<std::string_view, 2> AnnotationKeywords = {AssetReferenceKeyword,
                                                                EntryReferenceKeyword};
constexpr std::string_view ManifestFile = "pack.yaml";
constexpr std::string_view MetaSuffix = "_meta";
constexpr std::string_view Placeholder = "{}";
constexpr std::string_view SiblingPrefix = "./";
// The keys of a map's warps and of an exit's number there.
constexpr std::string_view WarpsKey = "warps";
constexpr std::string_view WarpExitKey = "exit";

// The line (from 1) of an exit's item in the warps of a map's data or patch, as the parser read it,
// or 0 when there is none.
std::size_t warpLine(const YamlNode& document, std::int32_t exit) {
  const YamlNode* warps = document.isMapping() ? document.find(WarpsKey) : nullptr;
  if (warps == nullptr || !warps->isSequence()) {
    return 0;
  }
  for (const YamlNode& item : warps->items()) {
    const YamlNode* number = item.isMapping() ? item.find(WarpExitKey) : nullptr;
    if (number == nullptr || !number->isScalar()) {
      continue;
    }
    const std::string& text = number->text();
    std::int32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size() && value == exit) {
      return item.line();
    }
  }
  return 0;
}

std::string_view textOf(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The folder of an id with a slash after it ("digimon/species_a/" for digimon/species_a/model), or
// nothing at the root.
std::string folderOf(const AssetId& id) {
  const std::size_t slash = id.str().rfind('/');
  return slash == std::string::npos ? std::string{} : id.str().substr(0, slash + 1);
}

// Whether a file with that extension is the file a reference names with another: the same type, or
// what dwx cook turns that type into. A cooked pack keeps the documents and models as extracted, so
// they still name a sample as .wav and a texture as .png while the files are .opus and .ktx2; the
// engine finds them by id.
bool isFileOfType(std::string_view extension, std::string_view named) {
  if (extension == named) {
    return true;
  }
  const Cooker* cooker = findCooker(cookers(), named);
  return cooker != nullptr && cooker->outputExtension == extension;
}

// An asset a document or a model names: its id and, when it names a file, the file's extension.
struct Reference {
  AssetId id;
  std::string extension;
};

// Turns a reference template and a value into the asset they name (see AssetReferenceKeyword).
core::Result<Reference> referenceOf(std::string_view annotation, std::string_view value,
                                    const AssetId& document) {
  const std::size_t at = annotation.find(Placeholder);
  if (at == std::string_view::npos ||
      annotation.find(Placeholder, at + 1) != std::string_view::npos) {
    return core::fail(std::format("the schema's {} '{}' must have one {}", AssetReferenceKeyword,
                                  annotation, Placeholder));
  }
  // A namespaced content id names its folder per part: map/<pack>/<name>/data
  // (assets::ContentId::assetPath).
  std::string target = std::format("{}{}{}", annotation.substr(0, at),
                                   assets::contentAssetPath(value), annotation.substr(at + 2));
  if (target.starts_with(SiblingPrefix)) {
    target = folderOf(document) + target.substr(SiblingPrefix.size());
  }
  const std::size_t leaf = target.rfind('/');
  const std::size_t dot = target.rfind('.');
  if (dot == std::string::npos || (leaf != std::string::npos && dot < leaf)) {
    auto id = AssetId::parse(target);
    if (!id) {
      return std::unexpected{id.error()};
    }
    return Reference{.id = std::move(*id), .extension = {}};
  }
  auto id = assets::assetIdOfFile(target);
  if (!id) {
    return std::unexpected{id.error()};
  }
  return Reference{.id = std::move(*id), .extension = target.substr(dot + 1)};
}

// The extension of the file that provides an asset, without the dot.
std::string extensionOf(const assets::AssetSource& source) {
  const std::string extension = source.file.extension().string();
  return extension.empty() ? extension : extension.substr(1);
}

// Reads a decimal integer of a document that its schema already found to be one.
std::optional<std::int64_t> integerOf(const YamlNode& node) {
  if (!node.isScalar()) {
    return std::nullopt;
  }
  const std::string& text = node.text();
  std::int64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// A schema of the schema folder, or why it cannot be used.
struct LoadedSchema {
  std::optional<core::JsonSchema> schema;
  std::string error;
};

// The ids of a content table's entries.
using EntryIds = std::set<std::string, std::less<>>;

// A schema rule of a mounted pack.
struct Rule {
  std::string schema;
  std::string pattern;
  std::string manifest; // the pack.yaml that has the rule
};

class Validator {
public:
  Validator(const assets::FileSystem& fileSystem, const VirtualFileSystem& stack,
            std::filesystem::path schemaFolder)
      : fileSystem_(&fileSystem), stack_(&stack), schemaFolder_(std::move(schemaFolder)) {}

  ValidationReport run() {
    report_.packs = stack_->packs().size();
    const std::vector<AssetId> ids = stack_->ids();
    report_.assets = ids.size();
    checkManifests();
    for (const AssetId& id : ids) {
      const assets::AssetSource& source = *stack_->resolve(id);
      const std::string extension = extensionOf(source);
      if (extension == "yaml" && assets::isMapPatchId(id)) {
        // Every pack's patch of a map applies, not only the one on top (map_patch.hpp).
        for (const assets::AssetSource& patch : stack_->sources(id)) {
          checkDocument(id, patch);
        }
      } else if (extension == "yaml") {
        checkDocument(id, source);
      } else if (extension == "glb" || extension == "gltf") {
        checkModelFiles(id, source);
      } else if (extension == "lua") {
        checkLua(id, source);
      }
      checkMetadata(id, source);
      checkContractExists(id, source);
    }
    for (assets::ContentProblem& problem : assets::checkContent(*stack_)) {
      add(problem.file, 0, 0,
          std::format("the engine cannot load {}: {}", problem.id.str(), problem.message));
    }
    for (assets::ContentProblem& problem : assets::checkTranslations(*stack_)) {
      add(problem.file, 0, 0,
          std::format(messages::TranslationProblem, problem.id.str(), problem.message));
    }
    for (assets::SourcesLockProblem& problem : assets::checkSourcesLocks(*stack_)) {
      add(std::move(problem.file), 0, 0, std::move(problem.message));
    }
    std::ranges::sort(report_.problems);
    return std::move(report_);
  }

private:
  void add(std::string file, std::size_t line, std::size_t column, std::string message) {
    report_.problems.push_back(
        {.file = std::move(file), .line = line, .column = column, .message = std::move(message)});
  }

  // The schema of that name, compiled the first time it is asked for; nullptr when it cannot be
  // used, after reporting why against the manifest that asked for it.
  const core::JsonSchema* schema(std::string_view name, const std::string& manifest) {
    auto found = schemas_.find(name);
    if (found == schemas_.end()) {
      auto compiled = compileSchema(name);
      LoadedSchema loaded;
      if (compiled) {
        loaded.schema.emplace(std::move(*compiled));
      } else {
        loaded.error = compiled.error().message;
      }
      found = schemas_.emplace(std::string{name}, std::move(loaded)).first;
    }
    if (!found->second.schema) {
      if (reportedSchemas_.emplace(manifest, std::string{name}).second) {
        add(manifest, 0, 0,
            std::format("schema '{}' cannot be used: {}", name, found->second.error));
      }
      return nullptr;
    }
    return &*found->second.schema;
  }

  [[nodiscard]] core::Result<core::JsonSchema> compileSchema(std::string_view name) const {
    const std::filesystem::path file = schemaFolder_ / std::format("{}.schema.json", name);
    const auto bytes = fileSystem_->readFile(file);
    if (!bytes) {
      return std::unexpected{bytes.error()};
    }
    const auto document = core::parseYaml(textOf(*bytes));
    if (!document) {
      return core::fail(std::format("{}: {}", assets::pathText(file), document.error().message));
    }
    auto compiled = core::JsonSchema::compile(*document, AnnotationKeywords);
    if (!compiled) {
      return core::fail(std::format("{}: {}", assets::pathText(file), compiled.error().message));
    }
    return compiled;
  }

  // Each pack.yaml against pack.schema.json and the engine's Lua API; the rules of every pack apply
  // to the whole stack. The grammar of api is the manifest reader's, so a pack with a malformed one
  // does not even mount.
  void checkManifests() {
    for (const assets::MountedPack& pack : stack_->packs()) {
      const std::string manifest = assets::pathText(pack.root / ManifestFile);
      if (auto api = assets::checkPackApi(pack.manifest); !api) {
        add(manifest, 0, 0, api.error().message);
      }
      if (pack.origin == assets::PackOrigin::Mod) {
        if (auto layer = assets::checkModLayer(pack.manifest); !layer) {
          add(manifest, 0, 0, layer.error().message);
        }
      }
      for (const assets::SchemaRule& rule : pack.manifest.schemas) {
        for (const std::string& pattern : rule.ids) {
          rules_.push_back({.schema = rule.schema, .pattern = pattern, .manifest = manifest});
        }
      }
      const core::JsonSchema* manifestSchema = schema(ManifestSchema, manifest);
      if (manifestSchema == nullptr) {
        continue;
      }
      // The pack's own file system, which may be an archive rather than the disk.
      const auto bytes = pack.fileSystem->readFile(pack.root / ManifestFile);
      if (!bytes) {
        add(manifest, 0, 0, std::format("cannot be read: {}", bytes.error().message));
        continue;
      }
      const auto document = core::parseYaml(textOf(*bytes));
      if (!document) {
        add(manifest, 0, 0, document.error().message);
        continue;
      }
      ++report_.documents;
      for (const core::SchemaProblem& problem : manifestSchema->check(*document).problems) {
        add(manifest, problem.line, problem.column,
            std::format("{}: {}", problem.path, problem.message));
      }
    }
  }

  // A Lua file compiles: Lua reads it as text and nothing runs, in a state with no library.
  void checkLua(const AssetId& id, const assets::AssetSource& source) {
    const auto bytes = stack_->read(id);
    if (!bytes) {
      return; // checkContent reports it
    }
    const std::string_view text = textOf(*bytes);
    const std::unique_ptr<lua_State, decltype(&lua_close)> state{luaL_newstate(), &lua_close};
    if (!state) {
      add(assets::pathText(source.file), 0, 0, "no Lua state to compile it in");
      return;
    }
    const std::string chunk = "=" + id.str();
    if (luaL_loadbufferx(state.get(), text.data(), text.size(), chunk.c_str(), "t") != LUA_OK) {
      const char* message = lua_tostring(state.get(), -1);
      add(assets::pathText(source.file), 0, 0,
          std::format("the Lua does not compile: {}", message == nullptr ? "no message" : message));
    }
  }

  // A YAML document: the one schema its id matches, the references it makes and, for a contract,
  // its model.
  void checkDocument(const AssetId& id, const assets::AssetSource& source) {
    const std::string file = assets::pathText(source.file);
    std::map<std::string, const Rule*> matches;
    for (const Rule& rule : rules_) {
      if (assets::matchesIdPattern(rule.pattern, id)) {
        matches.try_emplace(rule.schema, &rule);
      }
    }
    if (matches.empty()) {
      add(file, 0, 0, "no schema rule of the packs matches this document");
      return;
    }
    if (matches.size() > 1) {
      std::string names;
      for (const auto& [name, rule] : matches) {
        names += std::format("{}{} ({})", names.empty() ? "" : ", ", name, rule->pattern);
      }
      add(file, 0, 0, std::format("the rules of several schemas match this document: {}", names));
      return;
    }
    const auto& [name, rule] = *matches.begin();
    const core::JsonSchema* compiled = schema(name, rule->manifest);
    const auto bytes = stack_->read(source);
    if (compiled == nullptr || !bytes) {
      return;
    }
    const auto document = core::parseYaml(textOf(*bytes));
    if (!document) {
      return; // checkContent reports it
    }
    ++report_.documents;
    const core::SchemaReport checked = compiled->check(*document);
    for (const core::SchemaProblem& problem : checked.problems) {
      add(file, problem.line, problem.column, std::format("{}: {}", problem.path, problem.message));
    }
    for (const core::SchemaAnnotation& annotation : checked.annotations) {
      if (annotation.keyword == EntryReferenceKeyword) {
        checkEntryReference(file, annotation);
        continue;
      }
      const auto reference = referenceOf(annotation.annotation, annotation.value, id);
      if (!reference) {
        add(file, annotation.line, annotation.column,
            std::format("{}: '{}' is not a reference to an asset: {}", annotation.path,
                        annotation.value, reference.error().message));
        continue;
      }
      if (auto missing = checkReference(*reference); !missing.empty()) {
        add(file, annotation.line, annotation.column,
            std::format("{}: '{}' {}", annotation.path, annotation.value, missing));
      }
    }
    if (name == names::ContractSchema && checked.problems.empty()) {
      checkContract(id, file, *document);
    }
    if (name == names::MapSchema) {
      checkWarps(id, file, *document);
    }
  }

  // Where the warps of a map lead (assets/map_warps.hpp): a dead warp, two targets that disagree or
  // one that names no map, in the map as the game loads it, with the packs' patches. A problem of
  // an exit that a patch changed is the patch's. A map that does not load is checkContent's to
  // report.
  void checkWarps(const AssetId& id, const std::string& file, const YamlNode& document) {
    const MapTable* maps = mapTable();
    const std::optional<std::string> mapId = assets::mapIdOfAsset(id);
    if (maps == nullptr || !mapId) {
      return;
    }
    auto map = assets::loadMapDocument(*stack_, *mapId);
    if (!map) {
      return;
    }
    const bool basePack = assets::isBaseMapData(*stack_, *mapId);
    for (const assets::WarpIssue& issue : assets::resolveWarpTargets(*map, *maps, basePack)) {
      std::string message = assets::warpIssueMessage(*mapId, issue);
      if (const assets::MapExitPatch* patch = map->exitPatch(issue.exit)) {
        addPatchProblem(*mapId, patch->pack, issue.exit, std::move(message));
      } else {
        add(file, warpLine(document, issue.exit), 0, std::move(message));
      }
    }
  }

  // A problem of an exit, at the change of a pack's patch that names it.
  void addPatchProblem(std::string_view mapId, std::string_view pack, std::int32_t exit,
                       std::string message) {
    const auto patchId = assets::mapPatchId(mapId);
    const std::span<const assets::AssetSource> sources =
        patchId ? stack_->sources(*patchId) : std::span<const assets::AssetSource>{};
    const auto source = std::ranges::find(sources, pack, &assets::AssetSource::packId);
    if (source == sources.end()) {
      return;
    }
    std::size_t line = 0;
    if (const auto bytes = stack_->read(*source)) {
      if (const auto patch = core::parseYaml(textOf(*bytes))) {
        line = warpLine(*patch, exit);
      }
    }
    add(assets::pathText(source->file), line, 0, std::move(message));
  }

  // The composed map table, read the first time; nullptr when the packs have none or it does not
  // compose.
  const MapTable* mapTable() {
    if (!mapTableRead_) {
      mapTableRead_ = true;
      if (auto table = assets::loadMapTable(*stack_)) {
        mapTable_ = std::move(*table);
      }
    }
    return mapTable_ ? &*mapTable_ : nullptr;
  }

  // An id that must be an entry of a content table.
  void checkEntryReference(const std::string& file, const core::SchemaAnnotation& annotation) {
    ++report_.references;
    const auto kind = std::ranges::find(assets::composedTableKinds(), annotation.annotation,
                                        &assets::TableKind::defaultId);
    if (kind == assets::composedTableKinds().end()) {
      add(file, annotation.line, annotation.column,
          core::formatMessage(messages::EntryReferenceTable, annotation.path,
                              annotation.annotation));
      return;
    }
    const EntryIds* ids = entryIds(*kind);
    if (ids != nullptr && !ids->contains(annotation.value)) {
      add(file, annotation.line, annotation.column,
          core::formatMessage(messages::EntryReferenceUnknown, annotation.path, annotation.value,
                              annotation.annotation));
    }
  }

  // The ids of a content table's entries as the packs compose it, read the first time they are
  // asked for; nullptr when the table cannot be read, which checkContent reports.
  const EntryIds* entryIds(const assets::TableKind& kind) {
    auto found = entryIds_.find(kind.defaultId);
    if (found == entryIds_.end()) {
      found = entryIds_.emplace(std::string{kind.defaultId}, readEntryIds(kind)).first;
    }
    return found->second ? &*found->second : nullptr;
  }

  [[nodiscard]] std::optional<EntryIds> readEntryIds(const assets::TableKind& kind) const {
    const auto id = AssetId::parse(kind.defaultId);
    if (!id || stack_->resolve(*id) == nullptr) {
      return std::nullopt;
    }
    const auto text = assets::readComposedTable(*stack_, *id, *kind.composition);
    const auto table =
        text ? core::parseYaml(*text) : core::Result<YamlNode>{std::unexpected{text.error()}};
    if (!table || !table->isMapping()) {
      return std::nullopt;
    }
    EntryIds ids;
    for (const assets::EntrySection& section : kind.composition->sections) {
      const YamlNode* entries = section.ownsIds ? table->find(section.section) : nullptr;
      if (entries == nullptr || !entries->isSequence()) {
        continue;
      }
      for (const YamlNode& entry : entries->items()) {
        const YamlNode* key = entry.isMapping() ? entry.find(section.entryKey) : nullptr;
        if (key != nullptr && key->isScalar()) {
          ids.insert(key->text());
        }
      }
    }
    return ids;
  }

  // Follows a reference; returns what is wrong with it, or nothing.
  std::string checkReference(const Reference& reference) {
    ++report_.references;
    const assets::AssetSource* target = stack_->resolve(reference.id);
    if (target == nullptr) {
      return std::format("names {}, which no pack has", reference.id.str());
    }
    if (!reference.extension.empty() && !isFileOfType(extensionOf(*target), reference.extension)) {
      return std::format("names a .{} file, but {} is {}", reference.extension, reference.id.str(),
                         assets::pathText(target->file));
    }
    return {};
  }

  // The files a glTF model reads besides its own: images and buffers next to it.
  void checkModelFiles(const AssetId& id, const assets::AssetSource& source) {
    const auto bytes = stack_->read(id);
    if (!bytes) {
      return;
    }
    const auto model = assets::readModel(*bytes);
    if (!model) {
      return; // checkContent reports it
    }
    const std::string file = assets::pathText(source.file);
    for (const std::string& uri : model->externalUris) {
      const auto reference = referenceOf("./{}", uri, id);
      if (!reference || reference->extension.empty()) {
        add(file, 0, 0, std::format("the model uses '{}', which is not a file of the pack", uri));
        continue;
      }
      if (auto missing = checkReference(*reference); !missing.empty()) {
        add(file, 0, 0, std::format("the model uses '{}', which {}", uri, missing));
      }
    }
  }

  // <name>_meta describes <name>, which must be there too.
  void checkMetadata(const AssetId& id, const assets::AssetSource& source) {
    if (!id.str().ends_with(MetaSuffix)) {
      return;
    }
    const auto described =
        AssetId::parse(std::string_view{id.str()}.substr(0, id.str().size() - MetaSuffix.size()));
    if (!described) {
      return;
    }
    ++report_.references;
    if (stack_->resolve(*described) == nullptr) {
      add(assets::pathText(source.file), 0, 0,
          std::format("the metadata of {}, which no pack has", described->str()));
    }
  }

  void checkContractExists(const AssetId& id, const assets::AssetSource& source) {
    if (!assets::matchesIdPattern(names::ContractedModels, id)) {
      return;
    }
    const auto contract = AssetId::parse(folderOf(id) + std::string{names::ContractLeaf});
    if (contract && stack_->resolve(*contract) == nullptr) {
      add(assets::pathText(source.file), 0, 0,
          std::format("the model has no contract: {} is missing", contract->str()));
    }
  }

  // The model a contract names keeps the parents of its joints and has every animation it lists.
  void checkContract(const AssetId& id, const std::string& file, const YamlNode& contract) {
    const YamlNode* modelName = contract.find("model");
    const YamlNode* parentList = contract.find("joint_parents");
    const YamlNode* animations = contract.find("animations");
    if (modelName == nullptr || parentList == nullptr || animations == nullptr) {
      return; // the schema requires them
    }
    const auto reference = referenceOf("./{}", modelName->text(), id);
    if (!reference || stack_->resolve(reference->id) == nullptr) {
      return; // the reference check reports it
    }
    const auto bytes = stack_->read(reference->id);
    if (!bytes) {
      return; // checkContent reports it
    }
    const auto model = assets::readModel(*bytes);
    if (!model) {
      return;
    }
    ++report_.contracts;
    const std::string modelFile = assets::pathText(stack_->resolve(reference->id)->file);
    const auto problem = [&](std::string message) {
      add(modelFile, 0, 0, std::format("breaks its contract {}: {}", file, message));
    };
    const std::span parents = parentList->items();
    if (model->skins.empty()) {
      problem(std::format("it has no skin, the contract asks for {} joints", parents.size()));
    } else if (const auto& joints = model->skins.front(); joints.size() != parents.size()) {
      problem(std::format("its skin has {} joints, the contract asks for {}", joints.size(),
                          parents.size()));
    } else {
      for (std::size_t joint = 0; joint < joints.size(); ++joint) {
        if (const auto parent = integerOf(parents[joint]); parent != joints[joint]) {
          problem(std::format("joint {} has the parent {}, the contract says {}", joint,
                              joints[joint], parents[joint].text()));
          break;
        }
      }
    }
    std::string missing;
    for (const YamlNode& animation : animations->items()) {
      if (std::ranges::find(model->animations, animation.text()) == model->animations.end()) {
        missing += std::format("{}{}", missing.empty() ? "" : ", ", animation.text());
      }
    }
    if (!missing.empty()) {
      problem(std::format("it lacks the animations {}", missing));
    }
  }

  const assets::FileSystem* fileSystem_;
  const VirtualFileSystem* stack_;
  std::filesystem::path schemaFolder_;
  std::vector<Rule> rules_;
  std::map<std::string, LoadedSchema, std::less<>> schemas_;
  std::set<std::pair<std::string, std::string>> reportedSchemas_;
  std::map<std::string, std::optional<EntryIds>, std::less<>> entryIds_;
  bool mapTableRead_ = false;
  std::optional<MapTable> mapTable_;
  ValidationReport report_;
};

} // namespace

core::Result<ValidationReport> validatePacks(const assets::FileSystem& fileSystem,
                                             const std::filesystem::path& folder,
                                             const std::filesystem::path& schemaFolder,
                                             std::span<const std::filesystem::path> withFolders,
                                             assets::PackOrigin origin) {
  std::vector<assets::FoundPack> packs;
  for (const std::filesystem::path& with : withFolders) {
    const auto found = assets::findPacks(fileSystem, with);
    if (!found) {
      return std::unexpected{found.error()};
    }
    if (found->packs.empty()) {
      return core::fail(
          std::format("--with {} has no pack.yaml nor .pak, nor do the folders right under it",
                      assets::pathText(with)));
    }
    packs.insert(packs.end(), found->packs.begin(), found->packs.end());
  }
  const std::size_t withPackCount = packs.size();
  const auto found = assets::findPacks(fileSystem, folder);
  if (!found) {
    return std::unexpected{found.error()};
  }
  if (found->packs.empty()) {
    return core::fail(std::format("{} has no pack.yaml nor .pak, nor do the folders right under it",
                                  assets::pathText(folder)));
  }
  for (const assets::FoundPack& pack : found->packs) {
    // A pack named twice (by --with and by the folder) is mounted once.
    if (std::ranges::none_of(packs, [&](const assets::FoundPack& mounted) {
          return mounted.path.lexically_normal() == pack.path.lexically_normal();
        })) {
      packs.push_back(pack);
    }
  }
  // The archives stay open while the stack is checked. The --with packs are the engine's own, the
  // folder's have the origin asked for.
  auto opened = assets::openPacks(fileSystem, packs, assets::PackOrigin::Engine);
  if (!opened) {
    return std::unexpected{opened.error()};
  }
  for (assets::PackLocation& location : std::span{opened->locations}.subspan(withPackCount)) {
    location.origin = origin;
  }
  const auto stack = VirtualFileSystem::mount(opened->locations);
  if (!stack) {
    return std::unexpected{stack.error()};
  }
  return Validator{fileSystem, *stack, schemaFolder}.run();
}

std::filesystem::path defaultSchemaFolder(const assets::FileSystem& fileSystem,
                                          const std::filesystem::path& folder) {
  std::filesystem::path normal = folder.lexically_normal();
  if (!normal.has_filename()) {
    normal = normal.parent_path();
  }
  const auto found = assets::findPacks(fileSystem, normal);
  const bool isPack =
      found && found->packs.size() == 1 && found->packs.front().path.lexically_normal() == normal;
  if (!isPack) {
    return normal / "schemas";
  }
  // A pack named by a path without a parent, such as ".", has its parent at "..".
  return normal.has_parent_path() ? normal.parent_path() / "schemas"
                                  : (normal / ".." / "schemas").lexically_normal();
}

int validate(const std::filesystem::path& folder,
             const std::optional<std::filesystem::path>& schemaFolder,
             std::span<const std::filesystem::path> withFolders, assets::PackOrigin origin) {
  const assets::DiskFileSystem disk;
  const std::filesystem::path schemas = schemaFolder.value_or(defaultSchemaFolder(disk, folder));
  const auto report = validatePacks(disk, folder, schemas, withFolders, origin);
  if (!report) {
    std::println(stderr, "dwx validate: {}", report.error().message);
    return ExitFailed;
  }
  for (const ValidationProblem& problem : report->problems) {
    if (problem.line == 0) {
      std::println("{}: {}", problem.file, problem.message);
    } else {
      std::println("{}:{}:{}: {}", problem.file, problem.line, problem.column, problem.message);
    }
  }
  std::println(
      "{} packs, {} assets, {} documents checked against the schemas of {}, {} references, {} contracts: "
      "{} problems",
      report->packs, report->assets, report->documents, assets::pathText(schemas),
      report->references, report->contracts, report->problems.size());
  return report->problems.empty() ? ExitOk : ExitUnknown;
}

} // namespace dwe::dwx
