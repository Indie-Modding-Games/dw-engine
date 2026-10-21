#pragma once

#include <dwe/core/error.hpp>

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::assets {

// Splits a pack's namespace from the name in a namespaced content id: `mymod:newmon`.
inline constexpr char ContentNamespaceSeparator = ':';

// The longest name of a content id, without its namespace (which is a pack id, checkPackId).
inline constexpr std::size_t MaxContentNameLength = 64;

// The id of an entry of a content table (a Digimon, a map, an item, a move), as other tables, saves and scripts name
// it. An original keeps the stable name the base pack gives it (`species_a`); an entry a pack adds is namespaced with
// that pack's id (`mymod:newmon`), so two mods never claim the same name
// (docs/architecture/vfs.md#tablas-por-entradas). Each part is lowercase letters, digits and underscores; numbers
// (`index`) stay internal to the engine.
class ContentId {
public:
    // Checks the text and builds the id.
    static core::Result<ContentId> parse(std::string_view text);

    [[nodiscard]] const std::string& str() const { return text_; }

    // The pack that added the entry, or empty for an original.
    [[nodiscard]] std::string_view packNamespace() const;

    // The name without its namespace.
    [[nodiscard]] std::string_view name() const;

    [[nodiscard]] bool isNamespaced() const { return separator_ != std::string::npos; }

    // The asset path of the entry's folder, below the table's folder: `species_a` for an original, `mymod/newmon` for a
    // namespaced id, since asset ids have no ':' (`digimon/mymod/newmon/model`).
    [[nodiscard]] std::string assetPath() const;

    friend bool operator==(const ContentId& left, const ContentId& right) { return left.text_ == right.text_; }
    friend auto operator<=>(const ContentId& left, const ContentId& right) { return left.text_ <=> right.text_; }

private:
    ContentId(std::string text, std::size_t separator) : text_(std::move(text)), separator_(separator) {}

    std::string text_;
    std::size_t separator_ = std::string::npos;
};

// The asset path of a content id's folder (ContentId::assetPath), or the text as it is when it is no content id, such
// as a raw number the extractor kept as text.
std::string contentAssetPath(std::string_view id);

} // namespace dwe::assets
