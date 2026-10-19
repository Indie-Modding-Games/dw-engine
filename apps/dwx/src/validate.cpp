#include "validate.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/content_check.hpp>
#include <dwe/assets/model.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <system_error>
#include <utility>

#include "asset_names.hpp"
#include "dwx.hpp"

namespace dwe::dwx {

namespace {

using assets::AssetId;
using assets::VirtualFileSystem;
using core::YamlNode;

constexpr std::array<std::string_view, 1> AnnotationKeywords = {AssetReferenceKeyword};
constexpr std::string_view ManifestFile = "pack.yaml";
constexpr std::string_view MetaSuffix = "_meta";
constexpr std::string_view Placeholder = "{}";
constexpr std::string_view SiblingPrefix = "./";

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The folder of an id with a slash after it ("digimon/agum/" for digimon/agum/model), or nothing at the root.
std::string folderOf(const AssetId& id) {
    const std::size_t slash = id.str().rfind('/');
    return slash == std::string::npos ? std::string{} : id.str().substr(0, slash + 1);
}

// An asset a document or a model names: its id and, when it names a file, the file's extension.
struct Reference {
    AssetId id;
    std::string extension;
};

// Turns a reference template and a value into the asset they name (see AssetReferenceKeyword).
core::Result<Reference> referenceOf(std::string_view annotation, std::string_view value, const AssetId& document) {
    const std::size_t at = annotation.find(Placeholder);
    if (at == std::string_view::npos || annotation.find(Placeholder, at + 1) != std::string_view::npos) {
        return core::fail(
            std::format("the schema's {} '{}' must have one {}", AssetReferenceKeyword, annotation, Placeholder));
    }
    std::string target = std::format("{}{}{}", annotation.substr(0, at), value, annotation.substr(at + 2));
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

// A schema rule of a mounted pack.
struct Rule {
    std::string schema;
    std::string pattern;
    std::string manifest; // the pack.yaml that has the rule
};

class Validator {
public:
    Validator(const assets::FileSystem& fileSystem, const VirtualFileSystem& stack, std::filesystem::path schemaFolder)
        : fileSystem_(&fileSystem), stack_(&stack), schemaFolder_(std::move(schemaFolder)) {}

    ValidationReport run() {
        report_.packs = stack_->packs().size();
        const std::vector<AssetId> ids = stack_->ids();
        report_.assets = ids.size();
        checkManifests();
        for (const AssetId& id : ids) {
            const assets::AssetSource& source = *stack_->resolve(id);
            const std::string extension = extensionOf(source);
            if (extension == "yaml") {
                checkDocument(id, source);
            } else if (extension == "glb" || extension == "gltf") {
                checkModelFiles(id, source);
            }
            checkMetadata(id, source);
            checkContractExists(id, source);
        }
        for (assets::ContentProblem& problem : assets::checkContent(*stack_)) {
            add(problem.file, 0, 0, std::move(problem.message));
        }
        std::ranges::sort(report_.problems);
        return std::move(report_);
    }

private:
    void add(std::string file, std::size_t line, std::size_t column, std::string message) {
        report_.problems.push_back(
            {.file = std::move(file), .line = line, .column = column, .message = std::move(message)});
    }

    // The schema of that name, compiled the first time it is asked for; nullptr when it cannot be used, after
    // reporting why against the manifest that asked for it.
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
                add(manifest, 0, 0, std::format("schema '{}' cannot be used: {}", name, found->second.error));
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

    // Each pack.yaml against pack.schema.json; the rules of every pack apply to the whole stack.
    void checkManifests() {
        for (const assets::MountedPack& pack : stack_->packs()) {
            const std::string manifest = assets::pathText(pack.root / ManifestFile);
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
                add(manifest, problem.line, problem.column, std::format("{}: {}", problem.path, problem.message));
            }
        }
    }

    // A YAML document: the one schema its id matches, the references it makes and, for a contract, its model.
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
        const auto bytes = stack_->read(id);
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
            const auto reference = referenceOf(annotation.annotation, annotation.value, id);
            if (!reference) {
                add(file, annotation.line, annotation.column,
                    std::format("{}: '{}' is not a reference to an asset: {}", annotation.path, annotation.value,
                                reference.error().message));
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
    }

    // Follows a reference; returns what is wrong with it, or nothing.
    std::string checkReference(const Reference& reference) {
        ++report_.references;
        const assets::AssetSource* target = stack_->resolve(reference.id);
        if (target == nullptr) {
            return std::format("names {}, which no pack has", reference.id.str());
        }
        if (!reference.extension.empty() && extensionOf(*target) != reference.extension) {
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
            problem(std::format("its skin has {} joints, the contract asks for {}", joints.size(), parents.size()));
        } else {
            for (std::size_t joint = 0; joint < joints.size(); ++joint) {
                if (const auto parent = integerOf(parents[joint]); parent != joints[joint]) {
                    problem(std::format("joint {} has the parent {}, the contract says {}", joint, joints[joint],
                                        parents[joint].text()));
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
    ValidationReport report_;
};

} // namespace

core::Result<ValidationReport> validatePacks(const assets::FileSystem& fileSystem, const std::filesystem::path& folder,
                                             const std::filesystem::path& schemaFolder) {
    const auto found = assets::findPacks(fileSystem, folder);
    if (!found) {
        return std::unexpected{found.error()};
    }
    if (found->packs.empty()) {
        return core::fail(
            std::format("{} has no pack.yaml nor .pak, nor do the folders right under it", assets::pathText(folder)));
    }
    // The archives stay open while the stack is checked.
    const auto opened = assets::openPacks(fileSystem, found->packs);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    const auto stack = VirtualFileSystem::mount(opened->locations);
    if (!stack) {
        return std::unexpected{stack.error()};
    }
    return Validator{fileSystem, *stack, schemaFolder}.run();
}

std::filesystem::path defaultSchemaFolder(const assets::FileSystem& fileSystem, const std::filesystem::path& folder) {
    std::filesystem::path normal = folder.lexically_normal();
    if (!normal.has_filename()) {
        normal = normal.parent_path();
    }
    const auto found = assets::findPacks(fileSystem, normal);
    const bool isPack = found && found->packs.size() == 1 && found->packs.front().path.lexically_normal() == normal;
    if (!isPack) {
        return normal / "schemas";
    }
    // A pack named by a path without a parent, such as ".", has its parent at "..".
    return normal.has_parent_path() ? normal.parent_path() / "schemas" : (normal / ".." / "schemas").lexically_normal();
}

int validate(const std::filesystem::path& folder, const std::optional<std::filesystem::path>& schemaFolder) {
    const assets::DiskFileSystem disk;
    const std::filesystem::path schemas = schemaFolder.value_or(defaultSchemaFolder(disk, folder));
    const auto report = validatePacks(disk, folder, schemas);
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
    std::println("{} packs, {} assets, {} documents checked against the schemas of {}, {} references, {} contracts: "
                 "{} problems",
                 report->packs, report->assets, report->documents, assets::pathText(schemas), report->references,
                 report->contracts, report->problems.size());
    return report->problems.empty() ? ExitOk : ExitUnknown;
}

} // namespace dwe::dwx
