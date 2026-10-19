#include "game_tables.hpp"

#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/map/map_file.hpp>

#include <algorithm>
#include <utility>

namespace dwe::dwx {

core::Result<GameTables> readGameTables(std::span<const std::byte> executable,
                                        const formats::exe::ExecutableLayout& layout) {
    const auto image = formats::exe::MemoryImage::fromExecutable(executable);
    if (!image) {
        return std::unexpected{image.error()};
    }
    auto tables = formats::exe::readExecutableTables(*image, layout);
    if (!tables) {
        return std::unexpected{tables.error()};
    }
    auto ids = formats::exe::makeTableIds(*tables);
    return GameTables{.tables = std::move(*tables), .ids = std::move(ids)};
}

std::string mapVolumePath(std::size_t mapId, std::string_view filename, std::string_view extension) {
    std::string path = formats::map::mapFilePath(static_cast<int>(mapId), filename, extension);
    std::ranges::replace(path, '\\', '/');
    return path.starts_with('/') ? path.substr(1) : path;
}

} // namespace dwe::dwx
