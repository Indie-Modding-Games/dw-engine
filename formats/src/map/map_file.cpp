#include <dwe/formats/map/map_file.hpp>

#include <bytes/byte_cursor.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace dwe::formats::map {

namespace {

using bytes::ByteCursor;

// The game's fixed arrays: MAP_TILES[35], MapTileData.objects[188], NPC_ENTITIES[8] and MapDigimonPath.waypoints[8].
constexpr std::size_t MaxTiles = 35;
constexpr std::size_t MaxInstances = 188;
constexpr std::size_t MaxNpcs = 8;
constexpr std::size_t MaxWaypoints = 8;
constexpr std::size_t CollisionBytes = CollisionSize * CollisionSize;
constexpr int MaxImagesPerKind = 127; // num8bppImages and num4bppImages are int8

template <std::size_t N>
std::array<std::int16_t, N> readInt16s(ByteCursor& cursor) {
    std::array<std::int16_t, N> values{};
    std::ranges::generate(values, [&] { return cursor.i16(); });
    return values;
}

core::Result<MapSetup> readSetup(std::span<const std::byte> section) {
    ByteCursor c{section};
    MapSetup setup;
    setup.vpx = c.i32();
    setup.vpy = c.i32();
    setup.vpz = c.i32();
    setup.vrx = c.i32();
    setup.vry = c.i32();
    setup.vrz = c.i32();
    for (MapLight& light : setup.lights) {
        light = {.vx = c.i32(), .vy = c.i32(), .vz = c.i32(), .r = c.i32(), .g = c.i32(), .b = c.i32()};
    }
    setup.ambientR = c.i32();
    setup.ambientG = c.i32();
    setup.ambientB = c.i32();
    setup.projection = c.i32();
    std::ranges::generate(setup.regionA, [&] { return c.i32(); });
    std::ranges::generate(setup.regionB, [&] { return c.i32(); });
    setup.width = c.i32();
    setup.height = c.i32();
    if (c.overrun()) {
        return core::fail("the setup section is shorter than its fixed part");
    }
    if (setup.width < 0 || setup.height < 0 ||
        std::int64_t{setup.width} * setup.height > static_cast<std::int64_t>(MaxTiles)) {
        return core::fail(std::format("the tile grid is {} x {}; the game holds at most {} tiles", setup.width,
                                      setup.height, MaxTiles));
    }
    setup.tiles.resize(static_cast<std::size_t>(setup.width) * static_cast<std::size_t>(setup.height));
    std::ranges::generate(setup.tiles, [&] { return c.i32(); });
    if (c.overrun()) {
        return core::fail("the setup section ends inside the tile grid");
    }
    return setup;
}

core::Result<MapObjects> readObjects(std::span<const std::byte> section) {
    ByteCursor c{section};
    MapObjects objects;
    const std::int16_t spriteCount = c.i16();
    // Not capped at LOCAL_MAP_OBJECTS[180]: one US map has more sprites and the original writes past the array.
    if (spriteCount < 0) {
        return core::fail(std::format("{} sprites", spriteCount));
    }
    for (std::int16_t i = 0; i < spriteCount && !c.overrun(); ++i) {
        objects.sprites.push_back({.texX = c.i16(),
                                   .texY = c.i16(),
                                   .width = c.i16(),
                                   .height = c.i16(),
                                   .someX = c.i16(),
                                   .someY = c.i16(),
                                   .someZ = c.i16(),
                                   .clut = c.i16(),
                                   .transparency = c.i16()});
    }
    const std::int16_t instanceCount = c.i16();
    if (instanceCount < 0 || std::cmp_greater(instanceCount, MaxInstances)) {
        return core::fail(std::format("{} object instances; the game holds 0 to {}", instanceCount, MaxInstances));
    }
    for (std::int16_t i = 0; i < instanceCount && !c.overrun(); ++i) {
        MapObjectInstance instance;
        instance.animSprites = readInt16s<8>(c);
        instance.animTimes = readInt16s<8>(c);
        instance.x = c.i16();
        instance.y = c.i16();
        instance.flag = c.i16();
        objects.instances.push_back(instance);
    }
    if (c.overrun()) {
        return core::fail("the objects section ends inside a record");
    }
    return objects;
}

core::Result<MapNpc> readNpc(ByteCursor& c) {
    MapNpc npc;
    npc.typeId = c.i16();
    npc.followMode = c.i16();
    npc.posX = c.i16();
    npc.posY = c.i16();
    npc.posZ = c.i16();
    npc.rotX = c.i16();
    npc.rotY = c.i16();
    npc.rotZ = c.i16();
    npc.trackingRange = c.i16();
    npc.unknown12 = c.i16();
    npc.scriptId = c.i16();
    npc.hp = c.i16();
    npc.mp = c.i16();
    npc.currentHP = c.i16();
    npc.currentMP = c.i16();
    npc.off = c.i16();
    npc.def = c.i16();
    npc.speed = c.i16();
    npc.brain = c.i16();
    npc.bits = c.i16();
    npc.chargeMode = c.i16();
    npc.unknown2A = c.i16();
    npc.moves = readInt16s<4>(c);
    npc.movesPrio = readInt16s<4>(c);
    npc.fleeX = c.i16();
    npc.fleeY = c.i16();
    npc.fleeZ = c.i16();
    const std::int16_t waypointCount = c.i16();
    if (waypointCount < 0 || std::cmp_greater(waypointCount, MaxWaypoints)) {
        return core::fail(std::format("an NPC has {} waypoints; the game holds 0 to {}", waypointCount, MaxWaypoints));
    }
    npc.aiSections = readInt16s<8>(c);
    for (std::int16_t i = 0; i < waypointCount; ++i) {
        npc.waypoints.push_back({.x = c.i16(), .y = c.i16(), .z = c.i16()});
    }
    return npc;
}

core::Result<MapEntities> readEntities(std::span<const std::byte> section, bool hasNpcs) {
    ByteCursor c{section};
    MapEntities entities;
    entities.warps.spawnX = readInt16s<10>(c);
    entities.warps.spawnY = readInt16s<10>(c);
    entities.warps.spawnZ = readInt16s<10>(c);
    entities.warps.rotation = readInt16s<10>(c);
    entities.warps.targetMap = readInt16s<10>(c);
    entities.warps.targetExit = readInt16s<10>(c);
    entities.digimonCount = c.i16();
    if (c.overrun()) {
        return core::fail("the entities section is shorter than its warps");
    }
    if (!hasNpcs) {
        return entities;
    }
    if (entities.digimonCount < 0 || std::cmp_greater(entities.digimonCount, MaxNpcs)) {
        return core::fail(std::format("{} NPCs; the game holds 0 to {}", entities.digimonCount, MaxNpcs));
    }
    for (std::int16_t i = 0; i < entities.digimonCount; ++i) {
        auto npc = readNpc(c);
        if (!npc) {
            return std::unexpected{npc.error()};
        }
        entities.npcs.push_back(std::move(*npc));
    }
    if (c.overrun()) {
        return core::fail("the entities section ends inside an NPC record");
    }
    return entities;
}

} // namespace

core::Result<MapFile> readMapFile(std::span<const std::byte> file, const MapReadOptions& options) {
    if (options.imageCount8 < 0 || options.imageCount4 < 0 || options.imageCount8 > MaxImagesPerKind ||
        options.imageCount4 > MaxImagesPerKind) {
        return core::fail(std::format("invalid image counts {} and {}", options.imageCount8, options.imageCount4));
    }
    const auto imageCount8 = static_cast<std::size_t>(options.imageCount8);
    const std::size_t imageCount = imageCount8 + static_cast<std::size_t>(options.imageCount4);
    const bool hasObjects = imageCount > 0;
    const std::size_t sectionCount = 3 + imageCount + (hasObjects ? 1 : 0);

    ByteCursor header{file};
    std::vector<std::size_t> offsets(sectionCount);
    std::ranges::generate(offsets, [&] { return std::size_t{header.u32()}; });
    if (header.overrun()) {
        return core::fail(std::format("the file is shorter than its table of {} offsets", sectionCount));
    }
    if (offsets.front() < header.offset()) {
        return core::fail("the first section starts inside the table of offsets");
    }
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (offsets[i] > file.size() || (i > 0 && offsets[i] < offsets[i - 1])) {
            return core::fail(
                std::format("section {} starts at 0x{:X}, out of order or past the end of the file (0x{:X})", i,
                            offsets[i], file.size()));
        }
    }
    const auto section = [&](std::size_t i) {
        const std::size_t end = i + 1 < offsets.size() ? offsets[i + 1] : file.size();
        return file.subspan(offsets[i], end - offsets[i]);
    };

    MapFile map;
    std::size_t next = 0;
    auto setup = readSetup(section(next++));
    if (!setup) {
        return core::fail(std::format("setup: {}", setup.error().message));
    }
    map.setup = std::move(*setup);

    for (std::size_t i = 0; i < imageCount; ++i, ++next) {
        const bool eightBit = i < imageCount8;
        map.images.push_back({.bitsPerPixel = static_cast<std::uint8_t>(eightBit ? 8 : 4),
                              .offset = offsets[next],
                              .size = section(next).size()});
    }

    if (hasObjects) {
        auto objects = readObjects(section(next++));
        if (!objects) {
            return core::fail(std::format("objects: {}", objects.error().message));
        }
        map.objects = std::move(*objects);
    }

    auto entities = readEntities(section(next++), options.hasNpcs);
    if (!entities) {
        return core::fail(std::format("entities: {}", entities.error().message));
    }
    map.entities = std::move(*entities);

    const auto collision = section(next);
    if (collision.size() != CollisionBytes) {
        return core::fail(
            std::format("the collision grid has {} bytes instead of {}", collision.size(), CollisionBytes));
    }
    map.collision.resize(CollisionBytes);
    std::ranges::transform(collision, map.collision.begin(),
                           [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return map;
}

std::string mapFilePath(int mapId, std::string_view filename, std::string_view extension) {
    return std::format(R"(\MAP\MAP{}\{}{})", (mapId / 15) + 1, filename, extension);
}

} // namespace dwe::formats::map
