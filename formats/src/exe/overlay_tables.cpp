#include <dwe/formats/exe/overlay_tables.hpp>

#include <bytes/byte_cursor.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace dwe::formats::exe {

namespace {

using bytes::ByteCursor;

constexpr std::size_t PointSize = 4;
constexpr std::size_t FishKindSize = 0x18;
constexpr std::size_t FishingSpotSize = 0x10;
constexpr std::size_t FishingMapSize = 0x38;
constexpr std::size_t CameraPresetSize = 0xC;
constexpr std::size_t MapTrainingSpotsSize = 8;
constexpr std::size_t TrainingSpotSize = 8;
// A bound for lists that end with a marker, far above what any overlay holds.
constexpr std::size_t MaxListLength = 64;
constexpr std::size_t MaxPathLength = 64;
constexpr std::int32_t EndOfMaps = -1;
constexpr std::int16_t EndOfSpots = -1;

// US addresses: dw_decomp config/us/symbols_fish.txt, symbols_std.txt, symbols_vs.txt,
// symbols_trn.txt, symbols_trn2.txt, symbols_btl.txt and symbols.txt (the small tables in the
// executable). Counts: the array declarations in src/fish/fish.c, src/std/std_main.c and
// std_setup.c, src/vs/vs_scene.c and vs_camera.c, src/trn/trn_reward.c, trn_hud.c and trn_slots.c
// and their trn2 copies, and src/btl/battle_effect.c.
constexpr std::array Layouts = {
    OverlayTablesLayout{
        .version = "us",
        .fishing = {.baitChances = {.address = 0x8007A1F4, .count = 6},
                    .biteChances = {.address = 0x8007A3EC, .count = 200},
                    .biteKinds = 6,
                    .biteRows = 33,
                    .firstBiteDepth = 0x26,
                    .kinds = {.address = 0x8007AA28, .count = 7},
                    .maps = {.address = 0x8007AAD0, .count = 2},
                    .spotsPerMap = 6,
                    .shorelines = {ShorelineLocation{.points = {.address = 0x8007A890, .count = 16},
                                                     .axis = 'x',
                                                     .waterTest = 0x800797C8},
                                   ShorelineLocation{.points = {.address = 0x8007A9B0, .count = 7},
                                                     .axis = 'y',
                                                     .waterTest = 0x80079820}}},
        .stadium = {.opponentBaseStats = {.address = 0x800799B4, .count = 24},
                    .grades = {.address = 0x80079C24, .count = 23},
                    .winChances = {.address = 0x8007A58C, .count = 4},
                    .cameraPresets = {.address = 0x8007A390, .count = 9},
                    .arenaModels = {.address = 0x8007A304, .count = 3},
                    .arenaTextures = {.address = 0x8007A338, .count = 3},
                    .arenaCollisions = {.address = 0x8007A358, .count = 3},
                    .arenaModelCounts = {.address = 0x801347FC, .count = 2},
                    .arenaTextureCounts = {.address = 0x801347FE, .count = 2},
                    .bracketRowY = {.address = 0x8007A48C, .count = 8},
                    .bracketHopY = {.address = 0x8007A474, .count = 24},
                    .bracketPaths = {.address = 0x8007A598, .count = 8},
                    .spritePalettes = {.address = 0x8007A49C, .count = 112},
                    .projectileIcons = {.address = 0x8007A50C, .count = 112},
                    .projectilePalettes = {.address = 0x8007A57C, .count = 16}},
        .versus = {.opponentBaseStats = {},
                   .grades = {},
                   .winChances = {},
                   .cameraPresets = {.address = 0x8007063C, .count = 9},
                   .arenaModels = {.address = 0x800705B4, .count = 3},
                   .arenaTextures = {.address = 0x800705FC, .count = 3},
                   .arenaCollisions = {.address = 0x80070630, .count = 3},
                   .arenaModelCounts = {.address = 0x80134A5C, .count = 3},
                   .arenaTextureCounts = {.address = 0x80134A60, .count = 3},
                   .bracketRowY = {},
                   .bracketHopY = {},
                   .bracketPaths = {},
                   .spritePalettes = {},
                   .projectileIcons = {},
                   .projectilePalettes = {}},
        .training = {.animations = {.address = 0x8008F050, .count = 67},
                     .learnableMoves = {.address = 0x8008F14C, .count = 7},
                     .learnableMovesWidth = 8,
                     .learnWeights = {.address = 0x8008F184, .count = 8},
                     .learnWeightsWidth = 3,
                     .slotReels = {.address = 0x8008F1AC, .count = 3},
                     .slotReelWidth = 13,
                     .assistRolls = {.address = 0x8008F2CC, .count = 10},
                     .spots = {.address = 0x8008F28C, .count = 8}},
        .training2 = {.animations = {.address = 0x8008DA20, .count = 67},
                      .learnableMoves = {},
                      .learnableMovesWidth = 0,
                      .learnWeights = {},
                      .learnWeightsWidth = 0,
                      .slotReels = {.address = 0x8008DAA8, .count = 3},
                      .slotReelWidth = 13,
                      .assistRolls = {.address = 0x8008DBC8, .count = 10},
                      .spots = {.address = 0x8008DB88, .count = 8}},
        .battle = {.flattenedShotTypes = {.address = 0x8007383C, .count = 129}},
    },
};

// The bytes of count records of stride bytes at a table's address.
core::Result<std::span<const std::byte>> tableBytes(const MemoryImage& image,
                                                    const TableLocation& table, std::size_t stride,
                                                    std::string_view name) {
  if (!table.located()) {
    return core::fail(std::format("{} is not located for this version", name));
  }
  auto bytes = image.bytesAt(table.address, std::size_t{table.count} * stride);
  if (!bytes) {
    return core::fail(std::format("{}: {}", name, bytes.error().message));
  }
  return *bytes;
}

template <typename T>
core::Result<std::vector<T>> readValues(const MemoryImage& image, const TableLocation& table,
                                        std::string_view name) {
  auto bytes = tableBytes(image, table, sizeof(T), name);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  ByteCursor c{*bytes};
  std::vector<T> values(table.count);
  std::ranges::generate(values, [&c] {
    if constexpr (sizeof(T) == 1) {
      return static_cast<T>(c.u8());
    } else if constexpr (sizeof(T) == 2) {
      return static_cast<T>(c.u16());
    } else {
      return static_cast<T>(c.u32());
    }
  });
  return values;
}

// Reads rows of width values: a T[count][width] array.
template <typename T>
core::Result<std::vector<std::vector<T>>> readRows(const MemoryImage& image,
                                                   const TableLocation& table, std::size_t width,
                                                   std::string_view name) {
  auto flat = readValues<T>(
      image, {.address = table.address, .count = table.count * static_cast<std::uint32_t>(width)},
      name);
  if (!flat) {
    return std::unexpected{flat.error()};
  }
  std::vector<std::vector<T>> rows;
  for (std::size_t row = 0; row < table.count; ++row) {
    const auto first = flat->begin() + static_cast<std::ptrdiff_t>(row * width);
    rows.emplace_back(first, first + static_cast<std::ptrdiff_t>(width));
  }
  return rows;
}

core::Result<std::vector<Point16>> readPoints(const MemoryImage& image, std::uint32_t address,
                                              std::size_t count, std::string_view name) {
  auto bytes = image.bytesAt(address, count * PointSize);
  if (!bytes) {
    return core::fail(std::format("{}: {}", name, bytes.error().message));
  }
  ByteCursor c{*bytes};
  std::vector<Point16> points(count);
  std::ranges::generate(points, [&c] { return Point16{.x = c.i16(), .y = c.i16()}; });
  return points;
}

// A string through a pointer; a null pointer reads as an empty string.
core::Result<std::string> readPath(const MemoryImage& image, std::uint32_t pointer,
                                   std::string_view name) {
  if (pointer == 0) {
    return std::string{};
  }
  auto text = image.stringAt(pointer, MaxPathLength);
  if (!text) {
    return core::fail(std::format("{}: {}", name, text.error().message));
  }
  return std::move(*text);
}

core::Result<std::vector<std::string>>
readPaths(const MemoryImage& image, const TableLocation& table, std::string_view name) {
  auto pointers = readValues<std::uint32_t>(image, table, name);
  if (!pointers) {
    return std::unexpected{pointers.error()};
  }
  std::vector<std::string> paths;
  for (std::size_t i = 0; i < pointers->size(); ++i) {
    auto path = readPath(image, (*pointers)[i], std::format("{}[{}]", name, i));
    if (!path) {
      return std::unexpected{path.error()};
    }
    paths.push_back(std::move(*path));
  }
  return paths;
}

core::Result<FishingSpot> readFishingSpot(const MemoryImage& image, std::uint32_t address,
                                          std::string_view name) {
  auto bytes = image.bytesAt(address, FishingSpotSize);
  if (!bytes) {
    return core::fail(std::format("{}: {}", name, bytes.error().message));
  }
  ByteCursor c{*bytes};
  FishingSpot spot;
  std::ranges::generate(spot.unknown0, [&c] { return c.u8(); });
  spot.unknown4 = c.i32();
  const std::uint32_t points = c.u32();
  const std::uint8_t pointCount = c.u8();
  std::ranges::generate(spot.unknownD, [&c] { return c.u8(); });
  auto read = readPoints(image, points, pointCount, std::format("{}.points", name));
  if (!read) {
    return std::unexpected{read.error()};
  }
  spot.points = std::move(*read);
  return spot;
}

core::Result<FishingMap> readFishingMap(const MemoryImage& image, std::span<const std::byte> record,
                                        const FishingLayout& layout, std::string_view name) {
  ByteCursor c{record};
  FishingMap map;
  map.mapId = c.i16();
  map.unknown2 = c.i16();
  map.scale = c.i32();
  map.unknown8 = c.i32();
  map.fishScaleSmall = c.i32();
  map.fishScaleLarge = c.i32();
  map.seadramonTimes = {c.i16(), c.i16()};
  const std::uint32_t spots = c.u32();
  map.unknown1C = c.i32();
  const std::uint32_t waterTest = c.u32();
  c.skip(4); // unk24: a stub of FISH code next to the water test
  map.castRange = c.i16();
  map.unknown2A = c.i16();
  const std::uint32_t spriteFile = c.u32();
  const std::uint32_t waypoints = c.u32();
  const std::int32_t waypointCount = c.i32();

  for (std::size_t i = 0; i < layout.spotsPerMap; ++i) {
    auto spot = readFishingSpot(image, spots + static_cast<std::uint32_t>(i * FishingSpotSize),
                                std::format("{}.fishingSpots[{}]", name, i));
    if (!spot) {
      return std::unexpected{spot.error()};
    }
    map.spots.push_back(std::move(*spot));
  }
  for (std::size_t i = 0; i < layout.shorelines.size(); ++i) {
    if (layout.shorelines[i].waterTest == waterTest) {
      map.shoreline = i;
    }
  }
  auto path = readPath(image, spriteFile, std::format("{}.spriteFilePath", name));
  if (!path) {
    return std::unexpected{path.error()};
  }
  map.spriteFile = std::move(*path);
  if (waypointCount < 0 || std::cmp_greater(waypointCount, MaxListLength)) {
    return core::fail(std::format("{}: {} sea serpent waypoints", name, waypointCount));
  }
  auto points = readPoints(image, waypoints, static_cast<std::size_t>(waypointCount),
                           std::format("{}.seadramonWaypoints", name));
  if (!points) {
    return std::unexpected{points.error()};
  }
  map.seadramonWaypoints = std::move(*points);
  return map;
}

core::Result<std::vector<BattleArena>> readArenas(const MemoryImage& overlay,
                                                  const MemoryImage& executable,
                                                  const BattleStageLayout& layout) {
  auto models = readPaths(overlay, layout.arenaModels, "arena models");
  auto textures = readPaths(overlay, layout.arenaTextures, "arena textures");
  auto collisions = readPaths(overlay, layout.arenaCollisions, "arena collisions");
  auto modelCounts =
      readValues<std::uint8_t>(executable, layout.arenaModelCounts, "arena model counts");
  auto textureCounts =
      readValues<std::uint8_t>(executable, layout.arenaTextureCounts, "arena TIM counts");
  for (const auto* failed : {&models, &textures, &collisions}) {
    if (!*failed) {
      return std::unexpected{failed->error()};
    }
  }
  for (const auto* failed : {&modelCounts, &textureCounts}) {
    if (!*failed) {
      return std::unexpected{failed->error()};
    }
  }
  std::vector<BattleArena> arenas;
  for (std::size_t i = 0; i < models->size() && i < textures->size() && i < collisions->size();
       ++i) {
    const bool any = !(*models)[i].empty() || !(*textures)[i].empty() || !(*collisions)[i].empty();
    if (!any) {
      continue;
    }
    if ((*models)[i].empty() || (*textures)[i].empty() || (*collisions)[i].empty()) {
      return core::fail(std::format("arena {} names some of its files and not others", i));
    }
    if (i >= modelCounts->size() || i >= textureCounts->size()) {
      return core::fail(std::format("arena {} has files but no counts in the executable", i));
    }
    arenas.push_back({.index = i,
                      .model = (*models)[i],
                      .texture = (*textures)[i],
                      .collision = (*collisions)[i],
                      .modelCount = (*modelCounts)[i],
                      .textureCount = (*textureCounts)[i]});
  }
  return arenas;
}

core::Result<std::vector<TrainingMapSpots>> readTrainingSpots(const MemoryImage& image,
                                                              const TableLocation& table) {
  auto bytes = tableBytes(image, table, MapTrainingSpotsSize, "training spots");
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  ByteCursor c{*bytes};
  std::vector<TrainingMapSpots> maps;
  for (std::size_t i = 0; i < table.count; ++i) {
    const std::int32_t mapId = c.i32();
    const std::uint32_t spots = c.u32();
    if (mapId == EndOfMaps) {
      return maps;
    }
    TrainingMapSpots map{.mapId = mapId, .spots = {}};
    for (std::size_t k = 0;; ++k) {
      if (k == MaxListLength) {
        return core::fail(std::format("training spots of map {}: no end marker", mapId));
      }
      auto spot =
          image.bytesAt(spots + static_cast<std::uint32_t>(k * TrainingSpotSize), TrainingSpotSize);
      if (!spot) {
        return core::fail(std::format("training spots of map {}: {}", mapId, spot.error().message));
      }
      ByteCursor s{*spot};
      const TrainingSpot read{.x = s.i16(), .y = s.i16(), .z = s.i16(), .stat = s.i16()};
      if (read.stat == EndOfSpots) {
        break;
      }
      map.spots.push_back(read);
    }
    maps.push_back(std::move(map));
  }
  return core::fail("training spots: no end marker");
}

} // namespace

std::optional<OverlayTablesLayout> findOverlayTablesLayout(std::string_view version) {
  for (const OverlayTablesLayout& layout : Layouts) {
    if (layout.version == version) {
      return layout;
    }
  }
  return std::nullopt;
}

core::Result<FishingTables> readFishingTables(const MemoryImage& overlay,
                                              const FishingLayout& layout) {
  FishingTables tables;
  auto bait = tableBytes(overlay, layout.baitChances, 2, "FISH_BAIT_CHANCES");
  if (!bait) {
    return std::unexpected{bait.error()};
  }
  ByteCursor b{*bait};
  for (std::size_t i = 0; i < layout.baitChances.count; ++i) {
    tables.baitChances.push_back({.item = b.u8(), .threshold = b.u8()});
  }

  auto bite = readValues<std::int8_t>(overlay, layout.biteChances, "FISH_BITE_CHANCE");
  if (!bite) {
    return std::unexpected{bite.error()};
  }
  const std::size_t used = layout.biteRows * layout.biteKinds;
  if (used > bite->size()) {
    return core::fail("FISH_BITE_CHANCE is shorter than its rows");
  }
  tables.firstBiteDepth = layout.firstBiteDepth;
  for (std::size_t row = 0; row < layout.biteRows; ++row) {
    const auto first = bite->begin() + static_cast<std::ptrdiff_t>(row * layout.biteKinds);
    tables.biteChances.emplace_back(first, first + static_cast<std::ptrdiff_t>(layout.biteKinds));
  }
  tables.unusedBiteBytes.assign(bite->begin() + static_cast<std::ptrdiff_t>(used), bite->end());

  auto kinds = tableBytes(overlay, layout.kinds, FishKindSize, "FISH_KIND_INFO");
  if (!kinds) {
    return std::unexpected{kinds.error()};
  }
  ByteCursor k{*kinds};
  for (std::size_t i = 0; i < layout.kinds.count; ++i) {
    tables.kinds.push_back({.unknown0 = k.i32(),
                            .sizeMin = k.i32(),
                            .sizeMax = k.i32(),
                            .moveDelayMin = k.i32(),
                            .moveDelayMax = k.i32(),
                            .bait = k.i16(),
                            .unknown16 = k.i16()});
  }

  for (const ShorelineLocation& shoreline : layout.shorelines) {
    auto points =
        readPoints(overlay, shoreline.points.address, shoreline.points.count, "shoreline");
    if (!points) {
      return std::unexpected{points.error()};
    }
    tables.shorelines.push_back({.axis = shoreline.axis, .points = std::move(*points)});
  }

  auto maps = tableBytes(overlay, layout.maps, FishingMapSize, "FISHING_MAP_DATA");
  if (!maps) {
    return std::unexpected{maps.error()};
  }
  for (std::size_t i = 0; i < layout.maps.count; ++i) {
    auto map = readFishingMap(overlay, maps->subspan(i * FishingMapSize, FishingMapSize), layout,
                              std::format("FISHING_MAP_DATA[{}]", i));
    if (!map) {
      return std::unexpected{map.error()};
    }
    tables.maps.push_back(std::move(*map));
  }
  return tables;
}

// STD_BRACKET_PATHS and the bracket's other tables (std_main.c).
core::Result<BracketTables> readBracket(const MemoryImage& overlay,
                                        const BattleStageLayout& layout) {
  constexpr std::size_t PathSize = 24;
  BracketTables bracket;
  auto rowY = readValues<std::int16_t>(overlay, layout.bracketRowY, "bracket rows");
  if (!rowY) {
    return std::unexpected{rowY.error()};
  }
  auto hopY = readValues<std::int8_t>(overlay, layout.bracketHopY, "bracket hop");
  if (!hopY) {
    return std::unexpected{hopY.error()};
  }
  auto sprites = readValues<std::uint8_t>(overlay, layout.spritePalettes, "sprite palettes");
  if (!sprites) {
    return std::unexpected{sprites.error()};
  }
  auto icons = readValues<std::uint8_t>(overlay, layout.projectileIcons, "projectile icons");
  if (!icons) {
    return std::unexpected{icons.error()};
  }
  auto iconPalettes =
      readValues<std::uint8_t>(overlay, layout.projectilePalettes, "projectile palettes");
  if (!iconPalettes) {
    return std::unexpected{iconPalettes.error()};
  }
  auto paths = tableBytes(overlay, layout.bracketPaths, PathSize, "bracket paths");
  if (!paths) {
    return std::unexpected{paths.error()};
  }
  ByteCursor c{*paths};
  for (std::size_t i = 0; i < layout.bracketPaths.count; ++i) {
    BracketPath path;
    std::ranges::generate(path.x, [&c] { return c.i16(); });
    std::ranges::generate(path.flip, [&c] { return c.u8(); });
    bracket.paths.push_back(path);
  }
  bracket.rowY = std::move(*rowY);
  bracket.hopY = std::move(*hopY);
  bracket.spritePalettes = std::move(*sprites);
  bracket.projectileIcons = std::move(*icons);
  bracket.projectilePalettes = std::move(*iconPalettes);
  return bracket;
}

core::Result<BattleStageTables> readBattleStageTables(const MemoryImage& overlay,
                                                      const MemoryImage& executable,
                                                      const BattleStageLayout& layout) {
  BattleStageTables tables;
  if (layout.opponentBaseStats.located()) {
    auto stats = readValues<std::int16_t>(overlay, layout.opponentBaseStats, "opponent base stats");
    if (!stats) {
      return std::unexpected{stats.error()};
    }
    tables.opponentBaseStats = std::move(*stats);
  }
  if (layout.grades.located()) {
    auto grades = readPaths(overlay, layout.grades, "grades");
    if (!grades) {
      return std::unexpected{grades.error()};
    }
    tables.grades = std::move(*grades);
  }
  if (layout.winChances.located()) {
    auto rows = readRows<std::uint8_t>(overlay, layout.winChances, 3, "win chances");
    if (!rows) {
      return std::unexpected{rows.error()};
    }
    for (const auto& row : *rows) {
      tables.winChances.push_back({row[0], row[1], row[2]});
    }
  }
  auto cameras = tableBytes(overlay, layout.cameraPresets, CameraPresetSize, "camera presets");
  if (!cameras) {
    return std::unexpected{cameras.error()};
  }
  ByteCursor c{*cameras};
  for (std::size_t i = 0; i < layout.cameraPresets.count; ++i) {
    CameraPreset preset;
    std::ranges::generate(preset.values, [&c] { return c.i16(); });
    tables.cameraPresets.push_back(preset);
  }
  auto arenas = readArenas(overlay, executable, layout);
  if (!arenas) {
    return std::unexpected{arenas.error()};
  }
  tables.arenas = std::move(*arenas);
  if (layout.bracketPaths.located()) {
    auto bracket = readBracket(overlay, layout);
    if (!bracket) {
      return std::unexpected{bracket.error()};
    }
    tables.bracket = std::move(*bracket);
  }
  return tables;
}

core::Result<TrainingTables> readTrainingTables(const MemoryImage& overlay,
                                                const TrainingLayout& layout) {
  TrainingTables tables;
  auto animations = readRows<std::int8_t>(overlay, layout.animations, 2, "training animations");
  if (!animations) {
    return std::unexpected{animations.error()};
  }
  for (const auto& row : *animations) {
    tables.animations.push_back({.animation = row[0], .hitFrame = row[1]});
  }
  if (layout.learnableMoves.located()) {
    auto moves = readRows<std::uint8_t>(overlay, layout.learnableMoves, layout.learnableMovesWidth,
                                        "learnable moves");
    auto weights = readRows<std::int8_t>(overlay, layout.learnWeights, layout.learnWeightsWidth,
                                         "learn weights");
    if (!moves) {
      return std::unexpected{moves.error()};
    }
    if (!weights) {
      return std::unexpected{weights.error()};
    }
    tables.learnableMoves = std::move(*moves);
    tables.learnWeights = std::move(*weights);
  }
  auto reels = readRows<std::int8_t>(overlay, layout.slotReels, layout.slotReelWidth, "slot reels");
  auto assist = readValues<std::int8_t>(overlay, layout.assistRolls, "assist rolls");
  auto spots = readTrainingSpots(overlay, layout.spots);
  if (!reels) {
    return std::unexpected{reels.error()};
  }
  if (!assist) {
    return std::unexpected{assist.error()};
  }
  if (!spots) {
    return std::unexpected{spots.error()};
  }
  tables.slotReels = std::move(*reels);
  tables.assistRolls = std::move(*assist);
  tables.spots = std::move(*spots);
  return tables;
}

core::Result<BattleTables> readBattleTables(const MemoryImage& overlay,
                                            const BattleLayout& layout) {
  constexpr std::size_t TypeSize = 12;
  constexpr std::size_t SpriteAt = 10;
  auto bytes = tableBytes(overlay, layout.flattenedShotTypes, TypeSize, "flattened shot types");
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  BattleTables tables;
  for (std::size_t i = 0; i < layout.flattenedShotTypes.count; ++i) {
    ByteCursor c{bytes->subspan((i * TypeSize) + SpriteAt, 2)};
    tables.flattenedShotSprites.push_back(static_cast<std::int16_t>(c.u16()));
  }
  return tables;
}

} // namespace dwe::formats::exe
