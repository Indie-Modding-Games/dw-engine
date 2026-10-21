#include "disc_cache.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/formats/disc/disc_image.hpp>

#include <charconv>
#include <cstdio>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include "base_pack.hpp"
#include "dwx.hpp"
#include "messages.hpp"

namespace dwe::dwx {
namespace {

// An entry's name is the disc's SHA-1, this separator and the extractor version.
constexpr std::string_view VersionSeparator = "-v";
// A temporary folder's name is a dot, the entry's name, this infix and a random suffix.
constexpr std::string_view PartialInfix = ".partial-";

bool isPartialName(std::string_view name) {
    return name.starts_with('.') && name.contains(PartialInfix);
}

// The extractor version of an entry of disc called name, if it is one.
std::optional<int> entryVersion(std::string_view name, std::string_view discSha1) {
    const std::string prefix = std::format("{}{}", discSha1, VersionSeparator);
    if (!name.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string_view digits = name.substr(prefix.size());
    // from_chars takes a sign: only digits name a version.
    if (digits.empty() || digits.front() < '0' || digits.front() > '9') {
        return std::nullopt;
    }
    int version = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), version);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return version;
}

// The type of what is at path, without following a link: the cache never follows one out of its folder.
std::filesystem::file_type typeOf(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::symlink_status(path, error).type();
}

// An entry is a folder with its record: dwx moves it into place only after writing both, so one without the record
// was not written by this mechanism and is extracted again.
bool isCompleteEntry(const std::filesystem::path& entry) {
    return typeOf(entry) == std::filesystem::file_type::directory &&
           typeOf(entry / CacheRecordFile) == std::filesystem::file_type::regular;
}

// Flushes a file or a folder to the disk, so an entry that a crash left in place after its rename has its content.
// Windows cannot flush a folder and only flushes a file opened for writing, so there the rename relies on NTFS alone.
bool syncToDisk(const std::filesystem::path& path) {
#ifdef _WIN32
    static_cast<void>(path);
    return true;
#else
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (descriptor < 0) {
        return false;
    }
    const bool synced = ::fsync(descriptor) == 0;
    return ::close(descriptor) == 0 && synced;
#endif
}

// Flushes every file and folder under folder, and folder itself.
core::Result<void> syncTree(const std::filesystem::path& folder) {
#ifdef _WIN32
    // Nothing to flush there (see syncToDisk): the tree is not even walked.
    static_cast<void>(folder);
    return {};
#else
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator entry{folder, error};
         !error && entry != std::filesystem::end(entry); entry.increment(error)) {
        const std::filesystem::file_type type = entry->symlink_status(error).type();
        if (!error && (type == std::filesystem::file_type::regular || type == std::filesystem::file_type::directory) &&
            !syncToDisk(entry->path())) {
            return core::fail(core::formatMessage(messages::CacheCannotSync, assets::pathText(entry->path())));
        }
    }
    if (error) {
        return core::fail(core::formatMessage(messages::CacheCannotList, assets::pathText(folder), error.message()));
    }
    if (!syncToDisk(folder)) {
        return core::fail(core::formatMessage(messages::CacheCannotSync, assets::pathText(folder)));
    }
    return {};
#endif
}

std::string randomSuffix() {
    std::random_device device;
    return std::format("{:08x}{:08x}", device(), device());
}

core::Result<std::vector<std::filesystem::path>> folderEntries(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> entries;
    std::error_code error;
    for (std::filesystem::directory_iterator entry{folder, error}; !error && entry != std::filesystem::end(entry);
         entry.increment(error)) {
        entries.push_back(entry->path());
    }
    if (error) {
        return core::fail(core::formatMessage(messages::CacheCannotList, assets::pathText(folder), error.message()));
    }
    return entries;
}

core::Result<void> writeRecord(const std::filesystem::path& folder, const DiscCacheKey& key) {
    const std::filesystem::path file = folder / CacheRecordFile;
    std::ofstream stream{file, std::ios::binary};
    stream << std::format("# {}\ndisc_sha1: {}\nextractor_version: {}\n", messages::CacheRecordComment, key.discSha1,
                          key.extractorVersion);
    if (!stream) {
        return core::fail(core::formatMessage(messages::CacheCannotWrite, assets::pathText(file)));
    }
    return {};
}

// Writes the entry into a temporary folder and moves it into place.
core::Result<CachedContent> writeEntry(const std::filesystem::path& cache, const DiscCacheKey& key,
                                       const ContentWriter& write) {
    const std::filesystem::path entry = cache / key.name();
    const std::filesystem::path partial = cache / std::format(".{}{}{}", key.name(), PartialInfix, randomSuffix());
    std::error_code error;
    std::filesystem::create_directories(partial, error);
    if (error) {
        return core::fail(core::formatMessage(messages::CacheCannotCreate, assets::pathText(partial), error.message()));
    }
    const auto discard = [&partial] {
        std::error_code ignored;
        std::filesystem::remove_all(partial, ignored);
    };
    auto written = write(partial);
    if (written) {
        written = writeRecord(partial, key);
    }
    if (written) {
        written = syncTree(partial);
    }
    if (!written) {
        discard();
        return std::unexpected{written.error()};
    }
    std::filesystem::rename(partial, entry, error);
    if (error) {
        discard();
        // Another extraction of the same disc got there first: its entry is just as good.
        if (isCompleteEntry(entry)) {
            return CachedContent{.folder = entry, .extracted = false};
        }
        return core::fail(core::formatMessage(messages::CacheCannotRename, assets::pathText(partial),
                                              assets::pathText(entry), error.message()));
    }
    // The rename itself is in the cache folder.
    if (!syncToDisk(cache)) {
        std::println(stderr, "dwx: warning: {}",
                     core::formatMessage(messages::CacheCannotSync, assets::pathText(cache)));
    }
    return CachedContent{.folder = entry, .extracted = true};
}

void reportStaleEntries(const std::filesystem::path& cache, const DiscCacheKey& key) {
    const auto removed = removeStaleEntries(cache, key);
    if (!removed) {
        std::println(stderr, "dwx: warning: {}", removed.error().message);
        return;
    }
    for (const std::filesystem::path& entry : *removed) {
        std::println(stderr, "dwx: {}", core::formatMessage(messages::CacheRemovedStale, assets::pathText(entry)));
    }
}

void reportOldPartialEntries(const std::filesystem::path& cache) {
    const std::vector<std::filesystem::path> partials = oldPartialEntries(cache, PartialEntryWarningAge);
    if (partials.empty()) {
        return;
    }
    std::string names;
    for (const std::filesystem::path& partial : partials) {
        names += std::format("{}{}", names.empty() ? "" : ", ", assets::pathText(partial));
    }
    std::println(stderr, "dwx: warning: {}",
                 core::formatMessage(messages::CacheOldPartials, PartialEntryWarningAge.count(), names));
}

} // namespace

std::string DiscCacheKey::name() const {
    return std::format("{}{}{}", discSha1, VersionSeparator, extractorVersion);
}

core::Result<DiscCacheKey> discCacheKey(const std::filesystem::path& image) {
    const auto disc = formats::disc::DiscImage::open(image);
    if (!disc) {
        return std::unexpected{disc.error()};
    }
    const auto digest = hashFile(disc->dataFile());
    if (!digest) {
        return std::unexpected{digest.error()};
    }
    return DiscCacheKey{.discSha1 = core::toHex(*digest), .extractorVersion = ExtractorVersion};
}

core::Result<CachedContent> cachedContent(const std::filesystem::path& cache, const DiscCacheKey& key,
                                          const ContentWriter& write) {
    const std::filesystem::path entry = cache / key.name();
    if (isCompleteEntry(entry)) {
        return CachedContent{.folder = entry, .extracted = false};
    }
    // Anything else with the entry's name (a link, a file, a folder without its record) is replaced; a link goes
    // without what it points to.
    if (typeOf(entry) != std::filesystem::file_type::not_found) {
        std::println(stderr, "dwx: warning: {}",
                     core::formatMessage(messages::CacheIncompleteEntry, assets::pathText(entry)));
        std::error_code error;
        std::filesystem::remove_all(entry, error);
        if (error) {
            return core::fail(
                core::formatMessage(messages::CacheCannotRemove, assets::pathText(entry), error.message()));
        }
    }
    return writeEntry(cache, key, write);
}

core::Result<std::vector<std::filesystem::path>> removeStaleEntries(const std::filesystem::path& cache,
                                                                    const DiscCacheKey& key) {
    auto entries = folderEntries(cache);
    if (!entries) {
        return std::unexpected{entries.error()};
    }
    std::vector<std::filesystem::path> removed;
    for (const std::filesystem::path& entry : *entries) {
        const std::optional<int> version = entryVersion(assets::pathText(entry.filename()), key.discSha1);
        if (!version || *version >= key.extractorVersion || typeOf(entry) != std::filesystem::file_type::directory ||
            typeOf(entry / BasePackFolder) != std::filesystem::file_type::directory ||
            !writtenByExtract(entry / BasePackFolder)) {
            continue;
        }
        std::error_code error;
        std::filesystem::remove_all(entry, error);
        if (error) {
            return core::fail(
                core::formatMessage(messages::CacheCannotRemove, assets::pathText(entry), error.message()));
        }
        removed.push_back(entry);
    }
    return removed;
}

std::vector<std::filesystem::path> oldPartialEntries(const std::filesystem::path& cache, std::chrono::hours age) {
    std::vector<std::filesystem::path> partials;
    const auto entries = folderEntries(cache);
    if (!entries) {
        return partials;
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const std::filesystem::path& entry : *entries) {
        std::error_code error;
        const auto written = std::filesystem::last_write_time(entry, error);
        if (!error && isPartialName(assets::pathText(entry.filename())) && now - written > age) {
            partials.push_back(entry);
        }
    }
    return partials;
}

int extractToCache(const std::filesystem::path& image, const std::filesystem::path& cache) {
    const auto key = discCacheKey(image);
    if (!key) {
        std::println(stderr, "dwx: {}", key.error().message);
        return ExitFailed;
    }
    const ContentWriter extract = [&image, &cache, &key](const std::filesystem::path& folder) {
        std::println(stderr, "dwx: {}",
                     core::formatMessage(messages::CacheExtracting, assets::pathText(image),
                                         assets::pathText(cache / key->name()), ExtractorVersion));
        return extractBasePack(image, folder / BasePackFolder, false, stderr);
    };
    const auto content = cachedContent(cache, *key, extract);
    if (!content) {
        std::println(stderr, "dwx: {}", content.error().message);
        return ExitFailed;
    }
    const std::string_view state = content->extracted ? messages::CacheWritten : messages::CacheReady;
    std::println(stderr, "dwx: {}",
                 core::formatMessage(state, assets::pathText(content->folder), key->extractorVersion));
    if (content->extracted) {
        reportStaleEntries(cache, *key);
    }
    reportOldPartialEntries(cache);
    std::println("{}", assets::pathText(content->folder));
    return ExitOk;
}

} // namespace dwe::dwx
