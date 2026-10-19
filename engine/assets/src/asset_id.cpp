#include <dwe/assets/asset_id.hpp>

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

} // namespace dwe::assets
