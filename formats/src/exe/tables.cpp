#include <dwe/formats/exe/tables.hpp>

#include <bytes/byte_cursor.hpp>

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

namespace dwe::formats::exe {

namespace {

using bytes::ByteCursor;

constexpr std::size_t InlineNameSize = 20;
constexpr std::size_t MaxTableStringLength = 256;

// Reads count records of stride bytes at the table's address; parse reads one record from a cursor over it.
template <typename T, typename Parse>
core::Result<std::vector<T>> readRecords(const MemoryImage& image, const TableLocation& table, std::string_view name,
                                         std::size_t stride, Parse parse) {
    if (!table.located()) {
        return core::fail(std::format("{} is not located for this version", name));
    }
    auto bytes = image.bytesAt(table.address, std::size_t{table.count} * stride);
    if (!bytes) {
        return core::fail(std::format("{}: {}", name, bytes.error().message));
    }
    std::vector<T> records;
    records.reserve(table.count);
    for (std::size_t i = 0; i < table.count; ++i) {
        ByteCursor cursor{bytes->subspan(i * stride, stride)};
        records.push_back(parse(cursor));
    }
    return records;
}

// Reads a char[size] field: the bytes up to the first NUL.
std::string readFixedString(ByteCursor& cursor, std::size_t size) {
    const auto field = cursor.take(size);
    const auto end = std::ranges::find(field, std::byte{0});
    std::string text;
    std::ranges::transform(field.begin(), end, std::back_inserter(text),
                           [](std::byte b) { return static_cast<char>(b); });
    return text;
}

template <typename T, std::size_t N, typename Read>
std::array<T, N> readArray(Read read) {
    std::array<T, N> values{};
    std::ranges::generate(values, read);
    return values;
}

template <typename T>
core::Result<std::vector<T>> readScalars(const MemoryImage& image, const TableLocation& table, std::string_view name) {
    return readRecords<T>(image, table, name, sizeof(T), [](ByteCursor& c) {
        if constexpr (sizeof(T) == 1) {
            return static_cast<T>(c.u8());
        } else if constexpr (sizeof(T) == 2) {
            return static_cast<T>(c.u16());
        } else {
            return static_cast<T>(c.u32());
        }
    });
}

core::Result<std::vector<std::string>> readNamedStringTable(const MemoryImage& image, const TableLocation& table,
                                                            std::string_view name) {
    auto pointers = readScalars<std::uint32_t>(image, table, name);
    if (!pointers) {
        return std::unexpected{pointers.error()};
    }
    std::vector<std::string> strings;
    strings.reserve(pointers->size());
    for (std::size_t i = 0; i < pointers->size(); ++i) {
        if ((*pointers)[i] == 0) {
            strings.emplace_back(); // the US MOVE_NAMES ends with a null pointer for the last move
            continue;
        }
        auto text = image.stringAt((*pointers)[i], MaxTableStringLength);
        if (!text) {
            return core::fail(std::format("{}[{}]: {}", name, i, text.error().message));
        }
        strings.push_back(std::move(*text));
    }
    return strings;
}

// Assigns a table read by a reader to a field, or keeps the error.
template <typename T>
bool store(core::Result<T> read, T& field, core::Error& error) {
    if (!read) {
        error = std::move(read.error());
        return false;
    }
    field = std::move(*read);
    return true;
}

} // namespace

core::Result<std::vector<std::string>> readStringTable(const MemoryImage& image, const TableLocation& table) {
    return readNamedStringTable(image, table, std::format("string table at 0x{:08X}", table.address));
}

core::Result<std::vector<DigimonPara>> readDigimonData(const MemoryImage& image, const ExecutableLayout& layout) {
    const std::size_t nameSize = layout.inlineNames ? InlineNameSize : 0;
    return readRecords<DigimonPara>(image, layout.digimonData, "DIGIMON_DATA", nameSize + 0x20, [&](ByteCursor& c) {
        DigimonPara para;
        para.name = readFixedString(c, nameSize);
        para.boneCount = c.i32();
        para.radius = c.i16();
        para.height = c.i16();
        para.type = c.u8();
        para.level = c.u8();
        para.special = readArray<std::uint8_t, 3>([&] { return c.u8(); });
        para.dropItem = c.u8();
        para.dropChance = c.u8();
        para.moves = readArray<std::uint8_t, 16>([&] { return c.u8(); });
        return para;
    });
}

core::Result<std::vector<std::vector<SkeletonBone>>> readDigimonSkeletons(const MemoryImage& image,
                                                                          const ExecutableLayout& layout,
                                                                          const std::vector<DigimonPara>& digimonData) {
    auto pointers = readScalars<std::uint32_t>(image, layout.digimonSkeletons, "DIGIMON_SKELETONS");
    if (!pointers) {
        return std::unexpected{pointers.error()};
    }
    if (pointers->size() != digimonData.size()) {
        return core::fail(std::format("DIGIMON_SKELETONS has {} entries but DIGIMON_DATA has {}", pointers->size(),
                                      digimonData.size()));
    }
    std::vector<std::vector<SkeletonBone>> skeletons;
    skeletons.reserve(pointers->size());
    for (std::size_t type = 0; type < pointers->size(); ++type) {
        const std::int32_t boneCount = digimonData[type].boneCount;
        if (boneCount < 0) {
            return core::fail(std::format("DIGIMON_DATA[{}] has a negative bone count", type));
        }
        std::vector<SkeletonBone>& skeleton = skeletons.emplace_back();
        if (boneCount == 0) {
            continue; // unused types have no bones and a null pointer
        }
        auto bones = image.bytesAt((*pointers)[type], static_cast<std::size_t>(boneCount) * 2);
        if (!bones) {
            return core::fail(std::format("DIGIMON_SKELETONS[{}]: {}", type, bones.error().message));
        }
        ByteCursor cursor{*bones};
        for (std::int32_t bone = 0; bone < boneCount; ++bone) {
            skeleton.push_back({.objIndex = cursor.i8(), .parentIndex = cursor.i8()});
        }
    }
    return skeletons;
}

core::Result<std::vector<Move>> readMoveData(const MemoryImage& image, const ExecutableLayout& layout) {
    return readRecords<Move>(image, layout.moveData, "MOVE_DATA", 16, [](ByteCursor& c) {
        return Move{.distance = c.i32(),
                    .power = c.i16(),
                    .mpCost = c.u8(),
                    .iframes = c.u8(),
                    .range = c.u8(),
                    .special = c.u8(),
                    .status = c.u8(),
                    .accuracy = c.u8(),
                    .statusChance = c.u8(),
                    .unknown0D = c.u8(),
                    .unknown0E = c.u8(),
                    .unknown0F = c.u8()};
    });
}

core::Result<std::vector<Item>> readItemPara(const MemoryImage& image, const ExecutableLayout& layout) {
    const std::size_t nameSize = layout.inlineNames ? InlineNameSize : 0;
    return readRecords<Item>(image, layout.itemPara, "ITEM_PARA", nameSize + 12, [&](ByteCursor& c) {
        Item item;
        item.name = readFixedString(c, nameSize);
        item.value = c.i32();
        item.meritValue = c.u16();
        item.sortingValue = c.i16();
        item.itemColor = c.u8();
        item.droppable = c.u8();
        item.unknown1E = c.u16();
        return item;
    });
}

core::Result<std::vector<EvoRequirements>> readEvolutionRequirements(const MemoryImage& image,
                                                                     const ExecutableLayout& layout) {
    return readRecords<EvoRequirements>(image, layout.evolutionRequirements, "EVOLUTION_REQS", 28, [](ByteCursor& c) {
        return EvoRequirements{.digimon = c.i16(),
                               .hp = c.i16(),
                               .mp = c.i16(),
                               .offense = c.i16(),
                               .defense = c.i16(),
                               .speed = c.i16(),
                               .brain = c.i16(),
                               .care = c.i16(),
                               .weight = c.i16(),
                               .discipline = c.i16(),
                               .happiness = c.i16(),
                               .battles = c.i16(),
                               .techs = c.i16(),
                               .flags = c.i8()};
    });
}

core::Result<std::vector<EvoStatsGains>> readEvolutionStatsGains(const MemoryImage& image,
                                                                 const ExecutableLayout& layout) {
    return readRecords<EvoStatsGains>(image, layout.evolutionStatsGains, "EVOLUTION_STATS_GAINS", 14,
                                      [](ByteCursor& c) {
                                          return EvoStatsGains{.hp = c.i16(),
                                                               .mp = c.i16(),
                                                               .offense = c.i16(),
                                                               .defense = c.i16(),
                                                               .speed = c.i16(),
                                                               .brains = c.i16(),
                                                               .targetDigimon = c.i16()};
                                      });
}

core::Result<std::vector<EvolutionPath>> readEvolutionPaths(const MemoryImage& image, const ExecutableLayout& layout) {
    return readRecords<EvolutionPath>(image, layout.evolutionPaths, "EVOLUTION_PATHS", 11, [](ByteCursor& c) {
        EvolutionPath path;
        path.from = readArray<std::int8_t, 5>([&] { return c.i8(); });
        path.to = readArray<std::int8_t, 6>([&] { return c.i8(); });
        return path;
    });
}

core::Result<std::vector<RaiseData>> readRaiseData(const MemoryImage& image, const ExecutableLayout& layout) {
    return readRecords<RaiseData>(image, layout.raiseData, "RAISE_DATA", 28, [](ByteCursor& c) {
        RaiseData raise;
        raise.hungerTimes = readArray<std::int8_t, 8>([&] { return c.i8(); });
        raise.energyCap = c.u8();
        raise.energyThreshold = c.u8();
        raise.energyUsage = c.u8();
        c.skip(1); // alignment of poopTimer
        raise.poopTimer = c.i16();
        raise.unknown0E = c.i16();
        raise.poopSize = c.u8();
        raise.favoriteFood = c.u8();
        raise.sleepCycle = c.u8();
        raise.favoredRegion = c.u8();
        raise.trainingType = c.u8();
        raise.defaultWeight = c.u8();
        raise.viewX = c.i16();
        raise.viewY = c.i16();
        raise.viewZ = c.i16();
        return raise;
    });
}

core::Result<std::vector<CardData>> readCardData(const MemoryImage& image, const ExecutableLayout& layout) {
    return readRecords<CardData>(image, layout.cardData, "CARD_DATA", 4, [](ByteCursor& c) {
        return CardData{.digimonId = c.u8(), .spriteId = c.u8(), .meritValue = c.i16()};
    });
}

core::Result<std::vector<MapEntry>> readMapEntries(const MemoryImage& image, const ExecutableLayout& layout) {
    return readRecords<MapEntry>(image, layout.mapEntries, "MAP_ENTRIES", 16, [](ByteCursor& c) {
        MapEntry entry;
        entry.filename = readFixedString(c, 10);
        entry.num8bppImages = c.i8();
        entry.num4bppImages = c.i8();
        entry.flags = c.u8();
        entry.doorsId = c.i8();
        entry.toiletId = c.i8();
        entry.loadingName = c.i8();
        return entry;
    });
}

core::Result<std::vector<DoorMapData>> readDoorMapData(const MemoryImage& image, const ExecutableLayout& layout) {
    return readRecords<DoorMapData>(image, layout.doorMapData, "DOOR_MAPDATA", 54, [](ByteCursor& c) {
        DoorMapData door;
        door.modelId = readArray<std::int8_t, 6>([&] { return c.i8(); });
        const auto readSix = [&] { return readArray<std::int16_t, 6>([&] { return c.i16(); }); };
        door.posX = readSix();
        door.posY = readSix();
        door.posZ = readSix();
        door.rotation = readSix();
        return door;
    });
}

core::Result<std::vector<ToiletData>> readToiletData(const MemoryImage& image, const ExecutableLayout& layout) {
    return readRecords<ToiletData>(image, layout.toiletData, "TOILET_DATA", 8, [](ByteCursor& c) {
        return ToiletData{.posX1 = c.i16(), .posY1 = c.i16(), .posX2 = c.i16(), .posY2 = c.i16()};
    });
}

core::Result<std::vector<TransportTarget>> readTransportTargets(const MemoryImage& image,
                                                                const ExecutableLayout& layout) {
    return readRecords<TransportTarget>(
        image, layout.transportTargets, "BIRDRA_TRANSPORT_TARGETS", 8, [](ByteCursor& c) {
            return TransportTarget{.mapId = c.u8(), .exit = c.u8(), .trigger = c.u16(), .cost = c.u32()};
        });
}

core::Result<std::vector<MapLightUpdateData>> readMapLightUpdateData(const MemoryImage& image,
                                                                     const ExecutableLayout& layout) {
    return readRecords<MapLightUpdateData>(
        image, layout.mapLightUpdateData, "MAP_LIGHT_UPDATE_DATA", 8, [](ByteCursor& c) {
            return MapLightUpdateData{
                .mapId = c.u8(), .mode = c.u8(), .startId = c.i16(), .count = c.i16(), .trigger = c.u16()};
        });
}

core::Result<std::vector<MapSoundPara>> readMapSoundPara(const MemoryImage& image, const ExecutableLayout& layout) {
    return readRecords<MapSoundPara>(image, layout.mapSoundPara, "MAP_SOUND_PARA", 4, [](ByteCursor& c) {
        return MapSoundPara{.sectorId = c.u16(), .sectorCount = c.u16()};
    });
}

core::Result<std::vector<JukeboxTrack>> readJukeboxTracks(const MemoryImage& image, const ExecutableLayout& layout) {
    return readRecords<JukeboxTrack>(image, layout.jukeboxTracks, "MAIN_D_801303B8", 2,
                                     [](ByteCursor& c) { return JukeboxTrack{.font = c.u8(), .track = c.u8()}; });
}

core::Result<ExecutableTables> readExecutableTables(const MemoryImage& image, const ExecutableLayout& layout) {
    ExecutableTables tables;
    core::Error error;
    const auto strings = [&](const TableLocation& table, std::string_view name) {
        return readNamedStringTable(image, table, name);
    };
    const bool read =
        store(readDigimonData(image, layout), tables.digimonData, error) &&
        store(strings(layout.digimonFileNames, "PTR_DIGIMON_FILE_NAMES"), tables.digimonFileNames, error) &&
        store(readDigimonSkeletons(image, layout, tables.digimonData), tables.digimonSkeletons, error) &&
        store(readMoveData(image, layout), tables.moveData, error) &&
        store(strings(layout.moveNames, "MOVE_NAMES"), tables.moveNames, error) &&
        store(readRecords<std::array<std::uint8_t, 3>>(
                  image, layout.moveLearnChances, "MOVE_LEARN_CHANCES", 3,
                  [](ByteCursor& c) { return readArray<std::uint8_t, 3>([&] { return c.u8(); }); }),
              tables.moveLearnChances, error) &&
        store(readItemPara(image, layout), tables.itemPara, error) &&
        store(strings(layout.itemDescriptions, "ITEM_DESCS"), tables.itemDescriptions, error) &&
        store(readEvolutionRequirements(image, layout), tables.evolutionRequirements, error) &&
        store(readEvolutionStatsGains(image, layout), tables.evolutionStatsGains, error) &&
        store(readEvolutionPaths(image, layout), tables.evolutionPaths, error) &&
        store(readRaiseData(image, layout), tables.raiseData, error) &&
        store(readCardData(image, layout), tables.cardData, error) &&
        store(readScalars<std::int32_t>(image, layout.cardPrices, "CARD_PRICES"), tables.cardPrices, error) &&
        store(strings(layout.tournamentNames, "TOURNAMENT_NAMES"), tables.tournamentNames, error) &&
        store(strings(layout.tournamentGrades, "TOURNAMENT_GRADES"), tables.tournamentGrades, error) &&
        store(readRecords<std::array<std::uint8_t, 6>>(
                  image, layout.tournamentSchedule, "TOURNAMENT_SCHEDULE", 6,
                  [](ByteCursor& c) { return readArray<std::uint8_t, 6>([&] { return c.u8(); }); }),
              tables.tournamentSchedule, error) &&
        store(readScalars<std::uint16_t>(image, layout.tournamentMedalIds, "TOURNAMENT_MEDAL_IDS"),
              tables.tournamentMedalIds, error) &&
        store(strings(layout.medalNames, "MEDAL_NAMES"), tables.medalNames, error) &&
        store(strings(layout.medalDescriptions, "MEDAL_DESCRIPTIONS"), tables.medalDescriptions, error) &&
        store(strings(layout.bgmTrackNames, "BGM_TRACK_NAMES"), tables.bgmTrackNames, error) &&
        store(readScalars<std::uint8_t>(image, layout.enemyMusicMapping, "ENEMY_MUSIC_MAPPING"),
              tables.enemyMusicMapping, error) &&
        store(readScalars<std::int16_t>(image, layout.shopMaps, "SHOP_MAPS"), tables.shopMaps, error) &&
        store(readScalars<std::uint8_t>(image, layout.shopkeepDigimonTypes, "SHOPKEEP_DIGIMON_TYPES"),
              tables.shopkeepDigimonTypes, error) &&
        store(readMapEntries(image, layout), tables.mapEntries, error) &&
        store(strings(layout.mapNames, "MAP_NAME_PTR"), tables.mapNames, error) &&
        store(readDoorMapData(image, layout), tables.doorMapData, error) &&
        store(readToiletData(image, layout), tables.toiletData, error) &&
        store(readMapLightUpdateData(image, layout), tables.mapLightUpdateData, error) &&
        store(readScalars<std::uint8_t>(image, layout.digimonVlallSoundIds, "DIGIMON_VLALL_SOUND_ID"),
              tables.digimonVlallSoundIds, error) &&
        store(readScalars<std::uint8_t>(image, layout.digimonVballSoundIds, "DIGIMON_VBALL_SOUND_ID"),
              tables.digimonVballSoundIds, error) &&
        store(readMapSoundPara(image, layout), tables.mapSoundPara, error) &&
        store(readJukeboxTracks(image, layout), tables.jukeboxTracks, error) &&
        store(readItemEffects(image, layout), tables.itemEffects, error) &&
        store(readTransportTargets(image, layout), tables.transportTargets, error);
    if (!read) {
        return std::unexpected{std::move(error)};
    }
    return tables;
}

} // namespace dwe::formats::exe
