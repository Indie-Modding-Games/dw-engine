#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::game::save {

// The longest file name of a save storage.
inline constexpr std::size_t MaxFileNameLength = 64;

// Added to a file's name while it is being written.
inline constexpr std::string_view TemporarySuffix = ".tmp";

// Where save files live. The app gives the save system a storage: a folder of the user's data
// (DiskSaveStorage) or memory for tests. Names are plain file names (`slot_01.yaml`): 1 to 64
// characters `a-z`, `0-9`, `_` and `.`, not starting with a dot.
class SaveStorage {
public:
  SaveStorage() = default;
  virtual ~SaveStorage() = default;
  SaveStorage(const SaveStorage&) = delete;
  SaveStorage& operator=(const SaveStorage&) = delete;
  SaveStorage(SaveStorage&&) = delete;
  SaveStorage& operator=(SaveStorage&&) = delete;

  // Reads a whole file; nothing if it does not exist.
  [[nodiscard]] virtual core::Result<std::optional<std::string>>
  read(std::string_view name) const = 0;

  // Replaces a file so that it holds either its old text or the new one, never a part of either,
  // even if the game stops halfway. The old text, if there was one, is kept in `backup` (replacing
  // an earlier backup).
  virtual core::Result<void> write(std::string_view name, std::string_view text,
                                   std::string_view backup) = 0;
};

// Whether a name can name a file of a save storage.
[[nodiscard]] bool isValidFileName(std::string_view name);

// Save files in a folder on disk. The folder is created on the first write. A write goes to
// `<name>.tmp`, the old file is copied to the backup, and the temporary file is renamed over the
// old one, which the file system does in one step.
class DiskSaveStorage final : public SaveStorage {
public:
  explicit DiskSaveStorage(std::filesystem::path folder);

  [[nodiscard]] const std::filesystem::path& folder() const { return folder_; }

  [[nodiscard]] core::Result<std::optional<std::string>> read(std::string_view name) const override;
  core::Result<void> write(std::string_view name, std::string_view text,
                           std::string_view backup) override;

private:
  std::filesystem::path folder_;
};

// Save files in memory, for tests.
class MemorySaveStorage final : public SaveStorage {
public:
  [[nodiscard]] core::Result<std::optional<std::string>> read(std::string_view name) const override;
  core::Result<void> write(std::string_view name, std::string_view text,
                           std::string_view backup) override;

  // Puts a file as it is, without a backup: for files written by an older game or damaged ones.
  void put(std::string name, std::string text);

  // Makes the next write fail without changing anything, like a full disk.
  void failNextWrite() { failNextWrite_ = true; }

private:
  std::map<std::string, std::string, std::less<>> files_;
  bool failNextWrite_ = false;
};

} // namespace dwe::game::save
