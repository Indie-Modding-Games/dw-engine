#include <dwe/assets/asset_id.hpp>

#include <algorithm>
#include <format>

namespace dwe::assets {

namespace {

bool isIdCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

} // namespace

core::Result<AssetId> AssetId::parse(std::string_view text) {
    if (text.empty()) {
        return core::fail("an asset id cannot be empty");
    }
    if (text.size() > MaxLength) {
        return core::fail(std::format("asset id '{}' is longer than {} characters", text, MaxLength));
    }
    if (text.front() == '/' || text.back() == '/' || text.contains("//")) {
        return core::fail(std::format("asset id '{}' has an empty segment", text));
    }
    for (const char c : text) {
        if (c != '/' && !isIdCharacter(c)) {
            return core::fail(std::format("asset id '{}' may only use a-z, 0-9, '_' and '/'", text));
        }
    }
    return AssetId{std::string{text}};
}

core::Result<AssetId> assetIdOfFile(std::string_view relativePath) {
    const std::size_t nameStart = relativePath.rfind('/') + 1;
    const std::string_view name = relativePath.substr(nameStart);
    const std::size_t dot = name.find('.');
    if (dot == std::string_view::npos || dot == 0 || name.find('.', dot + 1) != std::string_view::npos) {
        return core::fail(std::format("file '{}' must be named <name>.<extension>, with a single dot", relativePath));
    }
    const std::string_view extension = name.substr(dot + 1);
    const auto isExtensionCharacter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (extension.empty() || !std::ranges::all_of(extension, isExtensionCharacter)) {
        return core::fail(std::format("file '{}' needs a lowercase extension of letters and digits", relativePath));
    }
    auto id = AssetId::parse(relativePath.substr(0, nameStart + dot));
    if (!id) {
        return core::fail(std::format("file '{}': {}", relativePath, id.error().message));
    }
    return id;
}

} // namespace dwe::assets
