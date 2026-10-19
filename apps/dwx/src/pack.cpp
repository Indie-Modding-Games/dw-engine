#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>

#include <algorithm>
#include <cstdio>
#include <format>
#include <print>
#include <span>
#include <system_error>

#include "dwx.hpp"

namespace dwe::dwx {

namespace {

// Opens the written archive and reads every entry back, which checks each one against its SHA-1, and its manifest.
core::Result<void> checkArchive(const std::filesystem::path& folder, const std::filesystem::path& archive,
                                std::span<const assets::PakEntry> written) {
    const auto pak = assets::PakFileSystem::open(archive);
    if (!pak) {
        return std::unexpected{pak.error()};
    }
    if (!std::ranges::equal((*pak)->entries(), written)) {
        return core::fail("its table of contents is not what was written");
    }
    for (const assets::PakEntry& entry : written) {
        if (auto read = (*pak)->readFile(archive / entry.path); !read) {
            return std::unexpected{read.error()};
        }
    }
    const auto manifest = (*pak)->readFile(archive / "pack.yaml");
    const auto original = assets::DiskFileSystem{}.readFile(folder / "pack.yaml");
    if (!manifest || !original || *manifest != *original) {
        return core::fail("its manifest is not the pack's");
    }
    return {};
}

} // namespace

int pack(const std::filesystem::path& folder, const std::filesystem::path& archive) {
    // An archive inside the pack would be packed into the next archive as one more file.
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::weakly_canonical(archive, error)
                                               .lexically_relative(std::filesystem::weakly_canonical(folder, error));
    if (error || (!relative.empty() && *relative.begin() != "..")) {
        std::println(stderr, "dwx: the archive {} must be outside the pack {}", archive.string(), folder.string());
        return ExitFailed;
    }
    const auto entries = assets::writePak(assets::DiskFileSystem{}, folder, archive);
    if (!entries) {
        std::println(stderr, "dwx: {}", entries.error().message);
        return ExitFailed;
    }
    if (auto checked = checkArchive(folder, archive, *entries); !checked) {
        std::println(stderr, "dwx: the archive {} does not read back: {}", archive.string(), checked.error().message);
        return ExitFailed;
    }
    std::uint64_t fileBytes = 0;
    std::uint64_t storedBytes = 0;
    std::size_t compressed = 0;
    for (const assets::PakEntry& entry : *entries) {
        fileBytes += entry.size;
        storedBytes += entry.storedSize;
        compressed += entry.compression == assets::PakCompression::Zstd ? 1 : 0;
    }
    std::println("Archive: {}", archive.string());
    std::println("Files: {} ({} bytes), {} compressed with zstd, {} stored as they are ({} bytes stored); archive {} "
                 "bytes",
                 entries->size(), fileBytes, compressed, entries->size() - compressed, storedBytes,
                 std::filesystem::file_size(archive, error));
    std::println("Check: every entry reads back and matches its SHA-1");
    return ExitOk;
}

} // namespace dwe::dwx
