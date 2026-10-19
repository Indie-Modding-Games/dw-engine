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

std::string digimonSlug(const GameTables* tables, std::size_t type) {
    if (tables != nullptr && type < tables->ids.digimon.size()) {
        return tables->ids.digimon[type];
    }
    return number(type);
}

core::Result<assets::AssetId> digimonAsset(const GameTables* tables, std::size_t type, std::string_view leaf) {
    return assets::AssetId::parse(std::format("digimon/{}/{}", digimonSlug(tables, type), leaf));
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

core::Result<assets::AssetId> packEntry(const GameTables* tables, std::string_view volumePath, std::size_t index) {
    if (volumePath == AllTimPath) {
        return digimonAsset(tables, index, "texture");
    }
    if (volumePath == CardPath) {
        return cardImage(index);
    }
    return discImage(volumePath, index);
}

core::Result<assets::AssetId> tableDocument(std::string_view name) {
    return assets::AssetId::parse(std::format("data/{}", name));
}

core::Result<assets::AssetId> bootConfig() {
    return assets::AssetId::parse("disc/boot");
}

core::Result<assets::AssetId> mapAsset(std::string_view slug, std::string_view leaf) {
    return assets::AssetId::parse(std::format("map/{}/{}", slug, leaf));
}

core::Result<assets::AssetId> mapImage(std::string_view slug, int bitsPerPixel, std::size_t index) {
    return assets::AssetId::parse(std::format("map/{}/texture{}/{}", slug, bitsPerPixel, number(index)));
}

core::Result<assets::AssetId> arenaCollision(std::string_view volumePath) {
    const std::string_view name = volumePath.substr(volumePath.rfind('/') + 1);
    return assets::AssetId::parse(std::format("arena/{}/collision", segment(name.substr(0, name.find('.')))));
}

core::Result<assets::AssetId> metadataOf(const assets::AssetId& image) {
    return assets::AssetId::parse(image.str() + "_meta");
}

} // namespace dwe::dwx::names
