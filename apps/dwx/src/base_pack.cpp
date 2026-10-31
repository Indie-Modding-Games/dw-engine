#include "base_pack.hpp"

#include <dwe/assets/content_check.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "asset_names.hpp"
#include "dwx.hpp"

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
constexpr std::string_view MarkerText =
    "Written by dwx extract. The next extraction empties this folder.\n";
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
  return !extension.empty() && std::ranges::all_of(extension, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
  });
}

// Writes an output: its bytes, or what its stream gives, part by part.
core::Result<void> writeOutput(const std::filesystem::path& file, const OutputFile& output) {
  if (!output.stream) {
    return writeFile(file, output.bytes);
  }
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error) {
    return core::fail(
        std::format("cannot create {}: {}", file.parent_path().string(), error.message()));
  }
  std::ofstream stream{file, std::ios::binary | std::ios::trunc};
  const auto written = output.stream([&](std::span<const std::byte> part) -> core::Result<void> {
    stream.write(reinterpret_cast<const char*>(part.data()),
                 static_cast<std::streamsize>(part.size()));
    if (!stream) {
      return core::fail(std::format("cannot write {}", file.string()));
    }
    return {};
  });
  if (!written) {
    return core::fail(std::format("{}: {}", file.string(), written.error().message));
  }
  stream.close();
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
  YamlNode schemas = YamlNode::mapping();
  for (const auto& rule : names::documentSchemas()) {
    YamlNode ids = YamlNode::sequence();
    for (const std::string_view id : rule.ids) {
      ids.append(quoted(std::string{id}));
    }
    schemas.set(std::string{rule.schema}, std::move(ids));
  }
  manifest.set("schemas", std::move(schemas));
  return core::emitYaml(manifest);
}

// The most threads an extraction uses. Files are independent, so they are hashed and extracted in
// parallel; the results are put together in path order, so the output does not depend on the number
// of threads.
constexpr unsigned MaxThreads = 16;

// Lets several threads read one sector source: reads take turns, and each thread gets its own copy
// of the user data, valid until that thread's next read.
class SharedSectorSource final : public formats::disc::SectorSource {
public:
  explicit SharedSectorSource(formats::disc::SectorSource& source) : source_(&source) {}

  [[nodiscard]] std::uint32_t sectorCount() const override { return source_->sectorCount(); }

  core::Result<formats::disc::SectorData> readSector(std::uint32_t lba) override {
    thread_local std::vector<std::byte> copy;
    const std::scoped_lock lock{mutex_};
    auto sector = source_->readSector(lba);
    if (!sector) {
      return sector;
    }
    copy.assign(sector->userData.begin(), sector->userData.end());
    return formats::disc::SectorData{
        .form = sector->form, .userData = copy, .subheader = sector->subheader};
  }

private:
  formats::disc::SectorSource* source_;
  std::mutex mutex_;
};

// What a worker made of one file.
struct FileWork {
  std::optional<core::Result<core::Sha1Digest>> digest;
  const Extractor* extractor = nullptr;
  std::optional<core::Result<Extraction>> extraction;
  std::vector<core::Sha1Digest> outputDigests; // of each output's bytes, to find duplicates
};

// Hashes every file and runs the extractor that takes it, on several threads.
std::vector<FileWork> extractFiles(const Disc& disc, std::span<const Extractor> registry,
                                   const FileProgress& onFile) {
  SharedSectorSource shared{disc.source()};
  const Disc view{shared, disc.volume(), disc.info(), disc.tables()};
  const auto& files = disc.volume().files;
  std::vector<FileWork> work(files.size());
  std::atomic<std::size_t> next{0};
  std::mutex progressMutex;
  std::size_t done = 0;
  const auto worker = [&] {
    for (std::size_t i = next++; i < files.size(); i = next++) {
      FileWork& item = work[i];
      item.digest = formats::disc::hashIsoFile(shared, files[i]);
      item.extractor = findExtractor(registry, files[i].path);
      if (*item.digest && item.extractor != nullptr) {
        item.extraction = item.extractor->extract(view, files[i]);
        if (*item.extraction) {
          for (const auto& output : (*item.extraction)->outputs) {
            item.outputDigests.push_back(core::sha1(output.bytes));
          }
        }
      }
      if (onFile) {
        const std::scoped_lock lock{progressMutex};
        onFile(++done, files.size());
      }
    }
  };
  const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1U, MaxThreads);
  std::vector<std::thread> threads;
  for (unsigned i = 1; i < count; ++i) {
    threads.emplace_back(worker);
  }
  worker();
  for (auto& thread : threads) {
    thread.join();
  }
  return work;
}

// Groups the outputs with the same extension and bytes, in the order of their first output;
// streamed outputs are left out.
std::vector<std::vector<std::string>> findDuplicates(const std::vector<OutputFile>& outputs,
                                                     const std::vector<core::Sha1Digest>& digests) {
  std::map<std::string, std::vector<std::string>> byContent;
  std::vector<std::string> order;
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    const OutputFile& output = outputs[i];
    if (output.stream) {
      continue;
    }
    const std::string key = std::format("{}:{}", output.extension, core::toHex(digests[i]));
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

core::Result<void> writeFile(const std::filesystem::path& file, std::span<const std::byte> bytes) {
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error) {
    return core::fail(
        std::format("cannot create {}: {}", file.parent_path().string(), error.message()));
  }
  std::ofstream stream{file, std::ios::binary | std::ios::trunc};
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!stream) {
    return core::fail(std::format("cannot write {}", file.string()));
  }
  return {};
}

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

core::Result<ExtractionRun> runExtractors(const Disc& disc, std::span<const Extractor> registry,
                                          const FileProgress& onFile) {
  ExtractionRun run;
  std::set<std::string, std::less<>> ids{std::string{ManifestId}};
  auto work = extractFiles(disc, registry, onFile);
  std::vector<core::Sha1Digest> outputDigests;
  for (std::size_t i = 0; i < work.size(); ++i) {
    const auto& file = disc.volume().files[i];
    FileWork& item = work[i];
    if (!item.digest) {
      return core::fail(std::format("{}: the file was not read", file.path));
    }
    const auto& digest = *item.digest;
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
    if (const Extractor* extractor = item.extractor; extractor != nullptr) {
      report.extractor = extractor->name;
      if (!item.extraction) {
        return core::fail(std::format("{}: extractor {} did not run", file.path, extractor->name));
      }
      auto& extraction = *item.extraction;
      if (!extraction) {
        report.unknowns.push_back(std::format("not extracted: {}", extraction.error().message));
      } else {
        for (auto& output : extraction->outputs) {
          if (!isExtension(output.extension)) {
            return core::fail(std::format("{}: extractor {} gave '{}' the extension '{}'",
                                          file.path, extractor->name, output.id.str(),
                                          output.extension));
          }
          if (output.id.str().starts_with(ToolFolder) || !ids.insert(output.id.str()).second) {
            return core::fail(
                std::format("{}: extractor {} gave the asset id '{}' twice or a reserved "
                            "one",
                            file.path, extractor->name, output.id.str()));
          }
          report.outputs.push_back(outputPath(output));
          run.outputs.push_back(std::move(output));
        }
        outputDigests.insert(outputDigests.end(), item.outputDigests.begin(),
                             item.outputDigests.end());
        report.unknowns = std::move(extraction->unknowns);
        report.notes = std::move(extraction->notes);
      }
      report.status = report.unknowns.empty() ? FileStatus::Extracted : FileStatus::Partial;
    }
    run.files.push_back(std::move(report));
  }
  run.duplicates = findDuplicates(run.outputs, outputDigests);
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

bool writtenByExtract(const std::filesystem::path& folder) {
  std::error_code error;
  return std::filesystem::is_regular_file(folder / MarkerFile, error);
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
  if (!writtenByExtract(folder)) {
    if (force) {
      return {};
    }
    return core::fail(
        std::format("{} is not empty and dwx extract did not write it; choose another folder or "
                    "pass --force to write into it without deleting anything",
                    folder.string()));
  }
  // A previous extraction wrote it: empty it, so files of an older id scheme do not linger.
  // remove_all deletes symbolic links, never what they point to.
  std::vector<std::filesystem::path> entries;
  for (std::filesystem::directory_iterator entry{folder, error};
       !error && entry != std::filesystem::end(entry); entry.increment(error)) {
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

core::Result<void> writeBasePack(const std::filesystem::path& folder, const ExtractionRun& run,
                                 const DiscInfo& info) {
  const auto manifest = manifestYaml();
  if (!manifest) {
    return std::unexpected{manifest.error()};
  }
  const auto coverage = coverageYaml(run, info);
  if (!coverage) {
    return std::unexpected{coverage.error()};
  }
  for (const auto& output : run.outputs) {
    if (auto written = writeOutput(folder / outputPath(output), output); !written) {
      return written;
    }
  }
  for (const auto& [name, text] :
       {std::pair{ManifestFile, std::string_view{*manifest}},
        std::pair{CoverageFile, std::string_view{*coverage}}, std::pair{MarkerFile, MarkerText}}) {
    if (auto written = writeText(folder / name, text); !written) {
      return written;
    }
  }
  return {};
}

core::Result<std::size_t> checkBasePack(const std::filesystem::path& folder,
                                        const ExtractionRun& run) {
  const assets::DiskFileSystem disk;
  const std::array roots = {folder};
  auto vfs = assets::VirtualFileSystem::mount(disk, roots, assets::PackOrigin::Engine);
  if (!vfs) {
    return core::fail(std::format("the base pack does not mount: {}", vfs.error().message));
  }
  for (const auto& output : run.outputs) {
    const assets::AssetSource* source = vfs->resolve(output.id);
    if (source == nullptr) {
      return core::fail(std::format("the asset '{}' does not resolve", output.id.str()));
    }
    if (source->packId != PackId || source->file != folder / outputPath(output)) {
      return core::fail(
          std::format("the asset '{}' resolves to {}", output.id.str(), source->file.string()));
    }
  }
  const std::vector<assets::ContentProblem> problems = assets::checkContent(*vfs);
  if (!problems.empty()) {
    std::string list;
    for (const assets::ContentProblem& problem : problems) {
      list += std::format("\n  {}: {}", problem.file, problem.message);
    }
    return core::fail(
        std::format("the engine cannot open {} of its assets:{}", problems.size(), list));
  }
  return vfs->ids().size();
}

} // namespace dwe::dwx
