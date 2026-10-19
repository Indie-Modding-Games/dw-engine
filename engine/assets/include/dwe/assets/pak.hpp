#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/sha1.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace dwe::assets {

// The .pak archive: one pack in a single file, laid out in docs/formats/pak.md. Version 1 stores every file as is.
inline constexpr std::uint32_t PakVersion = 1;
inline constexpr std::size_t PakHeaderSize = 64;
inline constexpr std::size_t PakEntrySize = 64;
// Where each file's bytes start: a multiple of this, from the start of the archive.
inline constexpr std::uint32_t PakAlignment = 16;

// How the bytes of an entry are stored: the low 8 bits of its flags.
enum class PakCompression : std::uint8_t { None = 0 };

// A file in a .pak archive.
struct PakEntry {
    std::string path; // relative to the pack's root, with '/' between folders
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    core::Sha1Digest sha1{}; // of the file's bytes

    friend bool operator==(const PakEntry&, const PakEntry&) = default;
};

// A .pak archive on disk seen as a file system with one folder, the archive's own path: listing it gives pack.yaml
// and every entry, and reading <archive>/<entry> reads that entry and checks its SHA-1. Mount it with a
// PackLocation whose root is the archive's path.
class PakFileSystem final : public FileSystem {
    // Only open() builds one.
    struct OpenKey {
        explicit OpenKey() = default;
    };

public:
    PakFileSystem(OpenKey /*key*/, std::filesystem::path archive, std::vector<std::byte> manifest,
                  std::vector<PakEntry> entries)
        : archive_(std::move(archive)), manifest_(std::move(manifest)), entries_(std::move(entries)) {}

    // Opens an archive and reads its table of contents and manifest; the entries are read when asked for.
    static core::Result<std::unique_ptr<PakFileSystem>> open(const std::filesystem::path& archive);

    [[nodiscard]] const std::filesystem::path& archive() const { return archive_; }

    // The entries, sorted by asset id.
    [[nodiscard]] std::span<const PakEntry> entries() const { return entries_; }

    [[nodiscard]] core::Result<std::vector<std::string>> listFiles(const std::filesystem::path& folder) const override;
    [[nodiscard]] core::Result<std::vector<std::byte>> readFile(const std::filesystem::path& file) const override;

private:
    std::filesystem::path archive_;
    std::vector<std::byte> manifest_;
    std::vector<PakEntry> entries_;
};

// Writes the pack at a root of a file system as a .pak archive: its pack.yaml, which must be a valid manifest, and
// every asset file, which must give valid and distinct ids. The pack's dependencies need not be there. The same
// files give the same archive, byte for byte. Returns the entries written.
core::Result<std::vector<PakEntry>> writePak(const FileSystem& fileSystem, const std::filesystem::path& packRoot,
                                             const std::filesystem::path& archive);

// The packs that findPacks found, ready to mount.
struct OpenedPacks {
    // Open while this lives: a VirtualFileSystem mounted from locations must not outlive it.
    std::vector<std::unique_ptr<PakFileSystem>> archives;
    // One per pack, in the order given.
    std::vector<PackLocation> locations;
};

// Opens packs for mounting: a folder pack is read through the given file system, which must outlive the result, and
// an archive is opened from disk with PakFileSystem. Fails on the first archive that does not open.
core::Result<OpenedPacks> openPacks(const FileSystem& folders, std::span<const FoundPack> packs);

} // namespace dwe::assets
