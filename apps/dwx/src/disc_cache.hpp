#pragma once

#include <dwe/core/error.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The dev cache of extracted discs (docs/architecture/assets-de-desarrollo.md): one content folder per disc and
// extractor version, which the game mounts with --dev-assets so it can read a disc image without a manual extraction.
namespace dwe::dwx {

// The version of what dwx extract writes from a disc. Bump it whenever the base pack written from the same disc
// changes, so a dev cache extracts the disc again instead of serving the old output. The fingerprint test of the disc
// cache fails until it is bumped (apps/dwx/README.md).
inline constexpr int ExtractorVersion = 3;

// The folder of an entry that holds the base pack, as in an extracted content folder.
inline constexpr std::string_view BasePackFolder = "base";
// The file of an entry that records which disc and extractor version made it.
inline constexpr std::string_view CacheRecordFile = "dwx-cache.yaml";
// Temporary folders older than this are named in a warning, so they can be deleted by hand.
inline constexpr std::chrono::hours PartialEntryWarningAge{24};

// A content folder in the cache.
struct CachedContent {
    std::filesystem::path folder;
    bool extracted = false; // false when the entry was already there
};

// The name of a disc's entry: the SHA-1 of the file that holds its sectors (the BIN of a CUE sheet) and the extractor
// version that wrote it.
struct DiscCacheKey {
    std::string discSha1;
    int extractorVersion = ExtractorVersion;

    [[nodiscard]] std::string name() const;
};

// Writes a content folder: the base pack under BasePackFolder.
using ContentWriter = std::function<core::Result<void>(const std::filesystem::path& folder)>;

// The key of a disc image for this dwx: hashes the file that holds its sectors.
core::Result<DiscCacheKey> discCacheKey(const std::filesystem::path& image);

// The entry of key in the cache folder. An entry that exists is used as it is: entries only appear by being renamed
// into place, so one that exists is complete. Otherwise write fills a temporary folder, the record of the key is added
// and the folder is renamed into place; if another process renamed its own first, that one is used. A failed write
// leaves nothing behind.
core::Result<CachedContent> cachedContent(const std::filesystem::path& cache, const DiscCacheKey& key,
                                          const ContentWriter& write);

// Removes the entries of the disc of key that an older extractor version made, only those dwx extract wrote, and
// returns them. Entries of a newer version stay (another dwx still uses them), and so do temporary folders: an
// extraction may still be writing them.
core::Result<std::vector<std::filesystem::path>> removeStaleEntries(const std::filesystem::path& cache,
                                                                    const DiscCacheKey& key);

// The temporary folders of the cache last written more than age ago: left by stopped extractions, most likely.
[[nodiscard]] std::vector<std::filesystem::path> oldPartialEntries(const std::filesystem::path& cache,
                                                                   std::chrono::hours age);

// dwx extract --cache: prints the content folder of the disc's entry on the standard output, as the only thing
// written there, extracting the disc first when the entry is not there. Progress goes to the standard error.
int extractToCache(const std::filesystem::path& image, const std::filesystem::path& cache);

} // namespace dwe::dwx
