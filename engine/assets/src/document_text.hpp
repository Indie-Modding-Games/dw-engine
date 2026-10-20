#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <string>
#include <string_view>

namespace dwe::assets {

// The text of a YAML document of the packs, by asset id.
inline core::Result<std::string> readDocumentText(const VirtualFileSystem& vfs, std::string_view assetId) {
    auto id = AssetId::parse(assetId);
    if (!id) {
        return std::unexpected{id.error()};
    }
    auto bytes = vfs.read(*id);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

} // namespace dwe::assets
