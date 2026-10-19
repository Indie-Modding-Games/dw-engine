#include "base_pack.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace dwe::dwx {

namespace {

using core::YamlNode;

constexpr std::string_view PackId = "base";
constexpr std::string_view PackVersion = "0.1.0";
constexpr std::string_view ManifestFile = "pack.yaml";
// The tool's own files go in its folder of the pack, so their ids cannot clash with an asset's.
constexpr std::string_view ToolFolder = "dwx/";
constexpr std::string_view CoverageFile = "dwx/coverage.yaml";
// Hidden, so the virtual file system does not take it for an asset.
constexpr std::string_view MarkerFile = ".dwx-extract";
constexpr std::string_view MarkerText = "Written by dwx extract. The next extraction empties this folder.\n";
// The manifest takes this asset id.
constexpr std::string_view ManifestId = "pack";

YamlNode quoted(std::string text) {
    return YamlNode::scalar(std::move(text), YamlNode::ScalarStyle::Quoted);
}

YamlNode number(std::uint64_t value) {
    return YamlNode::scalar(std::to_string(value));
}

YamlNode quotedList(std::span<const std::string> texts) {
    YamlNode list = YamlNode::sequence();
    for (const auto& text : texts) {
        list.append(quoted(text));
    }
    return list;
}

bool isExtension(std::string_view extension) {
    return !extension.empty() &&
           std::ranges::all_of(extension, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
}

core::Result<void> writeFile(const std::filesystem::path& file, std::span<const std::byte> bytes) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error) {
        return core::fail(std::format("cannot create {}: {}", file.parent_path().string(), error.message()));
    }
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        return core::fail(std::format("cannot write {}", file.string()));
    }
    return {};
}

core::Result<void> writeText(const std::filesystem::path& file, std::string_view text) {
    return writeFile(file, std::as_bytes(std::span{text}));
}

core::Result<std::string> manifestYaml() {
    YamlNode manifest = YamlNode::mapping();
    manifest.set("id", YamlNode::scalar(std::string{PackId}));
    manifest.set("version", YamlNode::scalar(std::string{PackVersion}));
    manifest.set("layer", YamlNode::scalar("base"));
    manifest.set("name", quoted("Base"));
    manifest.set("description", quoted("Extracted from the user's disc by dwx extract"));
    return core::emitYaml(manifest);
}

// Groups the outputs with the same extension and bytes, in the order of their first output.
std::vector<std::vector<std::string>> findDuplicates(const std::vector<OutputFile>& outputs) {
    std::map<std::string, std::vector<std::string>> byContent;
    std::vector<std::string> order;
    for (const auto& output : outputs) {
        const std::string key = std::format("{}:{}", output.extension, core::toHex(core::sha1(output.bytes)));
        auto& paths = byContent[key];
        if (paths.empty()) {
            order.push_back(key);
        }
        paths.push_back(outputPath(output));
    }
    std::vector<std::vector<std::string>> groups;
    for (const auto& key : order) {
        if (auto& paths = byContent[key]; paths.size() > 1) {
            groups.push_back(std::move(paths));
        }
    }
    return groups;
}

} // namespace

std::string_view fileStatusName(FileStatus status) {
    switch (status) {
    case FileStatus::Extracted:
        return "extracted";
    case FileStatus::Partial:
        return "partial";
    case FileStatus::Pending:
        return "pending";
    }
    return "unknown";
}

core::Result<ExtractionRun> runExtractors(const Disc& disc, std::span<const Extractor> registry) {
    ExtractionRun run;
    std::set<std::string, std::less<>> ids{std::string{ManifestId}};
    for (const auto& file : disc.volume().files) {
        const auto digest = formats::disc::hashIsoFile(disc.source(), file);
        if (!digest) {
            return core::fail(std::format("{}: {}", file.path, digest.error().message));
        }
        FileReport report{.file = file,
                          .sha1 = core::toHex(*digest),
                          .extractor = {},
                          .status = FileStatus::Pending,
                          .outputs = {},
                          .unknowns = {},
                          .notes = {}};
        if (const Extractor* extractor = findExtractor(registry, file.path); extractor != nullptr) {
            report.extractor = extractor->name;
            auto extraction = extractor->extract(disc, file);
            if (!extraction) {
                report.unknowns.push_back(std::format("not extracted: {}", extraction.error().message));
            } else {
                for (auto& output : extraction->outputs) {
                    if (!isExtension(output.extension)) {
                        return core::fail(std::format("{}: extractor {} gave '{}' the extension '{}'", file.path,
                                                      extractor->name, output.id.str(), output.extension));
                    }
                    if (output.id.str().starts_with(ToolFolder) || !ids.insert(output.id.str()).second) {
                        return core::fail(std::format("{}: extractor {} gave the asset id '{}' twice or a reserved "
                                                      "one",
                                                      file.path, extractor->name, output.id.str()));
                    }
                    report.outputs.push_back(outputPath(output));
                    run.outputs.push_back(std::move(output));
                }
                report.unknowns = std::move(extraction->unknowns);
                report.notes = std::move(extraction->notes);
            }
            report.status = report.unknowns.empty() ? FileStatus::Extracted : FileStatus::Partial;
        }
        run.files.push_back(std::move(report));
    }
    run.duplicates = findDuplicates(run.outputs);
    return run;
}

CoverageSummary summarize(const ExtractionRun& run) {
    CoverageSummary summary;
    summary.files = run.files.size();
    summary.outputs = run.outputs.size();
    for (const auto& group : run.duplicates) {
        summary.duplicates += group.size() - 1;
    }
    for (const auto& report : run.files) {
        summary.bytes += report.file.size;
        switch (report.status) {
        case FileStatus::Extracted:
            ++summary.extracted;
            summary.bytesCovered += report.file.size;
            break;
        case FileStatus::Partial:
            ++summary.partial;
            summary.bytesCovered += report.file.size;
            break;
        case FileStatus::Pending:
            ++summary.pending;
            break;
        }
    }
    return summary;
}

core::Result<std::string> coverageYaml(const ExtractionRun& run, const DiscInfo& info) {
    YamlNode disc = YamlNode::mapping();
    disc.set("volume", quoted(info.volumeId));
    disc.set("executable", quoted(info.executablePath));
    disc.set("executable_sha1", quoted(info.executableSha1));
    disc.set("version", quoted(info.version));

    const CoverageSummary summary = summarize(run);
    YamlNode totals = YamlNode::mapping();
    totals.set("files", number(summary.files));
    totals.set("extracted", number(summary.extracted));
    totals.set("partial", number(summary.partial));
    totals.set("pending", number(summary.pending));
    totals.set("bytes", number(summary.bytes));
    totals.set("bytes_covered", number(summary.bytesCovered));
    totals.set("outputs", number(summary.outputs));
    totals.set("duplicate_outputs", number(summary.duplicates));

    YamlNode files = YamlNode::sequence();
    for (const auto& report : run.files) {
        YamlNode entry = YamlNode::mapping();
        entry.set("path", quoted(report.file.path));
        entry.set("size", number(report.file.size));
        entry.set("sha1", quoted(report.sha1));
        entry.set("status", YamlNode::scalar(std::string{fileStatusName(report.status)}));
        if (!report.extractor.empty()) {
            entry.set("extractor", quoted(std::string{report.extractor}));
        }
        if (!report.outputs.empty()) {
            entry.set("outputs", quotedList(report.outputs));
        }
        if (!report.unknowns.empty()) {
            entry.set("unknowns", quotedList(report.unknowns));
        }
        if (!report.notes.empty()) {
            entry.set("notes", quotedList(report.notes));
        }
        files.append(std::move(entry));
    }

    YamlNode root = YamlNode::mapping();
    root.set("disc", std::move(disc));
    root.set("summary", std::move(totals));
    root.set("files", std::move(files));
    YamlNode duplicates = YamlNode::sequence();
    for (const auto& group : run.duplicates) {
        duplicates.append(quotedList(group));
    }
    root.set("duplicates", std::move(duplicates));
    return core::emitYaml(root);
}

core::Result<void> prepareOutputFolder(const std::filesystem::path& folder, bool force) {
    std::error_code error;
    if (!std::filesystem::exists(folder, error)) {
        std::filesystem::create_directories(folder, error);
        if (error) {
            return core::fail(std::format("cannot create {}: {}", folder.string(), error.message()));
        }
        return {};
    }
    if (!std::filesystem::is_directory(folder, error)) {
        return core::fail(std::format("{} is not a folder", folder.string()));
    }
    if (std::filesystem::is_empty(folder, error)) {
        return {};
    }
    if (!std::filesystem::exists(folder / MarkerFile, error)) {
        if (force) {
            return {};
        }
        return core::fail(std::format("{} is not empty and dwx extract did not write it; choose another folder or "
                                      "pass --force to write into it without deleting anything",
                                      folder.string()));
    }
    // A previous extraction wrote it: empty it, so files of an older id scheme do not linger. remove_all deletes
    // symbolic links, never what they point to.
    std::vector<std::filesystem::path> entries;
    for (std::filesystem::directory_iterator entry{folder, error}; !error && entry != std::filesystem::end(entry);
         entry.increment(error)) {
        entries.push_back(entry->path());
    }
    if (error) {
        return core::fail(std::format("cannot list {}: {}", folder.string(), error.message()));
    }
    for (const auto& entry : entries) {
        std::filesystem::remove_all(entry, error);
        if (error) {
            return core::fail(std::format("cannot remove {}: {}", entry.string(), error.message()));
        }
    }
    return {};
}

core::Result<void> writeBasePack(const std::filesystem::path& folder, const ExtractionRun& run, const DiscInfo& info) {
    const auto manifest = manifestYaml();
    if (!manifest) {
        return std::unexpected{manifest.error()};
    }
    const auto coverage = coverageYaml(run, info);
    if (!coverage) {
        return std::unexpected{coverage.error()};
    }
    for (const auto& output : run.outputs) {
        if (auto written = writeFile(folder / outputPath(output), output.bytes); !written) {
            return written;
        }
    }
    for (const auto& [name, text] :
         {std::pair{ManifestFile, std::string_view{*manifest}}, std::pair{CoverageFile, std::string_view{*coverage}},
          std::pair{MarkerFile, MarkerText}}) {
        if (auto written = writeText(folder / name, text); !written) {
            return written;
        }
    }
    return {};
}

core::Result<std::size_t> checkBasePack(const std::filesystem::path& folder, const ExtractionRun& run) {
    const assets::DiskFileSystem disk;
    const std::array roots = {folder};
    auto vfs = assets::VirtualFileSystem::mount(disk, roots);
    if (!vfs) {
        return core::fail(std::format("the base pack does not mount: {}", vfs.error().message));
    }
    for (const auto& output : run.outputs) {
        const assets::AssetSource* source = vfs->resolve(output.id);
        if (source == nullptr) {
            return core::fail(std::format("the asset '{}' does not resolve", output.id.str()));
        }
        if (source->packId != PackId || source->file != folder / outputPath(output)) {
            return core::fail(std::format("the asset '{}' resolves to {}", output.id.str(), source->file.string()));
        }
    }
    return vfs->ids().size();
}

} // namespace dwe::dwx
