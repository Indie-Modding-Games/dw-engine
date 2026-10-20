#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/rhi/device.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace dwe;

// Executables that create a Device link vk-bootstrap, a C++ library from vcpkg built without AddressSanitizer. Its
// copies of libc++ templates (std::vector<std::string> growth) can replace the project's instrumented ones at link
// time, and AddressSanitizer then reports container overflows that are not there (docs/architecture/render.md). This
// grows a vector of strings inside assets, as the game does when it looks for packs, in an executable that links the
// device.
TEST_CASE("listing files works in an executable that links the device") {
    // Keeps Device, and with it vk-bootstrap, in the link.
    [[maybe_unused]] auto* const create = &rhi::Device::create;

    const std::filesystem::path folder = std::filesystem::current_path() / "link_test_files";
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    REQUIRE(std::filesystem::create_directories(folder / "pack", error));
    constexpr std::size_t FileCount = 40;
    for (std::size_t index = 0; index < FileCount; ++index) {
        std::ofstream{folder / "pack" / std::format("file_{:02}.bin", index)} << index;
    }

    const assets::DiskFileSystem disk;
    const auto files = disk.listFiles(folder);
    REQUIRE(files.has_value());
    CHECK(files.value_or(std::vector<std::string>{}).size() == FileCount);
    const auto packs = assets::findPacks(disk, folder);
    CHECK(packs.has_value());

    std::filesystem::remove_all(folder, error);
}
