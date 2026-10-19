#include <dwe/formats/exe/tables_yaml.hpp>

#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <concepts>
#include <format>
#include <iterator>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

namespace dwe::formats::exe {

namespace {

using core::YamlNode;
using yaml::boolean;
using yaml::null;
using yaml::number;
using yaml::numbers;
using yaml::quoted;

constexpr std::uint8_t NoneU8 = 0xFF;
constexpr std::uint16_t AlwaysTrigger = 0xFFFF;
constexpr std::size_t MedalDescriptionLines = 3;

// Builds the documents, resolving references to ids and collecting notes.
class Converter {
public:
    explicit Converter(const ExecutableTables& tables) : tables_(&tables), ids_(makeTableIds(tables)) {}

    TablesYaml convert() {
        TablesYaml out;
        out.documents.push_back({.name = "digimon", .document = digimon()});
        out.documents.push_back({.name = "skeletons", .document = skeletons()});
        out.documents.push_back({.name = "moves", .document = moves()});
        out.documents.push_back({.name = "items", .document = items()});
        out.documents.push_back({.name = "evolution", .document = evolution()});
        out.documents.push_back({.name = "raise", .document = raise()});
        out.documents.push_back({.name = "cards", .document = cards()});
        out.documents.push_back({.name = "tournaments", .document = tournaments()});
        out.documents.push_back({.name = "medals", .document = medals()});
        out.documents.push_back({.name = "music", .document = music()});
        out.documents.push_back({.name = "shops", .document = shops()});
        out.documents.push_back({.name = "maps", .document = maps()});
        out.notes = std::move(notes_);
        return out;
    }

private:
    // A game text; notes the ones that keep escaped bytes.
    YamlNode text(const std::string& raw, std::string_view where) {
        std::string escaped = escapeGameText(raw);
        if (escaped != raw) {
            notes_.push_back(std::format("{}: text kept with escaped bytes", where));
        }
        return quoted(std::move(escaped));
    }

    // A reference by index: the id, null for the "none" value, or the raw number (with a note) if it is outside the
    // table.
    template <std::integral T>
    YamlNode reference(const std::vector<std::string>& ids, T index, std::optional<T> none, std::string_view where) {
        if (none && index == *none) {
            return null();
        }
        if (std::cmp_greater_equal(index, 0) && std::cmp_less(index, ids.size())) {
            return quoted(ids[static_cast<std::size_t>(index)]);
        }
        notes_.push_back(std::format("{}: reference {} is outside its table", where, index));
        return number(index);
    }

    // A Digimon type stored in a signed field (-1 = none) or an unsigned byte (0xFF = none).
    template <std::integral T>
    YamlNode digimonRef(T type, std::string_view where) {
        return reference<T>(ids_.digimon, type, static_cast<T>(std::is_signed_v<T> ? -1 : NoneU8), where);
    }

    // The Digimon a record belongs to, by the record's own position.
    YamlNode digimonAt(std::size_t type, std::string_view where) {
        return reference<std::size_t>(ids_.digimon, type, std::nullopt, where);
    }

    YamlNode digimon() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->digimonData.size(); ++i) {
            const DigimonPara& para = tables_->digimonData[i];
            const std::string where = std::format("DIGIMON_DATA[{}]", i);
            YamlNode node = YamlNode::mapping();
            node.set("id", quoted(ids_.digimon[i]));
            node.set("index", number(i));
            node.set("name", text(para.name, where));
            node.set("file_name", i < tables_->digimonFileNames.size()
                                      ? text(tables_->digimonFileNames[i], std::format("PTR_DIGIMON_FILE_NAMES[{}]", i))
                                      : null());
            node.set("bone_count", number(para.boneCount));
            node.set("radius", number(para.radius));
            node.set("height", number(para.height));
            node.set("type", number(para.type));
            node.set("level", number(para.level));
            YamlNode special = YamlNode::sequence();
            for (const std::uint8_t value : para.special) {
                special.append(value == NoneU8 ? null() : number(value));
            }
            node.set("special", std::move(special));
            node.set("drop_item", reference<std::uint8_t>(ids_.items, para.dropItem, NoneU8, where));
            node.set("drop_chance", number(para.dropChance));
            YamlNode moveList = YamlNode::sequence();
            for (const std::uint8_t move : para.moves) {
                moveList.append(reference<std::uint8_t>(ids_.moves, move, NoneU8, where));
            }
            node.set("moves", std::move(moveList));
            list.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("digimon", std::move(list));
        return root;
    }

    YamlNode skeletons() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->digimonSkeletons.size() && i < ids_.digimon.size(); ++i) {
            YamlNode node = YamlNode::mapping();
            node.set("digimon", quoted(ids_.digimon[i]));
            node.set("index", number(i));
            YamlNode bones = YamlNode::sequence();
            for (const SkeletonBone& bone : tables_->digimonSkeletons[i]) {
                YamlNode boneNode = YamlNode::mapping();
                boneNode.set("object", number(bone.objIndex));
                boneNode.set("parent", number(bone.parentIndex));
                bones.append(std::move(boneNode));
            }
            node.set("bones", std::move(bones));
            list.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("skeletons", std::move(list));
        return root;
    }

    YamlNode moves() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->moveData.size(); ++i) {
            const Move& move = tables_->moveData[i];
            YamlNode node = YamlNode::mapping();
            node.set("id", quoted(ids_.moves[i]));
            node.set("index", number(i));
            node.set("name", i < tables_->moveNames.size()
                                 ? text(tables_->moveNames[i], std::format("MOVE_NAMES[{}]", i))
                                 : null());
            node.set("distance", number(move.distance));
            node.set("power", number(move.power));
            node.set("mp_cost", number(move.mpCost));
            node.set("iframes", number(move.iframes));
            node.set("range", number(move.range));
            node.set("special", number(move.special));
            node.set("status", number(move.status));
            node.set("accuracy", number(move.accuracy));
            node.set("status_chance", number(move.statusChance));
            node.set("unknown_0d", number(move.unknown0D));
            node.set("unknown_0e", number(move.unknown0E));
            node.set("unknown_0f", number(move.unknown0F));
            list.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("moves", std::move(list));
        return root;
    }

    YamlNode items() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->itemPara.size(); ++i) {
            const Item& item = tables_->itemPara[i];
            YamlNode node = YamlNode::mapping();
            node.set("id", quoted(ids_.items[i]));
            node.set("index", number(i));
            node.set("name", text(item.name, std::format("ITEM_PARA[{}]", i)));
            node.set("description", i < tables_->itemDescriptions.size()
                                        ? text(tables_->itemDescriptions[i], std::format("ITEM_DESCS[{}]", i))
                                        : null());
            node.set("value", number(item.value));
            node.set("merit_value", number(item.meritValue));
            node.set("sorting_value", number(item.sortingValue));
            node.set("item_color", number(item.itemColor));
            node.set("droppable", number(item.droppable));
            node.set("unknown_1e", number(item.unknown1E));
            list.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("items", std::move(list));
        return root;
    }

    YamlNode evolution() {
        YamlNode requirements = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->evolutionRequirements.size(); ++i) {
            const EvoRequirements& r = tables_->evolutionRequirements[i];
            const std::string where = std::format("EVOLUTION_REQS[{}]", i);
            YamlNode node = YamlNode::mapping();
            node.set("target", digimonAt(i, where));
            node.set("index", number(i));
            node.set("digimon", digimonRef(r.digimon, where));
            node.set("hp", number(r.hp));
            node.set("mp", number(r.mp));
            node.set("offense", number(r.offense));
            node.set("defense", number(r.defense));
            node.set("speed", number(r.speed));
            node.set("brain", number(r.brain));
            node.set("care", number(r.care));
            node.set("weight", number(r.weight));
            node.set("discipline", number(r.discipline));
            node.set("happiness", number(r.happiness));
            node.set("battles", number(r.battles));
            node.set("techs", number(r.techs));
            node.set("flags", number(r.flags));
            requirements.append(std::move(node));
        }
        YamlNode gains = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->evolutionStatsGains.size(); ++i) {
            const EvoStatsGains& g = tables_->evolutionStatsGains[i];
            const std::string where = std::format("EVOLUTION_STATS_GAINS[{}]", i);
            YamlNode node = YamlNode::mapping();
            node.set("target", digimonAt(i, where));
            node.set("index", number(i));
            node.set("hp", number(g.hp));
            node.set("mp", number(g.mp));
            node.set("offense", number(g.offense));
            node.set("defense", number(g.defense));
            node.set("speed", number(g.speed));
            node.set("brains", number(g.brains));
            node.set("target_digimon", digimonRef(g.targetDigimon, where));
            gains.append(std::move(node));
        }
        YamlNode paths = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->evolutionPaths.size(); ++i) {
            const EvolutionPath& path = tables_->evolutionPaths[i];
            const std::string where = std::format("EVOLUTION_PATHS[{}]", i);
            YamlNode node = YamlNode::mapping();
            node.set("digimon", digimonAt(i + 1, where));
            node.set("index", number(i));
            YamlNode from = YamlNode::sequence();
            for (const std::int8_t id : path.from) {
                from.append(digimonRef(id, where));
            }
            YamlNode to = YamlNode::sequence();
            for (const std::int8_t id : path.to) {
                to.append(digimonRef(id, where));
            }
            node.set("from", std::move(from));
            node.set("to", std::move(to));
            paths.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("requirements", std::move(requirements));
        root.set("stats_gains", std::move(gains));
        root.set("paths", std::move(paths));
        return root;
    }

    YamlNode raise() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->raiseData.size(); ++i) {
            const RaiseData& r = tables_->raiseData[i];
            const std::string where = std::format("RAISE_DATA[{}]", i);
            YamlNode node = YamlNode::mapping();
            node.set("digimon", digimonAt(i, where));
            node.set("index", number(i));
            node.set("hunger_times", numbers(r.hungerTimes));
            node.set("energy_cap", number(r.energyCap));
            node.set("energy_threshold", number(r.energyThreshold));
            node.set("energy_usage", number(r.energyUsage));
            node.set("poop_timer", number(r.poopTimer));
            node.set("unknown_0e", number(r.unknown0E));
            node.set("poop_size", number(r.poopSize));
            node.set("favorite_food", reference<std::uint8_t>(ids_.items, r.favoriteFood, NoneU8, where));
            node.set("sleep_cycle", number(r.sleepCycle));
            node.set("favored_region", number(r.favoredRegion));
            node.set("training_type", number(r.trainingType));
            node.set("default_weight", number(r.defaultWeight));
            node.set("view", yaml::vector3(r.viewX, r.viewY, r.viewZ));
            list.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("raise", std::move(list));
        return root;
    }

    YamlNode cards() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->cardData.size(); ++i) {
            const CardData& card = tables_->cardData[i];
            YamlNode node = YamlNode::mapping();
            node.set("index", number(i));
            node.set("digimon", digimonRef(card.digimonId, std::format("CARD_DATA[{}]", i)));
            node.set("sprite_id", number(card.spriteId));
            node.set("merit_value", number(card.meritValue));
            list.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("prices", numbers(tables_->cardPrices));
        root.set("cards", std::move(list));
        return root;
    }

    YamlNode tournaments() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->tournamentNames.size(); ++i) {
            YamlNode node = YamlNode::mapping();
            node.set("id", quoted(ids_.tournaments[i]));
            node.set("index", number(i));
            node.set("name", text(tables_->tournamentNames[i], std::format("TOURNAMENT_NAMES[{}]", i)));
            node.set("grade", i < tables_->tournamentGrades.size()
                                  ? text(tables_->tournamentGrades[i], std::format("TOURNAMENT_GRADES[{}]", i))
                                  : null());
            list.append(std::move(node));
        }
        YamlNode schedule = YamlNode::sequence();
        for (std::size_t day = 0; day < tables_->tournamentSchedule.size(); ++day) {
            YamlNode held = YamlNode::sequence();
            for (const std::uint8_t id : tables_->tournamentSchedule[day]) {
                if (id == NoneU8) {
                    break;
                }
                held.append(reference<std::uint8_t>(ids_.tournaments, id, std::nullopt,
                                                    std::format("TOURNAMENT_SCHEDULE[{}]", day)));
            }
            YamlNode node = YamlNode::mapping();
            node.set("day", number(day));
            node.set("tournaments", std::move(held));
            schedule.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("tournaments", std::move(list));
        root.set("schedule", std::move(schedule));
        root.set("medal_ids", numbers(tables_->tournamentMedalIds));
        return root;
    }

    YamlNode medals() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->medalNames.size(); ++i) {
            YamlNode node = YamlNode::mapping();
            node.set("id", quoted(ids_.medals[i]));
            node.set("index", number(i));
            node.set("name", text(tables_->medalNames[i], std::format("MEDAL_NAMES[{}]", i)));
            YamlNode lines = YamlNode::sequence();
            for (std::size_t line = i * MedalDescriptionLines;
                 line < (i + 1) * MedalDescriptionLines && line < tables_->medalDescriptions.size(); ++line) {
                lines.append(text(tables_->medalDescriptions[line], std::format("MEDAL_DESCRIPTIONS[{}]", line)));
            }
            node.set("description", std::move(lines));
            list.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("medals", std::move(list));
        return root;
    }

    YamlNode music() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->bgmTrackNames.size(); ++i) {
            YamlNode node = YamlNode::mapping();
            node.set("id", quoted(ids_.bgmTracks[i]));
            node.set("index", number(i));
            node.set("name", text(tables_->bgmTrackNames[i], std::format("BGM_TRACK_NAMES[{}]", i)));
            list.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("bgm_tracks", std::move(list));
        root.set("enemy_music_mapping", numbers(tables_->enemyMusicMapping));
        return root;
    }

    YamlNode shops() {
        YamlNode maps = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->shopMaps.size(); ++i) {
            maps.append(reference<std::int16_t>(ids_.maps, tables_->shopMaps[i], std::nullopt,
                                                std::format("SHOP_MAPS[{}]", i)));
        }
        YamlNode keepers = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->shopkeepDigimonTypes.size(); ++i) {
            const std::uint8_t type = tables_->shopkeepDigimonTypes[i];
            if (type == NoneU8) {
                break;
            }
            keepers.append(reference<std::uint8_t>(ids_.digimon, type, std::nullopt,
                                                   std::format("SHOPKEEP_DIGIMON_TYPES[{}]", i)));
        }
        YamlNode root = YamlNode::mapping();
        root.set("shop_maps", std::move(maps));
        root.set("shopkeeper_digimon", std::move(keepers));
        return root;
    }

    // A 1-based index into another table (0 = none) as a 0-based index or null.
    static YamlNode oneBased(std::int8_t id) { return id > 0 ? number(id - 1) : null(); }

    YamlNode maps() {
        YamlNode list = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->mapEntries.size(); ++i) {
            const MapEntry& entry = tables_->mapEntries[i];
            const std::string where = std::format("MAP_ENTRIES[{}]", i);
            YamlNode node = YamlNode::mapping();
            node.set("id", quoted(ids_.maps[i]));
            node.set("index", number(i));
            node.set("filename", text(entry.filename, where));
            node.set("images_8bpp", number(entry.num8bppImages));
            node.set("images_4bpp", number(entry.num4bppImages));
            node.set("flags", number(entry.flags));
            node.set("sound_id", number(entry.flags & MapEntryFlags::SoundIdMask));
            node.set("fixed_time_of_day", boolean((entry.flags & MapEntryFlags::NoTimeOfDay) != 0));
            node.set("has_npcs", boolean((entry.flags & MapEntryFlags::HasNpcs) != 0));
            node.set("doors", oneBased(entry.doorsId));
            node.set("toilet", oneBased(entry.toiletId));
            node.set("loading_name", number(entry.loadingName));
            list.append(std::move(node));
        }
        YamlNode doors = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->doorMapData.size(); ++i) {
            const DoorMapData& door = tables_->doorMapData[i];
            YamlNode slots = YamlNode::sequence();
            for (std::size_t slot = 0; slot < door.modelId.size(); ++slot) {
                YamlNode node = YamlNode::mapping();
                node.set("model_id", door.modelId[slot] < 0 ? null() : number(door.modelId[slot]));
                node.set("position", yaml::vector3(door.posX[slot], door.posY[slot], door.posZ[slot]));
                node.set("rotation", number(door.rotation[slot]));
                slots.append(std::move(node));
            }
            YamlNode node = YamlNode::mapping();
            node.set("index", number(i));
            node.set("doors", std::move(slots));
            doors.append(std::move(node));
        }
        YamlNode toilets = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->toiletData.size(); ++i) {
            const ToiletData& toilet = tables_->toiletData[i];
            YamlNode node = YamlNode::mapping();
            node.set("index", number(i));
            node.set("x1", number(toilet.posX1));
            node.set("z1", number(toilet.posY1));
            node.set("x2", number(toilet.posX2));
            node.set("z2", number(toilet.posY2));
            toilets.append(std::move(node));
        }
        YamlNode names = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->mapNames.size(); ++i) {
            names.append(text(tables_->mapNames[i], std::format("MAP_NAME_PTR[{}]", i)));
        }
        YamlNode lights = YamlNode::sequence();
        for (std::size_t i = 0; i < tables_->mapLightUpdateData.size(); ++i) {
            const MapLightUpdateData& light = tables_->mapLightUpdateData[i];
            YamlNode node = YamlNode::mapping();
            node.set("map", reference<std::uint8_t>(ids_.maps, light.mapId, std::nullopt,
                                                    std::format("MAP_LIGHT_UPDATE_DATA[{}]", i)));
            node.set("mode", number(light.mode));
            node.set("start_id", number(light.startId));
            node.set("count", number(light.count));
            node.set("trigger", light.trigger == AlwaysTrigger ? null() : number(light.trigger));
            lights.append(std::move(node));
        }
        YamlNode root = YamlNode::mapping();
        root.set("maps", std::move(list));
        root.set("doors", std::move(doors));
        root.set("toilets", std::move(toilets));
        root.set("map_names", std::move(names));
        root.set("light_updates", std::move(lights));
        return root;
    }

    const ExecutableTables* tables_;
    TableIds ids_;
    std::vector<std::string> notes_;
};

} // namespace

std::string escapeGameText(std::string_view raw) {
    std::string escaped;
    escaped.reserve(raw.size());
    for (const char c : raw) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\') {
            escaped += "\\\\";
        } else if (byte >= 0x20 && byte < 0x7F) {
            escaped += c;
        } else {
            escaped += std::format("\\x{:02X}", byte);
        }
    }
    return escaped;
}

std::vector<std::string> makeIds(std::span<const std::string> names, std::string_view prefix) {
    std::vector<std::string> ids;
    ids.reserve(names.size());
    std::set<std::string, std::less<>> used;
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::string id;
        bool pendingSeparator = false;
        for (const char c : names[i]) {
            const bool letter = c >= 'a' && c <= 'z';
            const bool digit = c >= '0' && c <= '9';
            const bool upper = c >= 'A' && c <= 'Z';
            if (letter || digit || upper) {
                if (pendingSeparator && !id.empty()) {
                    id += '_';
                }
                pendingSeparator = false;
                id += upper ? static_cast<char>(c - 'A' + 'a') : c;
            } else {
                pendingSeparator = true;
            }
        }
        if (id.empty()) {
            id = std::format("{}_{}", prefix, i);
        }
        while (used.contains(id)) {
            id = std::format("{}_{}", id, i);
        }
        used.insert(id);
        ids.push_back(std::move(id));
    }
    return ids;
}

TableIds makeTableIds(const ExecutableTables& tables) {
    std::vector<std::string> digimonNames = tables.digimonFileNames;
    digimonNames.resize(tables.digimonData.size());
    std::vector<std::string> mapNames;
    mapNames.reserve(tables.mapEntries.size());
    std::ranges::transform(tables.mapEntries, std::back_inserter(mapNames),
                           [](const MapEntry& entry) { return entry.filename; });
    std::vector<std::string> itemNames;
    itemNames.reserve(tables.itemPara.size());
    std::ranges::transform(tables.itemPara, std::back_inserter(itemNames), [](const Item& item) { return item.name; });
    std::vector<std::string> moveNames = tables.moveNames;
    moveNames.resize(tables.moveData.size());
    return TableIds{
        .digimon = makeIds(digimonNames, "digimon"),
        .moves = makeIds(moveNames, "move"),
        .items = makeIds(itemNames, "item"),
        .maps = makeIds(mapNames, "map"),
        .tournaments = makeIds(tables.tournamentNames, "tournament"),
        .medals = makeIds(tables.medalNames, "medal"),
        .bgmTracks = makeIds(tables.bgmTrackNames, "bgm"),
    };
}

TablesYaml tablesToYaml(const ExecutableTables& tables) {
    return Converter{tables}.convert();
}

} // namespace dwe::formats::exe
