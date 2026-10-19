#pragma once

#include <dwe/core/error.hpp>

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>

namespace dwe::assets {

// The stable name of an asset, such as `digimon/agumon/model`: segments of lowercase letters, digits and
// underscores, separated by slashes. Packs override and add assets by this name, never by a number.
class AssetId {
public:
    static constexpr std::size_t MaxLength = 200;

    // Checks the text and builds the id.
    static core::Result<AssetId> parse(std::string_view text);

    [[nodiscard]] const std::string& str() const { return text_; }

    friend auto operator<=>(const AssetId&, const AssetId&) = default;

private:
    explicit AssetId(std::string text) : text_(std::move(text)) {}

    std::string text_;
};

} // namespace dwe::assets
