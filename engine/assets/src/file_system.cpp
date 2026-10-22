#include <dwe/assets/file_system.hpp>
#include <dwe/core/message.hpp>

#include <cstdint>
#include <format>
#include <fstream>
#include <system_error>

#include "messages.hpp"

namespace dwe::assets {

namespace {

bool isHidden(const std::filesystem::path& name) {
  return pathText(name).starts_with('.');
}

std::string keyOf(const std::filesystem::path& path) {
  std::string key = pathText(path.lexically_normal());
  while (key.size() > 1 && key.ends_with('/')) {
    key.pop_back();
  }
  return key;
}

} // namespace

std::string pathText(const std::filesystem::path& path) {
  const std::u8string text = path.generic_u8string();
  return std::string{reinterpret_cast<const char*>(text.data()), text.size()};
}

core::Result<std::vector<std::string>>
DiskFileSystem::listFiles(const std::filesystem::path& folder) const {
  std::error_code error;
  std::filesystem::recursive_directory_iterator entries{folder, error};
  if (error) {
    return core::fail(
        std::format("cannot open folder '{}': {}", pathText(folder), error.message()));
  }
  std::vector<std::string> files;
  for (; entries != std::filesystem::recursive_directory_iterator{}; entries.increment(error)) {
    if (error) {
      return core::fail(
          std::format("cannot list folder '{}': {}", pathText(folder), error.message()));
    }
    const std::filesystem::directory_entry& entry = *entries;
    if (isHidden(entry.path().filename())) {
      entries.disable_recursion_pending();
      continue;
    }
    if (entry.is_symlink(error)) {
      return core::fail(std::format("'{}' is a symbolic link", pathText(entry.path())));
    }
    if (entry.is_directory(error)) {
      continue;
    }
    if (!entry.is_regular_file(error)) {
      return core::fail(std::format("'{}' is not a regular file", pathText(entry.path())));
    }
    files.push_back(pathText(entry.path().lexically_relative(folder)));
  }
  if (error) {
    return core::fail(
        std::format("cannot list folder '{}': {}", pathText(folder), error.message()));
  }
  return files;
}

core::Result<std::uint64_t> DiskFileSystem::fileSize(const std::filesystem::path& file) const {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(file, error);
  if (error) {
    return core::fail(
        core::formatMessage(messages::CannotReadFile, pathText(file), error.message()));
  }
  return size;
}

core::Result<std::vector<std::byte>>
DiskFileSystem::readFile(const std::filesystem::path& file) const {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(file, error);
  if (error) {
    return core::fail(std::format("cannot read '{}': {}", pathText(file), error.message()));
  }
  std::ifstream stream{file, std::ios::binary};
  if (!stream) {
    return core::fail(std::format("cannot open '{}'", pathText(file)));
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return core::fail(std::format("cannot read '{}': it changed while reading", pathText(file)));
  }
  return bytes;
}

void MemoryFileSystem::addFile(const std::filesystem::path& file,
                               std::span<const std::byte> bytes) {
  files_.insert_or_assign(keyOf(file), std::vector<std::byte>{bytes.begin(), bytes.end()});
}

void MemoryFileSystem::addFile(const std::filesystem::path& file, std::string_view text) {
  addFile(file, std::as_bytes(std::span{text}));
}

void MemoryFileSystem::removeFile(const std::filesystem::path& file) {
  files_.erase(keyOf(file));
}

core::Result<std::vector<std::string>>
MemoryFileSystem::listFiles(const std::filesystem::path& folder) const {
  const std::string prefix = keyOf(folder) + '/';
  std::vector<std::string> files;
  bool exists = false;
  for (auto file = files_.lower_bound(prefix);
       file != files_.end() && file->first.starts_with(prefix); ++file) {
    exists = true;
    const std::string_view relative = std::string_view{file->first}.substr(prefix.size());
    const bool hidden = relative.starts_with('.') || relative.contains("/.");
    if (!hidden) {
      files.emplace_back(relative);
    }
  }
  if (!exists) {
    return core::fail(std::format("cannot open folder '{}': it does not exist", pathText(folder)));
  }
  return files;
}

core::Result<std::vector<std::byte>>
MemoryFileSystem::readFile(const std::filesystem::path& file) const {
  const auto found = files_.find(keyOf(file));
  if (found == files_.end()) {
    return core::fail(std::format("cannot read '{}': it does not exist", pathText(file)));
  }
  return found->second;
}

core::Result<std::uint64_t> MemoryFileSystem::fileSize(const std::filesystem::path& file) const {
  const auto found = files_.find(keyOf(file));
  if (found == files_.end()) {
    return core::fail(core::formatMessage(messages::FileMissing, pathText(file)));
  }
  return found->second.size();
}

} // namespace dwe::assets
