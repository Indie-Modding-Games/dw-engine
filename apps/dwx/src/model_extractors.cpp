#include "model_extractors.hpp"

#include <dwe/formats/image/tim_packs.hpp>
#include <dwe/formats/map/map_file.hpp>
#include <dwe/formats/model/animation.hpp>
#include <dwe/formats/model/gltf.hpp>
#include <dwe/formats/model/mmd.hpp>
#include <dwe/formats/model/skeleton.hpp>
#include <dwe/formats/model/tmd.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <ranges>
#include <utility>

#include "asset_names.hpp"

namespace dwe::dwx {

namespace {

using formats::disc::IsoFile;
using formats::image::Tim;

constexpr std::string_view AllTimPath = "CHDAT/ALLTIM.TIM";
constexpr std::string_view DigimonModelFolder = "CHDAT/MMD";
// model.c: loadMMD puts 30 types in each MMD<n> folder.
constexpr std::size_t TypesPerModelFolder = 30;
// model.c: loadDigimonTexture reads 9 sectors per type.
constexpr std::uint32_t AllTimSlotSectors = 9;
constexpr std::uint32_t SectorBytes = 2048;
// overworld.c: loadMapImage2 puts row i of the first 4-bit image's CLUT at (16 * i, 486).
constexpr std::uint16_t MapClutRowY = 486;
constexpr std::uint16_t MapClutStep = 16;
// overworld.c: loadMapImage1 leaves an 8-bit image's CLUT out when it is at row 480, the background's palette.
constexpr std::uint16_t BackgroundPaletteY = 480;

core::Result<std::vector<std::byte>> readFile(const Disc& disc, std::string_view path) {
    const IsoFile* file = disc.findFile(path);
    if (file == nullptr) {
        return core::fail(std::format("{} is not on the disc", path));
    }
    return formats::disc::readIsoFile(disc.source(), *file);
}

// Reads a file the way the original opens it: by name, in whatever folder it is (file.c: lookupFileTable). The
// folder of a map's path (map.c: buildMapPath) does not always match where the file is.
core::Result<std::vector<std::byte>> readFileByName(const Disc& disc, std::string_view name) {
    const auto& files = disc.volume().files;
    const auto named = [name](const IsoFile& file) {
        return file.path == name || (file.path.ends_with(name) && file.path[file.path.size() - name.size() - 1] == '/');
    };
    const auto found = std::ranges::find_if(files, named);
    if (found == files.end()) {
        return core::fail(std::format("no file on the disc is named {}", name));
    }
    if (std::ranges::count_if(files, named) > 1) {
        return core::fail(std::format("several files on the disc are named {}", name));
    }
    return formats::disc::readIsoFile(disc.source(), *found);
}

core::Result<const GameTables*> tablesOf(const Disc& disc) {
    if (disc.tables() == nullptr) {
        return core::fail("the executable is not a known version, so its tables cannot be read");
    }
    return disc.tables();
}

// Reads the TIM of one Digimon type: entry `type` of CHDAT/ALLTIM.TIM, without reading the whole pack.
core::Result<Tim> readDigimonTexture(const Disc& disc, std::size_t type) {
    const IsoFile* pack = disc.findFile(AllTimPath);
    if (pack == nullptr) {
        return core::fail(std::format("{} is not on the disc", AllTimPath));
    }
    const std::uint32_t slotBytes = AllTimSlotSectors * SectorBytes;
    if ((type + 1) * slotBytes > pack->size) {
        return core::fail(std::format("{} has no entry {}", AllTimPath, type));
    }
    const IsoFile slot{.path = pack->path,
                       .lba = pack->lba + (static_cast<std::uint32_t>(type) * AllTimSlotSectors),
                       .size = slotBytes};
    const auto bytes = formats::disc::readIsoFile(disc.source(), slot);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return formats::image::readTim(*bytes);
}

std::vector<std::string> issuesOf(const formats::model::Tmd& tmd) {
    std::vector<std::string> issues;
    issues.reserve(tmd.issues.size());
    for (const auto& issue : tmd.issues) {
        issues.push_back(std::format("TMD object {} primitive {}: {}", issue.object, issue.primitive, issue.message));
    }
    return issues;
}

// Writes the model; when its textures do not fit, writes it again with UVs only and says why.
core::Result<std::vector<std::byte>> writeModel(const formats::model::Tmd& tmd,
                                                std::span<const formats::model::SkeletonBone> skeleton,
                                                std::span<const formats::model::Animation> animations,
                                                const ModelTextures& textures, std::vector<std::string>& unknowns) {
    auto glb = formats::model::writeModelGlb(tmd, skeleton, animations, textures.textures);
    if (glb || textures.textures.empty()) {
        return glb;
    }
    unknowns.push_back(std::format("textures left out: {}", glb.error().message));
    return formats::model::writeModelGlb(tmd, skeleton, animations, {});
}

core::Result<Extraction> singleOutput(core::Result<assets::AssetId> id, core::Result<BuiltModel> model) {
    if (!id) {
        return std::unexpected{id.error()};
    }
    if (!model) {
        return std::unexpected{model.error()};
    }
    Extraction extraction;
    extraction.outputs.push_back(OutputFile{.id = *id, .extension = "glb", .bytes = std::move(model->glb)});
    extraction.unknowns = std::move(model->unknowns);
    extraction.notes = std::move(model->notes);
    return extraction;
}

// The images a field map uploads when it loads (map.c: loadMap, overworld.c: loadMapImage1, loadMapImage2).
core::Result<VramScene> mapScene(const Disc& disc, const formats::exe::MapEntry& entry) {
    const auto bytes = readFileByName(disc, entry.filename + ".MAP");
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const auto map =
        formats::map::readMapFile(*bytes, {.imageCount8 = entry.num8bppImages,
                                           .imageCount4 = entry.num4bppImages,
                                           .hasNpcs = (entry.flags & formats::exe::MapEntryFlags::HasNpcs) != 0});
    if (!map) {
        return std::unexpected{map.error()};
    }
    VramScene scene;
    bool firstIndexed4 = true;
    for (const auto& section : map->images) {
        const auto tim = formats::image::readTim(std::span(*bytes).subspan(section.offset, section.size));
        if (!tim) {
            return std::unexpected{tim.error()};
        }
        if (section.bitsPerPixel == 8) {
            if (tim->hasClut && tim->clutRect.y != BackgroundPaletteY) {
                scene.addTim(*tim);
            } else {
                scene.addPixels(*tim);
            }
            continue;
        }
        scene.addPixels(*tim);
        if (std::exchange(firstIndexed4, false) && tim->hasClut) {
            for (std::size_t row = 0; row < tim->clutRect.height; ++row) {
                const auto begin = tim->clut.begin() + static_cast<std::ptrdiff_t>(row * tim->clutRect.width);
                scene.addPalette(static_cast<std::uint16_t>(row * MapClutStep), MapClutRowY,
                                 std::vector<formats::image::Color15>(begin, begin + MapClutStep));
            }
        }
    }
    return scene;
}

// The door model a DOOR/DOOR<nn>.TMD path holds, as map_object.c: loadDoors numbers them.
std::optional<std::int8_t> doorModelOf(std::string_view path) {
    constexpr std::string_view Prefix = "DOOR/DOOR";
    if (!path.starts_with(Prefix) || !path.ends_with(".TMD") || path.size() != Prefix.size() + 6) {
        return std::nullopt;
    }
    const char tens = path[Prefix.size()];
    const char units = path[Prefix.size() + 1];
    if (tens < '0' || tens > '9' || units < '0' || units > '9') {
        return std::nullopt;
    }
    return static_cast<std::int8_t>(((tens - '0') * 10) + (units - '0'));
}

// Where the original draws a model: a TIM file uploaded where its images say, or the images of a field map.
struct SceneSource {
    std::string_view timFile;
    int map = -1;

    [[nodiscard]] bool used() const { return !timFile.empty() || map >= 0; }
};

// The scenes of the models whose texture the decomp shows, by the code that loads and draws them. ETCTIM.BIN stays in
// VRAM from the start of the game (main.c: main) and STDTIM.BIN through the arenas (std_setup.c:
// STD_initializeTournament, vs_scene.c: VS_loadVSAssets). Each scene is one place the original draws the model in.
struct ModelScenes {
    std::string_view model;
    std::array<SceneSource, 3> scenes;
    // Found by scanning the scripts for the routine that spawns it, not by code that ties the model to the scene.
    bool inferred = false;
};

constexpr SceneSource fromTimFile(std::string_view path) {
    return {.timFile = path, .map = -1};
}

constexpr SceneSource fromMap(int id) {
    return {.timFile = {}, .map = id};
}

constexpr std::string_view EtcTim = "ETCDAT/ETCTIM.BIN";
constexpr std::string_view StdTim = "STDDAT/STDTIM.BIN";

constexpr std::array KnownScenes = {
    // dooa.c: the egg of the rebirth scene, after ETCDAT/TAMA.TIM.
    ModelScenes{.model = "ETCDAT/TAMA.TMD", .scenes = {fromTimFile("ETCDAT/TAMA.TIM")}, .inferred = false},
    // fish.c: FISH_loadSpritesAndSeadramon, FISH_loadFishing; the map's sprite file is FISHING.TIM or FISHING2.TIM.
    ModelScenes{.model = "ETCDAT/SEA.TMD",
                .scenes = {fromTimFile("ETCDAT/FISHING.TIM"), fromTimFile("ETCDAT/FISHING2.TIM")},
                .inferred = false},
    ModelScenes{.model = "ETCDAT/SAO.TMD",
                .scenes = {fromTimFile("ETCDAT/FISHING.TIM"), fromTimFile("ETCDAT/FISHING2.TIM")},
                .inferred = false},
    // kar.c: KAR_start, only on map 136.
    ModelScenes{.model = "ETCDAT/KARRING.TMD", .scenes = {fromMap(136)}, .inferred = false},
    // The battle and arena overlays draw copies of these (buff_model.c, battle_effect.c, std_effect.c).
    ModelScenes{.model = "ETCDAT/DEFEFF.TMD", .scenes = {fromTimFile(EtcTim)}, .inferred = false},
    ModelScenes{.model = "ETCDAT/EX.TMD", .scenes = {fromTimFile("ETCDAT/EX.TIM")}, .inferred = false},
    ModelScenes{.model = "ETCHI/MAHI.TMD", .scenes = {fromTimFile(EtcTim)}, .inferred = false},
    // eab.c: EAB_startBuildup, spawned by the scripts of maps 225-228 (which upload no images) and 232.
    ModelScenes{.model = "ETCHI/BOSS_EFE.TMD", .scenes = {fromMap(225)}, .inferred = true},
    // map_object.c: initializeChest, loadDirtCartModel, initializeMedalModel; partner_impl.c: initializePoop.
    ModelScenes{.model = "ETCNA/TAKA.TMD", .scenes = {fromTimFile(EtcTim)}, .inferred = false},
    ModelScenes{.model = "ETCNA/DOSYA.TMD", .scenes = {fromTimFile(EtcTim)}, .inferred = false},
    ModelScenes{.model = "ETCNA/HAKO.TMD", .scenes = {fromTimFile(EtcTim)}, .inferred = false},
    ModelScenes{.model = "ETCNA/MEDAL.TMD", .scenes = {fromTimFile(EtcTim)}, .inferred = false},
    ModelScenes{.model = "ETCNA/UNTI.TMD", .scenes = {fromTimFile(EtcTim)}, .inferred = false},
    // map_object.c: spawnBoulder uploads ETCNA/IWA1.TIM right after the model.
    ModelScenes{.model = "ETCNA/IWA.TMD", .scenes = {fromTimFile("ETCNA/IWA1.TIM")}, .inferred = false},
    // Never loaded by the original: DOOR/DOOR00.TMD has the same bytes.
    ModelScenes{.model = "ETCNA/DOOR.TMD", .scenes = {}, .inferred = false},
    // map.c: loadMap, initializeTrainingPoop on map 165 and loadWarpCrystals on maps 101-103.
    ModelScenes{.model = "ETCNA/TRY.TMD", .scenes = {fromMap(165)}, .inferred = false},
    ModelScenes{.model = "ETCNA/WARP.TMD", .scenes = {fromMap(101), fromMap(102), fromMap(103)}, .inferred = false},
    // map_object.c: loadDirtPileModel, spawnGearbox, spawnToyTownBoxes, spawnAngemonPedestal, called by scripts.
    ModelScenes{.model = "ETCNA/T_YAMA.TMD", .scenes = {fromMap(28)}, .inferred = true},
    ModelScenes{.model = "ETCNA/GAND.TMD", .scenes = {fromMap(146)}, .inferred = true},
    ModelScenes{.model = "ETCNA/BIGBOX.TMD", .scenes = {fromMap(151)}, .inferred = true},
    ModelScenes{.model = "ETCNA/SMABOX.TMD", .scenes = {fromMap(151)}, .inferred = true},
    ModelScenes{.model = "ETCNA/ABOX.TMD", .scenes = {fromMap(97)}, .inferred = true},
    // std_hud.c, vs_hud.c: the arena's titles, after STDTIM.BIN.
    ModelScenes{.model = "STDDAT/1P2PWIN.TMD", .scenes = {fromTimFile(StdTim)}, .inferred = false},
    ModelScenes{.model = "STDDAT/CHAMP.TMD", .scenes = {fromTimFile(StdTim)}, .inferred = false},
    ModelScenes{.model = "STDDAT/DRAW.TMD", .scenes = {fromTimFile(StdTim)}, .inferred = false},
    ModelScenes{.model = "STDDAT/WIN_LOSE.TMD", .scenes = {fromTimFile(StdTim)}, .inferred = false},
    // std_setup.c: STD_loadArenaAssets, std_hud.c: STD_loadChampionModels, vs_scene.c: VS_loadArenaAssets. E_DAI is
    // never loaded; it has E_TOGI's first object.
    ModelScenes{.model = "STDDAT/T_TOGI.TMD", .scenes = {fromTimFile("STDDAT/T_TOGI.TIM")}, .inferred = false},
    ModelScenes{.model = "STDDAT/T_DAI.TMD", .scenes = {fromTimFile("STDDAT/T_TOGI.TIM")}, .inferred = false},
    ModelScenes{.model = "STDDAT/B_TOGI.TMD", .scenes = {fromTimFile("STDDAT/B_TOGI.TIM")}, .inferred = false},
    ModelScenes{.model = "STDDAT/B_DAI.TMD", .scenes = {fromTimFile("STDDAT/B_TOGI.TIM")}, .inferred = false},
    ModelScenes{.model = "STDDAT/E_TOGI.TMD", .scenes = {fromTimFile("STDDAT/E_TOGI.TIM")}, .inferred = false},
    ModelScenes{.model = "STDDAT/E_DAI.TMD", .scenes = {fromTimFile("STDDAT/E_TOGI.TIM")}, .inferred = false},
};

// What the original changes in a model's texture while it draws it. The file has neither, so the model does not.
struct RuntimeTexture {
    std::string_view model;
    std::string_view change;
};

constexpr std::array RuntimeTextures = {
    RuntimeTexture{.model = "ETCDAT/DEFEFF.TMD",
                   .change = "buff_model.c: morphBuffModel points object 5 at the palettes of rows 491 and 492 by "
                             "turns, one per frame"},
    RuntimeTexture{.model = "ETCNA/MEDAL.TMD",
                   .change = "map_object.c: activateMedalTexture copies the medal it shows into the texels the "
                             "model reads"},
};

// Uploads every image of a TIM file (one TIM, several one after another, or a stacked file) where it says.
core::Result<VramScene> timFileScene(const Disc& disc, std::string_view path) {
    const auto bytes = readFile(disc, path);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const auto list =
        path.ends_with(".BIN") ? formats::image::readStackedTims(*bytes) : formats::image::readTimSequence(*bytes);
    if (!list) {
        return std::unexpected{list.error()};
    }
    VramScene scene;
    for (const auto& entry : list->entries) {
        scene.addTim(entry.tim);
    }
    return scene;
}

// Reads the scenes of a model, by name; the ones that cannot be read go to the unknowns.
std::vector<NamedScene> readScenes(const Disc& disc, const formats::exe::ExecutableTables& tables,
                                   std::span<const SceneSource> sources, std::vector<std::string>& unknowns) {
    std::vector<NamedScene> scenes;
    for (const SceneSource& source : sources | std::views::filter(&SceneSource::used)) {
        std::string name{source.timFile};
        core::Result<VramScene> scene = core::fail("no scene");
        if (source.map >= 0) {
            if (std::cmp_greater_equal(source.map, tables.mapEntries.size())) {
                unknowns.push_back(std::format("map {} is not in the map table", source.map));
                continue;
            }
            const auto& entry = tables.mapEntries[static_cast<std::size_t>(source.map)];
            name = std::format("map {} ({})", source.map, entry.filename);
            scene = mapScene(disc, entry);
        } else {
            scene = timFileScene(disc, source.timFile);
        }
        if (!scene) {
            unknowns.push_back(std::format("{}: {}", name, scene.error().message));
            continue;
        }
        scenes.push_back({.name = std::move(name), .scene = std::move(*scene)});
    }
    return scenes;
}

// The maps whose doors include a door model (map_object.c: loadDoors runs after the map's images are uploaded).
std::vector<SceneSource> doorMaps(const formats::exe::ExecutableTables& tables, std::int8_t door) {
    std::vector<SceneSource> maps;
    for (std::size_t mapId = 0; mapId < tables.mapEntries.size(); ++mapId) {
        const std::int8_t doors = tables.mapEntries[mapId].doorsId;
        if (doors > 0 && std::cmp_less_equal(doors, tables.doorMapData.size()) &&
            std::ranges::contains(tables.doorMapData[static_cast<std::size_t>(doors - 1)].modelId, door)) {
            maps.push_back({.timFile = {}, .map = static_cast<int>(mapId)});
        }
    }
    return maps;
}

} // namespace

core::Result<std::size_t> digimonTypeOfModel(std::string_view path, std::span<const std::string> fileNames) {
    if (!handlesDigimonModel(path)) {
        return core::fail(std::format("{} is not a Digimon model", path));
    }
    const auto folder = static_cast<std::size_t>(path[DigimonModelFolder.size()] - '0');
    const std::string_view name =
        path.substr(DigimonModelFolder.size() + 2, path.size() - DigimonModelFolder.size() - 6);
    for (std::size_t type = 0; type < fileNames.size(); ++type) {
        if (fileNames[type] == name && type / TypesPerModelFolder == folder) {
            return type;
        }
    }
    return core::fail(std::format("no Digimon type has the model {}", path));
}

core::Result<BuiltModel> buildDigimonModel(std::span<const std::byte> bytes, const Tim& texture,
                                           std::span<const formats::exe::SkeletonBone> bones) {
    const auto mmd = formats::model::readMmd(bytes);
    if (!mmd) {
        return std::unexpected{mmd.error()};
    }
    std::vector<formats::model::SkeletonBone> skeleton;
    std::ranges::transform(bones, std::back_inserter(skeleton), [](const formats::exe::SkeletonBone& bone) {
        return formats::model::SkeletonBone{.objIndex = bone.objIndex, .parentIndex = bone.parentIndex};
    });
    BuiltModel model{.glb = {}, .unknowns = issuesOf(mmd->tmd), .notes = {}};

    std::vector<formats::model::Animation> animations;
    for (std::size_t id = 0; id < mmd->animations.size(); ++id) {
        const auto& offset = mmd->animations[id];
        if (!offset) {
            continue;
        }
        auto animation =
            formats::model::simulateAnimation(bytes.subspan(*offset), static_cast<std::uint8_t>(id), skeleton.size());
        if (!animation) {
            model.unknowns.push_back(std::format("animation {}: {}", id, animation.error().message));
            continue;
        }
        animations.push_back(std::move(*animation));
    }

    VramScene scene;
    scene.addTim(texture);
    const auto textures = textureModel(mmd->tmd, scene);
    if (!textures) {
        return std::unexpected{textures.error()};
    }
    model.unknowns.insert(model.unknowns.end(), textures->unresolved.begin(), textures->unresolved.end());
    auto glb = writeModel(mmd->tmd, skeleton, animations, *textures, model.unknowns);
    if (!glb) {
        return std::unexpected{glb.error()};
    }
    model.glb = std::move(*glb);
    return model;
}

core::Result<BuiltModel> buildStaticModel(std::span<const std::byte> bytes, std::span<const NamedScene> scenes,
                                          std::string_view unknownTexture) {
    const auto tmd = formats::model::readTmd(bytes);
    if (!tmd) {
        return std::unexpected{tmd.error()};
    }
    std::vector<ModelTextures> drawn;
    for (const NamedScene& scene : scenes) {
        auto textures = textureModel(*tmd, scene.scene);
        if (!textures) {
            return std::unexpected{textures.error()};
        }
        drawn.push_back(std::move(*textures));
    }

    BuiltModel model{.glb = {}, .unknowns = issuesOf(*tmd), .notes = {}};
    const VramScene empty;
    ModelTextures textures;
    if (drawn.empty()) {
        auto untextured = textureModel(*tmd, empty);
        if (!untextured) {
            return std::unexpected{untextured.error()};
        }
        textures = std::move(*untextured);
        if (textures.texturedPrimitives > 0) {
            model.unknowns.push_back(std::format("texture unknown ({}): its {} textured primitives keep their UVs on "
                                                 "their texture page, without an image",
                                                 unknownTexture, textures.texturedPrimitives));
        }
    } else {
        const auto chosen = std::ranges::find_if(drawn, [](const ModelTextures& t) { return t.unresolved.empty(); });
        const std::size_t index = chosen == drawn.end() ? 0 : static_cast<std::size_t>(chosen - drawn.begin());
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            if (i == index) {
                continue;
            }
            if (!drawn[i].unresolved.empty()) {
                model.unknowns.push_back(std::format("{} does not hold all its texels", scenes[i].name));
            } else if (drawn[i].texels != drawn[index].texels) {
                model.unknowns.push_back(std::format("{} draws it with other texels than {}, which the model has",
                                                     scenes[i].name, scenes[index].name));
            }
        }
        for (const std::string& unresolved : drawn[index].unresolved) {
            model.unknowns.push_back(std::format("{}: {}", scenes[index].name, unresolved));
        }
        model.notes.push_back(std::format("textured with what {} has in VRAM", scenes[index].name));
        textures = std::move(drawn[index]);
    }

    // A node per object, none the parent of another, so each object stays where the file puts it.
    std::vector<formats::model::SkeletonBone> skeleton;
    skeleton.reserve(tmd->objects.size());
    for (std::size_t i = 0; i < tmd->objects.size(); ++i) {
        skeleton.push_back({.objIndex = static_cast<std::int8_t>(i), .parentIndex = -1});
    }
    auto glb = writeModel(*tmd, skeleton, {}, textures, model.unknowns);
    if (!glb) {
        return std::unexpected{glb.error()};
    }
    model.glb = std::move(*glb);
    return model;
}

bool handlesDigimonModel(std::string_view path) {
    // CHDAT/MMD<digit>/<name>.MMD
    return path.starts_with(DigimonModelFolder) && path.size() > DigimonModelFolder.size() + 6 &&
           path[DigimonModelFolder.size()] >= '0' && path[DigimonModelFolder.size()] <= '9' &&
           path[DigimonModelFolder.size() + 1] == '/' && path.ends_with(".MMD") &&
           path.find('/', DigimonModelFolder.size() + 2) == std::string_view::npos;
}

core::Result<Extraction> extractDigimonModel(const Disc& disc, const IsoFile& file) {
    const auto game = tablesOf(disc);
    if (!game) {
        return std::unexpected{game.error()};
    }
    const formats::exe::ExecutableTables& tables = (*game)->tables;
    const auto type = digimonTypeOfModel(file.path, tables.digimonFileNames);
    if (!type) {
        return std::unexpected{type.error()};
    }
    if (*type >= tables.digimonSkeletons.size()) {
        return core::fail(std::format("Digimon type {} has no skeleton", *type));
    }
    const auto texture = readDigimonTexture(disc, *type);
    if (!texture) {
        return std::unexpected{texture.error()};
    }
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return singleOutput(names::digimonAsset(*game, *type, "model"),
                        buildDigimonModel(*bytes, *texture, tables.digimonSkeletons[*type]));
}

bool handlesStaticModel(std::string_view path) {
    return path.ends_with(".TMD");
}

core::Result<Extraction> extractStaticModel(const Disc& disc, const IsoFile& file) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const auto id = names::discModel(file.path);
    const auto door = doorModelOf(file.path);
    const auto* const known = std::ranges::find(KnownScenes, file.path, &ModelScenes::model);
    if (!door && known == KnownScenes.end()) {
        return singleOutput(id, buildStaticModel(*bytes, {}, "the code that draws it is not traced"));
    }
    const auto game = tablesOf(disc);
    if (!game) {
        return std::unexpected{game.error()};
    }
    const formats::exe::ExecutableTables& tables = (*game)->tables;
    std::vector<std::string> unknowns;
    std::vector<NamedScene> scenes;
    std::string_view unknownTexture;
    if (door) {
        const auto maps = doorMaps(tables, *door);
        scenes = readScenes(disc, tables, maps, unknowns);
        unknownTexture = maps.empty() ? "no map has this door" : "its maps could not be read";
    } else {
        scenes = readScenes(disc, tables, known->scenes, unknowns);
        unknownTexture = std::ranges::none_of(known->scenes, &SceneSource::used) ? "the original never loads it"
                                                                                 : "its scenes could not be read";
        if (known->inferred && !scenes.empty()) {
            unknowns.push_back(std::format("texture inferred: {} is where the scripts spawn it, but no code ties the "
                                           "model to it",
                                           scenes.front().name));
        }
    }
    auto model = buildStaticModel(*bytes, scenes, unknownTexture);
    if (model) {
        model->unknowns.insert(model->unknowns.end(), unknowns.begin(), unknowns.end());
        const auto* const runtime = std::ranges::find(RuntimeTextures, file.path, &RuntimeTexture::model);
        if (runtime != RuntimeTextures.end()) {
            model->notes.push_back(std::format("the texture changes as it is drawn: {}", runtime->change));
        }
    }
    return singleOutput(id, std::move(model));
}

} // namespace dwe::dwx
