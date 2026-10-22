#include "cook.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <print>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "audio_cooker.hpp"
#include "dwx.hpp"
#include "texture_cooker.hpp"

namespace dwe::dwx {

namespace {

using core::YamlNode;

constexpr std::string_view ManifestName = "pack.yaml";
// Hidden, so the virtual file system does not take it for an asset. It marks the folder as written by dwx cook.
constexpr std::string_view CacheFile = ".dwx-cook";
// The cache key of files copied without a cooker.
constexpr std::string_view CopyKey = "copy";

constexpr std::uint32_t GlbMagic = 0x46546C67; // "glTF"
constexpr std::uint32_t GlbVersion = 2;
constexpr std::size_t GlbHeaderSize = 12;

std::uint32_t u32At(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
    }
    return value;
}

// The game loads binary glTF as dwx extract writes it; cooking only checks the header so a broken file fails here
// rather than at load time.
core::Result<std::vector<std::byte>> checkGlb(std::span<const std::byte> source, const CookOptions& /*options*/) {
    if (source.size() < GlbHeaderSize || u32At(source, 0) != GlbMagic) {
        return core::fail("not a binary glTF file");
    }
    if (u32At(source, 4) != GlbVersion) {
        return core::fail(std::format("binary glTF version {}, not {}", u32At(source, 4), GlbVersion));
    }
    if (u32At(source, 8) != source.size()) {
        return core::fail(
            std::format("the binary glTF header says {} bytes, but the file has {}", u32At(source, 8), source.size()));
    }
    return std::vector<std::byte>{source.begin(), source.end()};
}

constexpr std::array Registry = {
    Cooker{.name = "texture",
           .version = 3,
           .sourceExtension = "png",
           .outputExtension = "ktx2",
           .plan = planTexture,
           .cook = cookTexture},
    Cooker{.name = "glb",
           .version = 1,
           .sourceExtension = "glb",
           .outputExtension = "glb",
           .plan = nullptr,
           .cook = checkGlb},
    Cooker{.name = "audio",
           .version = 1,
           .sourceExtension = "wav",
           .outputExtension = "opus",
           .plan = nullptr,
           .cook = cookAudio},
};

// The folder of the movies, which are copied as dwx extract writes them (Y4M video and WAV sound) while the cooked
// video format is not decided. Their sound goes with the video frames and movie.yaml describes it as extracted, so it
// is not cooked like the sounds the game plays on their own.
constexpr std::string_view MovieFolder = "movie/";

// The id suffix and type of the YAML file that describes an asset, as dwx extract writes it (asset_names: metadataOf).
constexpr std::string_view SidecarSuffix = "_meta";
constexpr std::string_view SidecarExtension = ".yaml";

// What the cache knows about one output file: the source and cooker it came from and its own digest.
struct CacheRecord {
    std::string source;
    std::string cooker;
    std::string sha1;
};

// By output path inside the pack.
using Cache = std::map<std::string, CacheRecord, std::less<>>;

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The cache key of how an asset is made: copied, or the cooker with the options that change its bytes.
std::string cookerKey(const Cooker* cooker, const CookOptions& options) {
    if (cooker == nullptr || options.passThrough) {
        return std::string{CopyKey};
    }
    return std::format("{}/{}{}", cooker->name, cooker->version,
                       options.colorSpace == ColorSpace::Linear ? "/linear" : "");
}

// Reads and parses the sidecar of an asset, if the pack has one.
core::Result<std::optional<YamlNode>> readSidecar(const assets::FileSystem& fileSystem,
                                                  const std::filesystem::path& pack,
                                                  const std::map<std::string, std::string, std::less<>>& pathsById,
                                                  const assets::AssetId& id) {
    const auto found = pathsById.find(id.str() + std::string{SidecarSuffix});
    if (found == pathsById.end() || !found->second.ends_with(SidecarExtension)) {
        return std::nullopt;
    }
    const auto bytes = fileSystem.readFile(pack / found->second);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    auto sidecar = core::parseYaml(textOf(*bytes));
    if (!sidecar) {
        return core::fail(std::format("{}: {}", (pack / found->second).string(), sidecar.error().message));
    }
    return std::optional{std::move(*sidecar)};
}

// Reads the cache of an earlier cook. One that does not parse counts as empty: everything cooks again.
Cache readCache(const std::filesystem::path& file) {
    Cache cache;
    const auto bytes = assets::DiskFileSystem{}.readFile(file);
    if (!bytes) {
        return cache;
    }
    const auto root = core::parseYaml(textOf(*bytes));
    const YamlNode* outputs = root && root->isMapping() ? root->find("outputs") : nullptr;
    if (outputs == nullptr || !outputs->isSequence()) {
        return cache;
    }
    const auto field = [](const YamlNode& item, std::string_view key) -> std::optional<std::string> {
        const YamlNode* value = item.find(key);
        return value != nullptr && value->isScalar() ? std::optional{value->text()} : std::nullopt;
    };
    for (const YamlNode& item : outputs->items()) {
        if (!item.isMapping()) {
            continue;
        }
        auto path = field(item, "path");
        auto source = field(item, "source");
        auto cooker = field(item, "cooker");
        auto sha1 = field(item, "sha1");
        if (path && source && cooker && sha1) {
            cache.insert_or_assign(
                std::move(*path),
                CacheRecord{.source = std::move(*source), .cooker = std::move(*cooker), .sha1 = std::move(*sha1)});
        }
    }
    return cache;
}

core::Result<void> writeCache(const std::filesystem::path& file, const Cache& cache) {
    const auto quoted = [](const std::string& text) { return YamlNode::scalar(text, YamlNode::ScalarStyle::Quoted); };
    YamlNode outputs = YamlNode::sequence();
    for (const auto& [path, record] : cache) {
        YamlNode item = YamlNode::mapping();
        item.set("path", quoted(path));
        item.set("source", quoted(record.source));
        item.set("cooker", quoted(record.cooker));
        item.set("sha1", quoted(record.sha1));
        outputs.append(std::move(item));
    }
    YamlNode root = YamlNode::mapping();
    root.set("outputs", std::move(outputs));
    const auto text = core::emitYaml(root);
    if (!text) {
        return std::unexpected{text.error()};
    }
    return writeFile(file, std::as_bytes(std::span{*text}));
}

// Returns the digest of an output file if an earlier cook wrote it from the same source with the same cooker and it
// has not changed since.
std::optional<std::string> keptDigest(const Cache& cache, const std::string& outputPath, const CacheRecord& record,
                                      const std::filesystem::path& output) {
    const auto earlier = cache.find(outputPath);
    if (earlier == cache.end() || earlier->second.source != record.source || earlier->second.cooker != record.cooker) {
        return std::nullopt;
    }
    const auto digest = hashFile(output);
    if (!digest || core::toHex(*digest) != earlier->second.sha1) {
        return std::nullopt;
    }
    return earlier->second.sha1;
}

// Makes the output folder ready and returns the cache of the cook that wrote it, if any.
core::Result<Cache> prepareOutputFolder(const std::filesystem::path& pack, const std::filesystem::path& out) {
    std::error_code error;
    const std::filesystem::path packPath = std::filesystem::weakly_canonical(pack, error);
    const std::filesystem::path outPath = std::filesystem::weakly_canonical(out, error);
    if (error) {
        return core::fail(std::format("cannot resolve {}: {}", out.string(), error.message()));
    }
    if (isWithin(outPath, packPath) || isWithin(packPath, outPath)) {
        return core::fail(
            std::format("the output folder {} and the pack {} must not hold each other", out.string(), pack.string()));
    }
    if (std::filesystem::exists(out, error) && !std::filesystem::is_directory(out, error)) {
        return core::fail(std::format("{} is not a folder", out.string()));
    }
    if (std::filesystem::exists(out / CacheFile, error)) {
        return readCache(out / CacheFile);
    }
    if (std::filesystem::exists(out, error) && !std::filesystem::is_empty(out, error)) {
        return core::fail(
            std::format("{} is not empty and dwx cook did not write it; choose another folder", out.string()));
    }
    // Marked at once, so a cook that stops halfway leaves a folder the next cook takes over.
    if (auto marked = writeCache(out / CacheFile, {}); !marked) {
        return std::unexpected{marked.error()};
    }
    return Cache{};
}

// Removes the files of an earlier cook that no asset makes any more, and the folders that leaves empty.
core::Result<std::size_t> removeStaleFiles(const std::filesystem::path& out, const Cache& written) {
    const auto files = assets::DiskFileSystem{}.listFiles(out);
    if (!files) {
        return std::unexpected{files.error()};
    }
    std::size_t removed = 0;
    std::error_code error;
    for (const std::string& file : *files) {
        if (file == ManifestName || written.contains(file)) {
            continue;
        }
        std::filesystem::remove(out / file, error);
        if (error) {
            return core::fail(std::format("cannot remove {}: {}", (out / file).string(), error.message()));
        }
        ++removed;
        for (auto folder = (out / file).parent_path(); folder != out && std::filesystem::is_empty(folder, error);
             folder = folder.parent_path()) {
            std::filesystem::remove(folder, error);
        }
    }
    return removed;
}

// What a cook does with one asset.
enum class Step : std::uint8_t {
    Cook,  // run its cooker
    Copy,  // copy it as is: no cooker takes its type, or its cooker's plan passes it through
    Keep,  // leave what an earlier cook wrote: same source, same cooker, untouched since
    Share, // copy the output of an earlier asset with the same source and cooker
};

// One asset of a cook.
struct Job {
    const assets::PackFile* file = nullptr;
    const Cooker* cooker = nullptr; // null when copied as is
    bool passed = false;            // a cooker takes its type, but its plan passes it through
    CookOptions options;
    std::string outputPath;
    CacheRecord record;
    Step step = Step::Cook;
    std::size_t sharedWith = 0; // the job whose output a shared one copies
    std::uint64_t sourceSize = 0;
    std::uint64_t outputSize = 0;
    core::Result<void> result;
};

// The most threads a cook uses. Assets are independent, so they are hashed and cooked in parallel; whatever depends
// on their order is decided between the parallel steps, so the output does not depend on the number of threads.
constexpr unsigned MaxThreads = 16;

// Runs task(0) to task(count - 1) on several threads and returns when all are done.
template <typename Task>
void runInParallel(std::size_t count, const Task& task) {
    std::atomic<std::size_t> next{0};
    const auto worker = [&] {
        for (std::size_t index = next++; index < count; index = next++) {
            task(index);
        }
    };
    const unsigned threadCount = std::clamp(std::thread::hardware_concurrency(), 1U, MaxThreads);
    std::vector<std::jthread> threads;
    for (unsigned i = 1; i < threadCount; ++i) {
        threads.emplace_back(worker);
    }
    worker();
    // The threads join here, as they go out of scope.
}

// Returns the error of the first job that failed, in path order.
core::Result<void> firstFailure(std::span<const Job> jobs) {
    const auto failed = std::ranges::find_if(jobs, [](const Job& job) { return !job.result; });
    return failed == jobs.end() ? core::Result<void>{} : std::unexpected{failed->result.error()};
}

// Cooks or copies one asset into the output folder and records the digest and size of what it wrote.
core::Result<void> produce(Job& job, const std::filesystem::path& pack, const std::filesystem::path& out) {
    const std::filesystem::path sourcePath = pack / job.file->path;
    const auto source = assets::DiskFileSystem{}.readFile(sourcePath);
    if (!source) {
        return std::unexpected{source.error()};
    }
    if (core::toHex(core::sha1(*source)) != job.record.source) {
        return core::fail(std::format("{} changed while cooking", sourcePath.string()));
    }
    const auto bytes =
        job.cooker == nullptr ? core::Result<std::vector<std::byte>>{*source} : job.cooker->cook(*source, job.options);
    if (!bytes) {
        return core::fail(std::format("{}: {}", sourcePath.string(), bytes.error().message));
    }
    if (auto saved = writeFile(out / job.outputPath, *bytes); !saved) {
        return saved;
    }
    job.record.sha1 = core::toHex(core::sha1(*bytes));
    job.outputSize = bytes->size();
    return {};
}

} // namespace

bool isWithin(const std::filesystem::path& path, const std::filesystem::path& folder) {
    const std::filesystem::path relative = path.lexically_relative(folder);
    return !relative.empty() && *relative.begin() != "..";
}

std::span<const Cooker> cookers() {
    return Registry;
}

const Cooker* findCooker(std::span<const Cooker> registry, std::string_view extension) {
    const auto found = std::ranges::find(registry, extension, &Cooker::sourceExtension);
    return found == registry.end() ? nullptr : &*found;
}

core::Result<CookSummary> cookPack(std::span<const Cooker> registry, const std::filesystem::path& pack,
                                   const std::filesystem::path& out) {
    const assets::DiskFileSystem disk;
    const auto manifest = disk.readFile(pack / ManifestName);
    if (!manifest) {
        return std::unexpected{manifest.error()};
    }
    const auto parsed = assets::parsePackManifest(textOf(*manifest));
    if (!parsed) {
        return core::fail(std::format("{}: {}", (pack / ManifestName).string(), parsed.error().message));
    }
    const auto files = assets::listPackFiles(disk, pack);
    if (!files) {
        return core::fail(std::format("pack '{}': {}", parsed->id, files.error().message));
    }
    const auto cache = prepareOutputFolder(pack, out);
    if (!cache) {
        return std::unexpected{cache.error()};
    }

    std::map<std::string, std::string, std::less<>> pathsById;
    for (const assets::PackFile& file : *files) {
        pathsById.emplace(file.id.str(), file.path);
    }

    // Hashes every source, decides how it cooks and checks what an earlier cook left, on several threads.
    std::vector<Job> jobs(files->size());
    runInParallel(jobs.size(), [&](std::size_t index) {
        Job& job = jobs[index];
        job.file = &(*files)[index];
        job.cooker = job.file->path.starts_with(MovieFolder)
                         ? nullptr
                         : findCooker(registry, job.file->path.substr(job.file->path.rfind('.') + 1));
        const auto source = disk.readFile(pack / job.file->path);
        if (!source) {
            job.result = std::unexpected{source.error()};
            return;
        }
        if (job.cooker != nullptr && job.cooker->plan != nullptr) {
            const auto sidecar = readSidecar(disk, pack, pathsById, job.file->id);
            if (!sidecar) {
                job.result = std::unexpected{sidecar.error()};
                return;
            }
            auto options = job.cooker->plan(job.file->id, *source, *sidecar ? &**sidecar : nullptr);
            if (!options) {
                job.result =
                    core::fail(std::format("{}: {}", (pack / job.file->path).string(), options.error().message));
                return;
            }
            job.options = *options;
        }
        if (job.options.passThrough) {
            job.cooker = nullptr;
            job.passed = true;
        }
        job.outputPath = job.cooker == nullptr ? job.file->path
                                               : std::format("{}.{}", job.file->id.str(), job.cooker->outputExtension);
        job.sourceSize = source->size();
        job.record = CacheRecord{
            .source = core::toHex(core::sha1(*source)), .cooker = cookerKey(job.cooker, job.options), .sha1 = {}};
        if (auto kept = keptDigest(*cache, job.outputPath, job.record, out / job.outputPath)) {
            job.record.sha1 = std::move(*kept);
            job.step = Step::Keep;
        }
    });
    if (auto failed = firstFailure(jobs); !failed) {
        return std::unexpected{failed.error()};
    }

    // In path order, so the choice does not depend on the threads: an asset with the same source and cooker as an
    // earlier one takes a copy of its output.
    std::map<std::string, std::size_t, std::less<>> firstJobs;
    for (std::size_t index = 0; index < jobs.size(); ++index) {
        Job& job = jobs[index];
        const auto [first, added] =
            firstJobs.try_emplace(std::format("{}:{}", job.record.cooker, job.record.source), index);
        if (job.step == Step::Keep) {
            continue;
        }
        if (added) {
            job.step = job.cooker != nullptr ? Step::Cook : Step::Copy;
        } else {
            job.step = Step::Share;
            job.sharedWith = first->second;
        }
    }

    runInParallel(jobs.size(), [&](std::size_t index) {
        if (jobs[index].step == Step::Cook || jobs[index].step == Step::Copy) {
            jobs[index].result = produce(jobs[index], pack, out);
        }
    });
    if (auto failed = firstFailure(jobs); !failed) {
        return std::unexpected{failed.error()};
    }

    CookSummary summary;
    Cache written;
    for (Job& job : jobs) {
        std::error_code error;
        if (job.step == Step::Share) {
            const Job& first = jobs[job.sharedWith];
            std::filesystem::create_directories((out / job.outputPath).parent_path(), error);
            std::filesystem::copy_file(out / first.outputPath, out / job.outputPath,
                                       std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                return core::fail(std::format("cannot write {}: {}", (out / job.outputPath).string(), error.message()));
            }
            job.record.sha1 = first.record.sha1;
            job.outputSize = first.outputSize;
        } else if (job.step == Step::Keep) {
            job.outputSize = std::filesystem::file_size(out / job.outputPath, error);
        }
        summary.sourceBytes += job.sourceSize;
        summary.outputBytes += job.outputSize;
        written.emplace(job.outputPath, job.record);
    }
    const auto countOf = [&](Step step) {
        return static_cast<std::size_t>(std::ranges::count(jobs, step, &Job::step));
    };
    summary.assets = jobs.size();
    summary.cooked = countOf(Step::Cook);
    summary.copied = countOf(Step::Copy);
    summary.passed = static_cast<std::size_t>(
        std::ranges::count_if(jobs, [](const Job& job) { return job.step == Step::Copy && job.passed; }));
    summary.copied -= summary.passed;
    summary.kept = countOf(Step::Keep);
    summary.shared = countOf(Step::Share);
    if (auto saved = writeFile(out / ManifestName, *manifest); !saved) {
        return std::unexpected{saved.error()};
    }
    const auto removed = removeStaleFiles(out, written);
    if (!removed) {
        return std::unexpected{removed.error()};
    }
    summary.removed = *removed;
    if (auto saved = writeCache(out / CacheFile, written); !saved) {
        return std::unexpected{saved.error()};
    }

    // The cooked pack must give the same asset ids as its source.
    const auto cooked = assets::listPackFiles(disk, out);
    if (!cooked) {
        return core::fail(std::format("the cooked pack does not list: {}", cooked.error().message));
    }
    if (!std::ranges::equal(*cooked, *files, {}, &assets::PackFile::id, &assets::PackFile::id)) {
        return core::fail("the cooked pack does not have the same asset ids as its source");
    }
    return summary;
}

int cook(const std::filesystem::path& pack, const std::filesystem::path& out) {
    const auto summary = cookPack(cookers(), pack, out);
    if (!summary) {
        std::println(stderr, "dwx: {}", summary.error().message);
        return ExitFailed;
    }
    std::println("Cooked pack: {}", out.string());
    std::println("Assets: {} ({} cooked, {} copied, {} passed through as lookup data, {} kept from the last cook, {} "
                 "shared with an asset with the same bytes)",
                 summary->assets, summary->cooked, summary->copied, summary->passed, summary->kept, summary->shared);
    std::println("Bytes: {} of source, {} cooked; {} stale files removed", summary->sourceBytes, summary->outputBytes,
                 summary->removed);
    return ExitOk;
}

} // namespace dwe::dwx
