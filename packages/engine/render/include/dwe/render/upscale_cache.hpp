#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace dwe::render {

// Where HD scenery keeps what its upscaler made (scenery_upscale.hpp), so each background and each
// model texture is upscaled once: a folder under the user's data, one zstd-compressed file per
// result, named by a hash of everything the result depends on (the pixels, the palettes, the
// options and the upscaler's version). Nothing in it is ever needed: a missing, damaged or stale
// file is upscaled again. Safe to use from several threads (each file is written to a temporary
// name and renamed). The folder is kept under a size limit: past it, the files used longest ago
// go (a background is 3 to 5 MiB per palette variant once compressed).
class UpscaleCache {
public:
  static constexpr std::uintmax_t DefaultLimit = std::uintmax_t{512} << 20U;

  // Without a folder nothing is kept (tests, autotests that must not write).
  explicit UpscaleCache(std::optional<std::filesystem::path> folder = std::nullopt,
                        std::uintmax_t limit = DefaultLimit);

  // The bytes kept under key, when there are exactly `bytes` of them.
  [[nodiscard]] std::optional<std::vector<std::byte>> load(std::uint64_t key,
                                                           std::size_t bytes) const;
  // Keeps the bytes under key; a failure only logs a warning.
  void store(std::uint64_t key, std::span<const std::byte> data) const;

  [[nodiscard]] const std::optional<std::filesystem::path>& folder() const { return folder_; }

private:
  [[nodiscard]] std::filesystem::path fileOf(std::uint64_t key) const;
  // Removes the files used longest ago, but `kept`, until the folder is within the limit.
  void prune(const std::filesystem::path& kept) const;

  std::optional<std::filesystem::path> folder_;
  std::uintmax_t limit_;
};

// A 64-bit FNV-1a hash, fed piece by piece: the key of a cache entry.
class UpscaleKey {
public:
  UpscaleKey& add(std::span<const std::byte> bytes);
  // A plain value's bytes (a number, a struct without padding); containers go through the span.
  template <typename T>
    requires std::is_trivially_copyable_v<T>
  UpscaleKey& add(const T& value) {
    return add(std::as_bytes(std::span{&value, 1}));
  }
  [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
  std::uint64_t hash_ = 14695981039346656037ULL;
};

} // namespace dwe::render
