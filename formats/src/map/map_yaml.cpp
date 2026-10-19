#include <dwe/formats/map/map_yaml.hpp>

#include <yaml/yaml_values.hpp>

#include <array>
#include <format>
#include <set>
#include <string_view>
#include <utility>

namespace dwe::formats::map {

namespace {

using core::YamlNode;
using yaml::null;
using yaml::number;
using yaml::numbers;
using yaml::quoted;
using yaml::vector3;

constexpr std::uint8_t FreeCell = 0x00;
constexpr std::uint8_t BlockedCell = 0x80;
constexpr char FreeSymbol = '.';
constexpr char BlockedSymbol = '#';
// Symbols for the other cell values, given in ascending order of value.
constexpr std::string_view TriggerSymbols = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

YamlNode idOf(std::span<const std::string> ids, std::int16_t index) {
    if (index >= 0 && std::cmp_less(index, ids.size())) {
        return quoted(ids[static_cast<std::size_t>(index)]);
    }
    return null();
}

YamlNode rgb(std::int32_t r, std::int32_t g, std::int32_t b) {
    YamlNode node = YamlNode::mapping();
    node.set("r", number(r));
    node.set("g", number(g));
    node.set("b", number(b));
    return node;
}

YamlNode camera(const MapSetup& setup) {
    YamlNode node = YamlNode::mapping();
    node.set("viewpoint", vector3(setup.vpx, setup.vpy, setup.vpz));
    node.set("reference", vector3(setup.vrx, setup.vry, setup.vrz));
    node.set("projection", number(setup.projection));
    return node;
}

YamlNode lights(const MapSetup& setup) {
    YamlNode list = YamlNode::sequence();
    for (const MapLight& light : setup.lights) {
        YamlNode node = YamlNode::mapping();
        node.set("direction", vector3(light.vx, light.vy, light.vz));
        node.set("color", rgb(light.r, light.g, light.b));
        list.append(std::move(node));
    }
    return list;
}

YamlNode tiles(const MapSetup& setup) {
    YamlNode rows = YamlNode::sequence();
    const auto columns = static_cast<std::size_t>(std::max(setup.width, 0));
    for (std::size_t start = 0; columns > 0 && start < setup.tiles.size(); start += columns) {
        std::string row;
        for (std::size_t i = start; i < start + columns && i < setup.tiles.size(); ++i) {
            row += std::format("{}{}", row.empty() ? "" : " ", setup.tiles[i]);
        }
        rows.append(quoted(std::move(row)));
    }
    YamlNode node = YamlNode::mapping();
    node.set("width", number(setup.width));
    node.set("height", number(setup.height));
    node.set("rows", std::move(rows));
    return node;
}

YamlNode images(const MapFile& map) {
    YamlNode list = YamlNode::sequence();
    for (const MapImageSection& image : map.images) {
        YamlNode node = YamlNode::mapping();
        node.set("bits_per_pixel", number(image.bitsPerPixel));
        node.set("offset", number(image.offset));
        node.set("size", number(image.size));
        list.append(std::move(node));
    }
    return list;
}

YamlNode objects(const MapObjects& objects) {
    YamlNode sprites = YamlNode::sequence();
    for (const MapSprite& sprite : objects.sprites) {
        YamlNode node = YamlNode::mapping();
        node.set("tex_x", number(sprite.texX));
        node.set("tex_y", number(sprite.texY));
        node.set("width", number(sprite.width));
        node.set("height", number(sprite.height));
        node.set("position", vector3(sprite.someX, sprite.someY, sprite.someZ));
        node.set("clut", number(sprite.clut));
        node.set("transparency", number(sprite.transparency));
        sprites.append(std::move(node));
    }
    YamlNode instances = YamlNode::sequence();
    for (const MapObjectInstance& instance : objects.instances) {
        YamlNode frames = YamlNode::sequence();
        for (std::size_t i = 0; i < instance.animSprites.size() && instance.animSprites[i] != -1; ++i) {
            YamlNode frame = YamlNode::mapping();
            frame.set("sprite", number(instance.animSprites[i]));
            frame.set("time", number(instance.animTimes[i]));
            frames.append(std::move(frame));
        }
        YamlNode node = YamlNode::mapping();
        node.set("frames", std::move(frames));
        node.set("x", number(instance.x));
        node.set("y", number(instance.y));
        node.set("flag", number(instance.flag));
        instances.append(std::move(node));
    }
    YamlNode node = YamlNode::mapping();
    node.set("sprites", std::move(sprites));
    node.set("instances", std::move(instances));
    return node;
}

YamlNode warps(const MapWarps& warps, const MapYamlIds& ids) {
    YamlNode list = YamlNode::sequence();
    for (std::size_t exit = 0; exit < warps.spawnX.size(); ++exit) {
        YamlNode node = YamlNode::mapping();
        node.set("exit", number(exit));
        node.set("spawn", vector3(warps.spawnX[exit], warps.spawnY[exit], warps.spawnZ[exit]));
        node.set("rotation", number(warps.rotation[exit]));
        node.set("target_map", number(warps.targetMap[exit]));
        node.set("target_map_id", idOf(ids.maps, warps.targetMap[exit]));
        node.set("target_exit", number(warps.targetExit[exit]));
        list.append(std::move(node));
    }
    return list;
}

YamlNode npcs(const std::vector<MapNpc>& npcs, const MapYamlIds& ids) {
    YamlNode list = YamlNode::sequence();
    for (const MapNpc& npc : npcs) {
        YamlNode node = YamlNode::mapping();
        node.set("type_id", number(npc.typeId));
        node.set("digimon", idOf(ids.digimon, npc.typeId));
        node.set("follow_mode", number(npc.followMode));
        node.set("position", vector3(npc.posX, npc.posY, npc.posZ));
        node.set("rotation", vector3(npc.rotX, npc.rotY, npc.rotZ));
        node.set("tracking_range", number(npc.trackingRange));
        node.set("unknown_12", number(npc.unknown12));
        node.set("script_id", number(npc.scriptId));
        node.set("hp", number(npc.hp));
        node.set("mp", number(npc.mp));
        node.set("current_hp", number(npc.currentHP));
        node.set("current_mp", number(npc.currentMP));
        node.set("off", number(npc.off));
        node.set("def", number(npc.def));
        node.set("speed", number(npc.speed));
        node.set("brain", number(npc.brain));
        node.set("bits", number(npc.bits));
        node.set("charge_mode", number(npc.chargeMode));
        node.set("unknown_2a", number(npc.unknown2A));
        node.set("moves", numbers(npc.moves));
        node.set("moves_prio", numbers(npc.movesPrio));
        node.set("flee", vector3(npc.fleeX, npc.fleeY, npc.fleeZ));
        node.set("ai_sections", numbers(npc.aiSections));
        YamlNode waypoints = YamlNode::sequence();
        for (const MapWaypoint& waypoint : npc.waypoints) {
            waypoints.append(vector3(waypoint.x, waypoint.y, waypoint.z));
        }
        node.set("waypoints", std::move(waypoints));
        list.append(std::move(node));
    }
    return list;
}

// One character per cell when the values fit the symbols; otherwise two hex digits per cell.
YamlNode collision(const std::vector<std::uint8_t>& cells) {
    std::array<char, 256> symbols{};
    const std::set<std::uint8_t> values(cells.begin(), cells.end());
    std::size_t nextTrigger = 0;
    bool fits = true;
    for (const std::uint8_t value : values) {
        if (value == FreeCell) {
            symbols[value] = FreeSymbol;
        } else if (value == BlockedCell) {
            symbols[value] = BlockedSymbol;
        } else if (nextTrigger < TriggerSymbols.size()) {
            symbols[value] = TriggerSymbols[nextTrigger++];
        } else {
            fits = false;
        }
    }

    YamlNode legend = YamlNode::sequence();
    if (fits) {
        for (const std::uint8_t value : values) {
            YamlNode entry = YamlNode::mapping();
            entry.set("cell", quoted(std::string(1, symbols[value])));
            entry.set("value", number(value));
            legend.append(std::move(entry));
        }
    }
    YamlNode rows = YamlNode::sequence();
    for (std::size_t start = 0; start < cells.size(); start += CollisionSize) {
        std::string row;
        for (std::size_t i = start; i < start + CollisionSize && i < cells.size(); ++i) {
            row += fits ? std::string(1, symbols[cells[i]]) : std::format("{:02X}", cells[i]);
        }
        rows.append(quoted(std::move(row)));
    }
    YamlNode node = YamlNode::mapping();
    node.set("width", number(CollisionSize));
    node.set("height", number(CollisionSize));
    node.set("encoding", quoted(fits ? "legend" : "hex"));
    node.set("legend", std::move(legend));
    node.set("rows", std::move(rows));
    return node;
}

} // namespace

core::YamlNode mapToYaml(const MapFile& map, const MapYamlIds& ids) {
    const MapSetup& setup = map.setup;
    YamlNode root = YamlNode::mapping();
    root.set("camera", camera(setup));
    root.set("lights", lights(setup));
    root.set("ambient", rgb(setup.ambientR, setup.ambientG, setup.ambientB));
    YamlNode regions = YamlNode::mapping();
    regions.set("a", numbers(setup.regionA));
    regions.set("b", numbers(setup.regionB));
    root.set("regions", std::move(regions));
    root.set("tiles", tiles(setup));
    root.set("images", images(map));
    root.set("objects", map.objects ? objects(*map.objects) : null());
    root.set("warps", warps(map.entities.warps, ids));
    root.set("digimon_count", number(map.entities.digimonCount));
    root.set("npcs", npcs(map.entities.npcs, ids));
    root.set("collision", collision(map.collision));
    return root;
}

} // namespace dwe::formats::map
