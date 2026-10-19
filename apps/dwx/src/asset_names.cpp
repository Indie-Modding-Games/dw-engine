#include "asset_names.hpp"

#include <format>

namespace dwe::dwx::names {

namespace {

constexpr std::string_view AllTimPath = "CHDAT/ALLTIM.TIM";
constexpr std::string_view CardPath = "CARD/CARD.ALL";

std::string number(std::size_t value) {
    return std::format("{:03}", value);
}

} // namespace

std::string segment(std::string_view discName) {
    std::string text;
    text.reserve(discName.size());
    for (const char c : discName) {
        if (c >= 'A' && c <= 'Z') {
            text.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            text.push_back(c);
        } else {
            text.push_back('_');
        }
    }
    return text;
}

core::Result<assets::AssetId> digimonTexture(std::size_t type) {
    return assets::AssetId::parse(std::format("digimon/{}/texture", number(type)));
}

core::Result<assets::AssetId> cardImage(std::size_t card) {
    return assets::AssetId::parse(std::format("card/{}", number(card)));
}

core::Result<assets::AssetId> discImage(std::string_view volumePath, std::optional<std::size_t> index) {
    std::string id = "texture";
    std::string_view rest = volumePath;
    for (std::size_t slash = rest.find('/'); slash != std::string_view::npos; slash = rest.find('/')) {
        id += "/" + segment(rest.substr(0, slash));
        rest.remove_prefix(slash + 1);
    }
    // The file name without its extension.
    id += "/" + segment(rest.substr(0, rest.find('.')));
    if (index) {
        id += "/" + number(*index);
    }
    return assets::AssetId::parse(id);
}

core::Result<assets::AssetId> packEntry(std::string_view volumePath, std::size_t index) {
    if (volumePath == AllTimPath) {
        return digimonTexture(index);
    }
    if (volumePath == CardPath) {
        return cardImage(index);
    }
    return discImage(volumePath, index);
}

core::Result<assets::AssetId> metadataOf(const assets::AssetId& image) {
    return assets::AssetId::parse(image.str() + "_meta");
}

} // namespace dwe::dwx::names
