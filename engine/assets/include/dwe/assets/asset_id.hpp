#pragma once

#include <dwe/core/error.hpp>

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::assets {

// The stable name of an asset, such as `digimon/agumon/model`: segments of lowercase letters,
// digits and underscores, separated by slashes. Packs override and add assets by this name, never
// by a number.
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

// Returns the id of a file inside a pack: its path from the pack's root without the extension, so
// `digimon/agumon/model.gltf` is `digimon/agumon/model`. The file name must have exactly one dot,
// followed by a lowercase extension.
core::Result<AssetId> assetIdOfFile(std::string_view relativePath);

// Checks a pattern of asset ids: an id where `*` stands for any text inside one segment and `**`
// for any text, slashes included. `map/*/data` matches `map/town/data`; `texture/**_meta` matches
// `texture/etc/a/001_meta`.
core::Result<void> checkIdPattern(std::string_view pattern);

// Whether an id matches a pattern that checkIdPattern accepts.
bool matchesIdPattern(std::string_view pattern, const AssetId& id);

} // namespace dwe::assets
