#include <dwe/assets/asset_id.hpp>

#include <algorithm>
#include <format>
#include <vector>

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

core::Result<void> checkIdPattern(std::string_view pattern) {
    if (pattern.empty() || pattern.size() > AssetId::MaxLength) {
        return core::fail(std::format("id pattern '{}' must have 1 to {} characters", pattern, AssetId::MaxLength));
    }
    if (pattern.front() == '/' || pattern.back() == '/' || pattern.contains("//")) {
        return core::fail(std::format("id pattern '{}' has an empty segment", pattern));
    }
    if (pattern.contains("***")) {
        return core::fail(std::format("id pattern '{}' has more than two '*' in a row", pattern));
    }
    if (!std::ranges::all_of(pattern, [](char c) { return c == '/' || c == '*' || isIdCharacter(c); })) {
        return core::fail(std::format("id pattern '{}' may only use a-z, 0-9, '_', '/' and '*'", pattern));
    }
    return {};
}

bool matchesIdPattern(std::string_view pattern, const AssetId& id) {
    // matched[j]: whether the pattern read so far matches the first j characters of the id. One pass per pattern
    // element keeps the time at length × length, whatever the stars.
    const std::string_view text = id.str();
    std::vector<bool> matched(text.size() + 1, false);
    matched[0] = true;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        std::vector<bool> next(text.size() + 1, false);
        if (pattern[i] == '*') {
            const bool crossesSegments = i + 1 < pattern.size() && pattern[i + 1] == '*';
            for (std::size_t j = 0; j <= text.size(); ++j) {
                next[j] = matched[j] || (j > 0 && next[j - 1] && (crossesSegments || text[j - 1] != '/'));
            }
            i += crossesSegments ? 1 : 0;
        } else {
            for (std::size_t j = 1; j <= text.size(); ++j) {
                next[j] = matched[j - 1] && text[j - 1] == pattern[i];
            }
        }
        matched = std::move(next);
    }
    return matched[text.size()];
}

} // namespace dwe::assets
