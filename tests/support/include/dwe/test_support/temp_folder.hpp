#pragma once

#include <dwe/core/assert.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string_view>
#include <system_error>

namespace dwe::test_support {

// A folder under the system's temporary folder, with a unique name, removed with everything in it
// at the end.
class TempFolder {
public:
  TempFolder() {
    std::random_device random;
    path_ = std::filesystem::temp_directory_path() /
            std::format("dwe_tests_{:08x}{:08x}", random(), random());
    std::error_code error;
    DWE_ASSERT(std::filesystem::create_directories(path_, error));
  }
  ~TempFolder() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  TempFolder(const TempFolder&) = delete;
  TempFolder& operator=(const TempFolder&) = delete;
  TempFolder(TempFolder&&) = delete;
  TempFolder& operator=(TempFolder&&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  // Writes a file at a path relative to the folder, creating the folders it needs.
  void write(const std::filesystem::path& relative, std::string_view text) const {
    const std::filesystem::path file = path_ / relative;
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    DWE_ASSERT(!error);
    std::ofstream stream{file, std::ios::binary};
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    DWE_ASSERT(stream.good());
  }

private:
  std::filesystem::path path_;
};

} // namespace dwe::test_support
