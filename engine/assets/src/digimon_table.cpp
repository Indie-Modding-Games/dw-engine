#include <dwe/assets/content_id.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/messages.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <format>
#include <span>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view TableKey = "digimon";
constexpr std::string_view IdKey = "id";
constexpr std::string_view NameKey = "name";
constexpr std::string_view FileNameKey = "file_name";
constexpr std::string_view BoneCountKey = "bone_count";
constexpr std::string_view RadiusKey = "radius";
constexpr std::string_view HeightKey = "height";
constexpr std::string_view TypeKey = "type";
constexpr std::string_view LevelKey = "level";
constexpr std::string_view SpecialKey = "special";
constexpr std::string_view DropItemKey = "drop_item";
constexpr std::string_view DropChanceKey = "drop_chance";
constexpr std::string_view MovesKey = "moves";

template <std::size_t Count>
std::array<std::optional<std::int32_t>, Count> readOptionalIntegers(YamlReader& reader, const YamlNode& node,
                                                                    std::string_view what) {
    std::array<std::optional<std::int32_t>, Count> values{};
    const std::span<const YamlNode> items = reader.sequence(node, what, Count);
    for (std::size_t i = 0; i < items.size(); ++i) {
        values.at(i) = reader.optionalInteger<std::int32_t>(items[i]);
    }
    return values;
}

template <std::size_t Count>
std::array<std::optional<std::string>, Count> readOptionalTexts(YamlReader& reader, const YamlNode& node,
                                                                std::string_view what) {
    std::array<std::optional<std::string>, Count> values{};
    const std::span<const YamlNode> items = reader.sequence(node, what, Count);
    for (std::size_t i = 0; i < items.size(); ++i) {
        values.at(i) = reader.optionalText(items[i]);
    }
    return values;
}

DigimonRow readRow(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::DigimonRowText,
                           {IdKey, EntryIndexKey, NameKey, FileNameKey, BoneCountKey, RadiusKey, HeightKey, TypeKey,
                            LevelKey, SpecialKey, DropItemKey, DropChanceKey, MovesKey})) {
        return {};
    }
    const auto integer = [&](std::string_view key) { return reader.integer<std::int32_t>(reader.field(node, key)); };
    return {.id = reader.text(reader.field(node, IdKey)),
            .index = integer(EntryIndexKey),
            .name = reader.text(reader.field(node, NameKey)),
            .fileName = reader.text(reader.field(node, FileNameKey)),
            .boneCount = integer(BoneCountKey),
            .radius = integer(RadiusKey),
            .height = integer(HeightKey),
            .type = integer(TypeKey),
            .level = integer(LevelKey),
            .special = readOptionalIntegers<3>(reader, reader.field(node, SpecialKey), messages::SpecialsText),
            .dropItem = reader.optionalText(reader.field(node, DropItemKey)),
            .dropChance = integer(DropChanceKey),
            .moves = readOptionalTexts<16>(reader, reader.field(node, MovesKey), messages::MovesText)};
}

} // namespace

const DigimonRow* DigimonTable::find(std::string_view id) const {
    const auto found = std::ranges::find(digimon, id, &DigimonRow::id);
    return found == digimon.end() ? nullptr : &*found;
}

core::Result<DigimonTable> parseDigimonTable(std::string_view yaml) {
    auto root = core::parseYaml(yaml);
    if (!root) {
        return std::unexpected{root.error()};
    }
    YamlReader reader;
    DigimonTable table;
    if (reader.expectKeys(*root, messages::DigimonTableText, {TableKey})) {
        for (const YamlNode& row : reader.sequence(reader.field(*root, TableKey), TableKey)) {
            table.digimon.push_back(readRow(reader, row));
        }
    }
    return reader.finish(std::move(table), messages::DigimonTableText);
}

std::string speciesContractId(std::string_view species) {
    return std::format("{}/{}/{}", SpeciesFolder, contentAssetPath(species), SpeciesContractLeaf);
}

core::Result<void> requireNewSpeciesContracts(const VirtualFileSystem& vfs, const DigimonTable& table) {
    for (const DigimonRow& row : table.digimon) {
        const auto species = ContentId::parse(row.id);
        if (!species || !species->isNamespaced()) {
            continue;
        }
        const std::string contract = speciesContractId(row.id);
        const auto id = AssetId::parse(contract);
        if (!id || vfs.resolve(*id) == nullptr) {
            return core::fail(messages::formatMessage(messages::NewSpeciesWithoutContract, row.id, contract));
        }
    }
    return {};
}

core::Result<DigimonTable> loadDigimonTable(const VirtualFileSystem& vfs, const AssetId& id) {
    auto text = readComposedTable(vfs, id, DigimonComposition);
    if (!text) {
        return std::unexpected{text.error()};
    }
    auto table = parseDigimonTable(*text);
    if (!table) {
        return table;
    }
    if (auto contracts = requireNewSpeciesContracts(vfs, *table); !contracts) {
        return std::unexpected{contracts.error()};
    }
    return table;
}

} // namespace dwe::assets
