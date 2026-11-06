#include <dwe/core/log.hpp>
#include <dwe/render/upscale_cache.hpp>

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>
#include <utility>

namespace dwe::render {

namespace {

// A file: this magic, the size of the bytes it holds (8 bytes, little-endian as the machine), then
// one zstd frame of them.
constexpr std::array<char, 4> Magic{'D', 'W', 'U', '1'};
// Fast to write, small enough: the files are a few megabytes at most.
constexpr int Level = 3;

} // namespace

UpscaleCache::UpscaleCache(std::optional<std::filesystem::path> folder, std::uintmax_t limit)
    : folder_(std::move(folder)), limit_(limit) {}

std::filesystem::path UpscaleCache::fileOf(std::uint64_t key) const {
  return *folder_ / std::format("{:016x}.bin", key);
}

std::optional<std::vector<std::byte>> UpscaleCache::load(std::uint64_t key,
                                                         std::size_t bytes) const {
  if (!folder_) {
    return std::nullopt;
  }
  std::ifstream file{fileOf(key), std::ios::binary};
  if (!file) {
    return std::nullopt;
  }
  const std::vector<char> contents{std::istreambuf_iterator<char>{file}, {}};
  std::uint64_t size = 0;
  if (contents.size() < Magic.size() + sizeof(size) ||
      std::memcmp(contents.data(), Magic.data(), Magic.size()) != 0) {
    return std::nullopt;
  }
  std::memcpy(&size, contents.data() + Magic.size(), sizeof(size));
  if (size != bytes) {
    return std::nullopt;
  }
  std::vector<std::byte> result(bytes);
  const std::size_t header = Magic.size() + sizeof(size);
  const std::size_t written = ZSTD_decompress(result.data(), result.size(),
                                              contents.data() + header, contents.size() - header);
  if (ZSTD_isError(written) != 0 || written != bytes) {
    return std::nullopt;
  }
  // Used now: the last to go when the folder is pruned.
  std::error_code error;
  std::filesystem::last_write_time(fileOf(key), std::filesystem::file_time_type::clock::now(),
                                   error);
  return result;
}

void UpscaleCache::store(std::uint64_t key, std::span<const std::byte> data) const {
  if (!folder_) {
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(*folder_, error);
  std::vector<char> compressed(ZSTD_compressBound(data.size()));
  const std::size_t size =
      ZSTD_compress(compressed.data(), compressed.size(), data.data(), data.size(), Level);
  if (ZSTD_isError(size) != 0) {
    core::logWarning("upscale cache: cannot compress: {}", ZSTD_getErrorName(size));
    return;
  }
  const std::filesystem::path target = fileOf(key);
  std::filesystem::path temporary = target;
  temporary += std::format(".{}.tmp", std::hash<std::thread::id>{}(std::this_thread::get_id()));
  {
    std::ofstream file{temporary, std::ios::binary | std::ios::trunc};
    const std::uint64_t bytes = data.size();
    file.write(Magic.data(), Magic.size());
    file.write(reinterpret_cast<const char*>(&bytes), sizeof(bytes));
    file.write(compressed.data(), static_cast<std::streamsize>(size));
    if (!file) {
      core::logWarning("upscale cache: cannot write {}", temporary.string());
      std::filesystem::remove(temporary, error);
      return;
    }
  }
  std::filesystem::rename(temporary, target, error);
  if (error) {
    core::logWarning("upscale cache: cannot keep {}: {}", target.string(), error.message());
    std::filesystem::remove(temporary, error);
    return;
  }
  prune(target);
}

void UpscaleCache::prune(const std::filesystem::path& kept) const {
  struct Entry {
    std::filesystem::path path;
    std::filesystem::file_time_type used;
    std::uintmax_t size;
  };
  std::vector<Entry> entries;
  std::uintmax_t total = 0;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator{*folder_, error}) {
    if (!entry.is_regular_file(error) || entry.path().extension() != ".bin") {
      continue;
    }
    const std::uintmax_t size = entry.file_size(error);
    if (error) {
      continue;
    }
    total += size;
    entries.push_back({.path = entry.path(), .used = entry.last_write_time(error), .size = size});
  }
  if (total <= limit_) {
    return;
  }
  std::ranges::sort(entries, {}, &Entry::used);
  for (const Entry& entry : entries) {
    if (total <= limit_) {
      break;
    }
    if (entry.path == kept) {
      continue;
    }
    if (std::filesystem::remove(entry.path, error)) {
      total -= entry.size;
    }
  }
}

UpscaleKey& UpscaleKey::add(std::span<const std::byte> bytes) {
  constexpr std::uint64_t Prime = 1099511628211ULL;
  for (const std::byte byte : bytes) {
    hash_ = (hash_ ^ static_cast<std::uint64_t>(byte)) * Prime;
  }
  return *this;
}

} // namespace dwe::render
