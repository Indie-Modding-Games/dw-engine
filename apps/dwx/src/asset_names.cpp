#include "asset_names.hpp"

#include <format>

namespace dwe::dwx::names {

namespace {

constexpr std::string_view AllTimPath = "CHDAT/ALLTIM.TIM";
constexpr std::string_view CardPath = "CARD/CARD.ALL";

std::string number(std::size_t value) {
    return std::format("{:03}", value);
}

// "<prefix>/<folders>/<name>" for a volume path, without the file's extension.
std::string volumePathId(std::string_view prefix, std::string_view volumePath) {
    std::string id{prefix};
    std::string_view rest = volumePath;
    for (std::size_t slash = rest.find('/'); slash != std::string_view::npos; slash = rest.find('/')) {
        id += "/" + segment(rest.substr(0, slash));
        rest.remove_prefix(slash + 1);
    }
    id += "/" + segment(rest.substr(0, rest.find('.')));
    return id;
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

core::Result<assets::AssetId> discModel(std::string_view volumePath, std::string_view variant) {
    std::string id = volumePathId("model", volumePath);
    if (!variant.empty()) {
        id += "_" + segment(variant);
    }
    return assets::AssetId::parse(id);
}

core::Result<assets::AssetId> cardImage(std::size_t card) {
    return assets::AssetId::parse(std::format("card/{}", number(card)));
}

core::Result<assets::AssetId> discImage(std::string_view volumePath, std::optional<std::size_t> index) {
    std::string id = volumePathId("texture", volumePath);
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

core::Result<assets::AssetId> soundAsset(std::string_view bank, std::optional<std::size_t> entry,
                                         std::string_view leaf) {
    if (entry) {
        return assets::AssetId::parse(std::format("sound/{}/{}/{}", segment(bank), number(*entry), leaf));
    }
    return assets::AssetId::parse(std::format("sound/{}/{}", segment(bank), leaf));
}

core::Result<assets::AssetId> musicAsset(std::size_t font, std::string_view leaf) {
    return assets::AssetId::parse(std::format("music/{}/{}", number(font), leaf));
}

core::Result<assets::AssetId> movieAsset(std::string_view volumePath, std::string_view leaf) {
    const std::string_view name = volumePath.substr(volumePath.rfind('/') + 1);
    return assets::AssetId::parse(std::format("movie/{}/{}", segment(name.substr(0, name.find('.'))), leaf));
}

core::Result<assets::AssetId> metadataOf(const assets::AssetId& image) {
    return assets::AssetId::parse(image.str() + "_meta");
}

core::Result<assets::AssetId> scriptListing(std::string_view volumePath, std::optional<std::size_t> id) {
    const std::string_view name = volumePath.substr(volumePath.rfind('/') + 1);
    std::string text = "script/" + segment(name.substr(0, name.find('.')));
    if (id) {
        text += "/" + number(*id);
    }
    return assets::AssetId::parse(text);
}

core::Result<assets::AssetId> textTable(std::string_view language, std::string_view source) {
    return assets::AssetId::parse(std::format("text/{}/{}", segment(language), segment(source)));
}

core::Result<assets::AssetId> fontImage() {
    return assets::AssetId::parse("font/main");
}

core::Result<assets::AssetId> effectAsset(const GameTables* tables, std::size_t slot, std::string_view leaf) {
    const bool named = tables != nullptr && slot < tables->ids.moves.size();
    return assets::AssetId::parse(std::format("effect/{}/{}", named ? tables->ids.moves[slot] : number(slot), leaf));
}

core::Result<assets::AssetId> modelContract(const GameTables* tables, std::size_t type) {
    return digimonAsset(tables, type, ContractLeaf);
}

const std::vector<DocumentSchema>& documentSchemas() {
    static const std::vector<DocumentSchema> schemas{
        {.schema = "cards", .ids = {"data/cards"}},
        {.schema = "digimon", .ids = {"data/digimon"}},
        {.schema = "evolution", .ids = {"data/evolution"}},
        {.schema = "fishing", .ids = {"data/fishing"}},
        {.schema = "items", .ids = {"data/items"}},
        {.schema = "item_effects", .ids = {"data/item_effects"}},
        {.schema = "maps", .ids = {"data/maps"}},
        {.schema = "medals", .ids = {"data/medals"}},
        {.schema = "moves", .ids = {"data/moves"}},
        {.schema = "music", .ids = {"data/music"}},
        {.schema = "new_game_stats", .ids = {"data/new_game_stats"}},
        {.schema = "raise", .ids = {"data/raise"}},
        {.schema = "shops", .ids = {"data/shops"}},
        {.schema = "skeletons", .ids = {"data/skeletons"}},
        {.schema = "stadium", .ids = {"data/stadium"}},
        {.schema = "tournaments", .ids = {"data/tournaments"}},
        {.schema = "training", .ids = {"data/training"}},
        {.schema = "versus", .ids = {"data/versus"}},
        {.schema = "boot", .ids = {"disc/boot"}},
        {.schema = MapSchema, .ids = {"map/*/data"}},
        {.schema = "collision", .ids = {"arena/*/collision"}},
        {.schema = "image",
         .ids = {"map/*/texture8/*_meta", "map/*/texture4/*_meta", "digimon/*/texture_meta", "card/*_meta",
                 "effect/*/texture_meta", "texture/**_meta"}},
        {.schema = ContractSchema, .ids = {"digimon/*/contract"}},
        {.schema = "sound_bank", .ids = {"sound/*/bank_meta", "sound/*/*/bank_meta", "music/*/font_meta"}},
        {.schema = "script", .ids = {"script/maphead", "script/*/*"}},
        {.schema = "text", .ids = {"text/*/*"}},
        {.schema = "font", .ids = {"font/main_meta"}},
        {.schema = "effect", .ids = {"effect/*/effect"}},
        {.schema = "effect_script", .ids = {"effect/*/script"}},
        {.schema = "movie", .ids = {"movie/*/movie"}},
        {.schema = "coverage", .ids = {"dwx/coverage"}},
    };
    return schemas;
}

} // namespace dwe::dwx::names
