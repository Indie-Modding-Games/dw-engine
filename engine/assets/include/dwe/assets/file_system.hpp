#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

// Where packs are read from: folders on disk, .pak archives (PakFileSystem) or files kept in memory for tests. The
// virtual file system only talks to this interface. Its const functions may be called from several threads at once.
class FileSystem {
public:
    FileSystem() = default;
    virtual ~FileSystem() = default;
    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;
    FileSystem(FileSystem&&) = delete;
    FileSystem& operator=(FileSystem&&) = delete;

    // Lists the files under a folder, at any depth, as paths relative to it with '/' between folders, in no
    // particular order. Files and folders whose name starts with '.' are left out.
    [[nodiscard]] virtual core::Result<std::vector<std::string>>
    listFiles(const std::filesystem::path& folder) const = 0;

    // Reads a whole file.
    [[nodiscard]] virtual core::Result<std::vector<std::byte>> readFile(const std::filesystem::path& file) const = 0;
};

// The files on disk. Symbolic links are errors: a pack must not reach outside its folder.
class DiskFileSystem final : public FileSystem {
public:
    [[nodiscard]] core::Result<std::vector<std::string>> listFiles(const std::filesystem::path& folder) const override;
    [[nodiscard]] core::Result<std::vector<std::byte>> readFile(const std::filesystem::path& file) const override;
};

// Files kept in memory, for tests and for content built at run time.
class MemoryFileSystem final : public FileSystem {
public:
    // Adds a file, or replaces it if it exists. Folders exist as long as they hold a file.
    void addFile(const std::filesystem::path& file, std::span<const std::byte> bytes);
    void addFile(const std::filesystem::path& file, std::string_view text);
    // Removes a file, if it exists.
    void removeFile(const std::filesystem::path& file);

    [[nodiscard]] core::Result<std::vector<std::string>> listFiles(const std::filesystem::path& folder) const override;
    [[nodiscard]] core::Result<std::vector<std::byte>> readFile(const std::filesystem::path& file) const override;

private:
    // Keyed by the normalized path with '/' separators.
    std::map<std::string, std::vector<std::byte>, std::less<>> files_;
};

// Returns a path as UTF-8 with '/' separators, for messages and asset ids.
std::string pathText(const std::filesystem::path& path);

} // namespace dwe::assets
