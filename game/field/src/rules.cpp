#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/field/rules.hpp>

#include <algorithm>
#include <format>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::field {

namespace {

using assets::YamlReader;
using core::YamlNode;

// A distance, speed or time: zero or more.
float readNonNegative(YamlReader& reader, const YamlNode& node) {
    const float value = reader.number(node);
    if (value < 0.0F) {
        reader.fail(node, "the value cannot be negative");
    }
    return value;
}

// A size that must be more than zero.
float readPositive(YamlReader& reader, const YamlNode& node) {
    const float value = reader.number(node);
    if (reader.ok() && value <= 0.0F) {
        reader.fail(node, "the value must be more than zero");
    }
    return value;
}

MoverRules readMover(YamlReader& reader, const YamlNode& node, std::string_view what,
                     std::initializer_list<std::string_view> extraKeys = {}) {
    std::vector<std::string_view> keys{"model", "radius", "walk_speed", "run_speed", "turn_speed"};
    keys.insert(keys.end(), extraKeys.begin(), extraKeys.end());
    if (!node.isMapping()) {
        reader.fail(node, std::format("{} must be a mapping", what));
        return {};
    }
    for (const core::YamlEntry& entry : node.entries()) {
        if (std::ranges::find(keys, entry.key) == keys.end()) {
            reader.fail(entry.value, std::format("unknown key '{}' in {}", entry.key, what));
            return {};
        }
    }
    MoverRules mover{.model = reader.text(reader.field(node, "model")),
                     .radius = readPositive(reader, reader.field(node, "radius")),
                     .walkSpeed = readPositive(reader, reader.field(node, "walk_speed")),
                     .runSpeed = readPositive(reader, reader.field(node, "run_speed")),
                     .turnSpeed = readNonNegative(reader, reader.field(node, "turn_speed"))};
    if (reader.ok() && mover.runSpeed < mover.walkSpeed) {
        reader.fail(node, std::format("{}: run_speed cannot be less than walk_speed", what));
    }
    return mover;
}

PartnerRules readPartner(YamlReader& reader, const YamlNode& node) {
    PartnerRules partner{.mover = readMover(reader, node, "partner",
                                            {"run_distance", "walk_distance", "spawn_distance", "trail_length",
                                             "sight_half_width", "stuck_seconds", "teleport_distance"})};
    if (!reader.ok()) {
        return partner;
    }
    partner.runDistance = readPositive(reader, reader.field(node, "run_distance"));
    partner.walkDistance = readPositive(reader, reader.field(node, "walk_distance"));
    partner.spawnDistance = readNonNegative(reader, reader.field(node, "spawn_distance"));
    partner.trailLength = reader.integer<std::int32_t>(reader.field(node, "trail_length"));
    partner.sightHalfWidth = readNonNegative(reader, reader.field(node, "sight_half_width"));
    partner.stuckSeconds = readPositive(reader, reader.field(node, "stuck_seconds"));
    partner.teleportDistance = readPositive(reader, reader.field(node, "teleport_distance"));
    if (reader.ok() && partner.runDistance < partner.walkDistance) {
        reader.fail(node, "partner: run_distance cannot be less than walk_distance");
    }
    if (reader.ok() && partner.trailLength < 1) {
        reader.fail(node, "partner: trail_length must be at least 1");
    }
    return partner;
}

InputRules readInput(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, "input", {"dead_zone", "arrive_distance"})) {
        return {};
    }
    InputRules input{.deadZone = readNonNegative(reader, reader.field(node, "dead_zone")),
                     .arriveDistance = readPositive(reader, reader.field(node, "arrive_distance"))};
    if (reader.ok() && input.deadZone >= 1.0F) {
        reader.fail(node, "input: dead_zone must be less than 1");
    }
    return input;
}

TransitionRules readTransition(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, "transition", {"fade_out_seconds", "fade_in_seconds"})) {
        return {};
    }
    return {.fadeOutSeconds = readNonNegative(reader, reader.field(node, "fade_out_seconds")),
            .fadeInSeconds = readNonNegative(reader, reader.field(node, "fade_in_seconds"))};
}

ClockRules readClock(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, "clock", {"start_hour", "minute_seconds"})) {
        return {};
    }
    ClockRules clock{.startHour = reader.integer<std::int32_t>(reader.field(node, "start_hour")),
                     .minuteSeconds = readPositive(reader, reader.field(node, "minute_seconds"))};
    constexpr std::int32_t HoursPerDay = 24;
    if (reader.ok() && (clock.startHour < 0 || clock.startHour >= HoursPerDay)) {
        reader.fail(node, "clock: start_hour must be 0 to 23");
    }
    return clock;
}

glm::vec3 readLight(YamlReader& reader, const YamlNode& node, std::string_view what) {
    const auto items = reader.sequence(node, what, 3);
    if (items.size() != 3) {
        return glm::vec3{1.0F};
    }
    const glm::vec3 light{reader.number(items[0]), reader.number(items[1]), reader.number(items[2])};
    if (reader.ok() &&
        (glm::any(glm::lessThan(light, glm::vec3{0.0F})) || glm::any(glm::greaterThan(light, glm::vec3{1.0F})))) {
        reader.fail(node, std::format("time_of_day: {} must be three numbers from 0 to 1", what));
    }
    return light;
}

TimeOfDayRules readTimeOfDay(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, "time_of_day",
                           {"day_start", "dusk_start", "night_start", "dusk_light", "night_light"})) {
        return {};
    }
    TimeOfDayRules time{.dayStart = reader.integer<std::int32_t>(reader.field(node, "day_start")),
                        .duskStart = reader.integer<std::int32_t>(reader.field(node, "dusk_start")),
                        .nightStart = reader.integer<std::int32_t>(reader.field(node, "night_start")),
                        .duskLight = readLight(reader, reader.field(node, "dusk_light"), "dusk_light"),
                        .nightLight = readLight(reader, reader.field(node, "night_light"), "night_light")};
    constexpr std::int32_t HoursPerDay = 24;
    if (reader.ok() && (time.dayStart < 0 || time.dayStart >= time.duskStart || time.duskStart >= time.nightStart ||
                        time.nightStart >= HoursPerDay)) {
        reader.fail(node, "time_of_day: the hours must go 0 <= day_start < dusk_start < night_start <= 23");
    }
    return time;
}

template <typename T>
core::Result<T> loadDocument(const assets::VirtualFileSystem& vfs, std::string_view idText,
                             core::Result<T> (*parse)(std::string_view)) {
    auto id = assets::AssetId::parse(idText);
    if (!id) {
        return std::unexpected{id.error()};
    }
    auto bytes = vfs.read(*id);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return parse(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

} // namespace

core::Result<FieldRules> parseFieldRules(std::string_view yaml) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return std::unexpected{core::Error{std::format("field rules: {}", tree.error().message)}};
    }
    YamlReader reader;
    if (!reader.expectKeys(*tree, "the field rules",
                           {"player", "partner", "input", "transition", "clock", "time_of_day"})) {
        return std::unexpected{reader.error("field rules")};
    }
    FieldRules rules{.player = readMover(reader, reader.field(*tree, "player"), "player"),
                     .partner = readPartner(reader, reader.field(*tree, "partner")),
                     .input = readInput(reader, reader.field(*tree, "input")),
                     .transition = readTransition(reader, reader.field(*tree, "transition")),
                     .clock = readClock(reader, reader.field(*tree, "clock")),
                     .timeOfDay = readTimeOfDay(reader, reader.field(*tree, "time_of_day"))};
    return reader.finish(std::move(rules), "field rules");
}

core::Result<FieldWarps> parseFieldWarps(std::string_view yaml) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return std::unexpected{core::Error{std::format("field warps: {}", tree.error().message)}};
    }
    YamlReader reader;
    if (!reader.expectKeys(*tree, "the field warps", {"start", "scripted_warps"})) {
        return std::unexpected{reader.error("field warps")};
    }
    FieldWarps warps;
    const YamlNode& start = reader.field(*tree, "start");
    if (reader.expectKeys(start, "start", {"map", "exit"})) {
        warps.startMap = reader.text(reader.field(start, "map"));
        warps.startExit = reader.integer<std::int32_t>(reader.field(start, "exit"));
    }
    for (const YamlNode& item : reader.sequence(reader.field(*tree, "scripted_warps"), "scripted_warps")) {
        if (!reader.expectKeys(item, "a scripted warp", {"map", "trigger", "target_map", "target_exit", "evidence"})) {
            break;
        }
        warps.scriptedWarps.push_back({.map = reader.text(reader.field(item, "map")),
                                       .trigger = reader.integer<std::int32_t>(reader.field(item, "trigger")),
                                       .targetMap = reader.text(reader.field(item, "target_map")),
                                       .targetExit = reader.integer<std::int32_t>(reader.field(item, "target_exit")),
                                       .evidence = reader.text(reader.field(item, "evidence"))});
        const ScriptedWarp& added = warps.scriptedWarps.back();
        if (reader.ok() && (added.trigger <= 0 || added.trigger >= 0x80)) {
            reader.fail(item, "a trigger is a collision value from 1 to 127");
        }
    }
    return reader.finish(std::move(warps), "field warps");
}

core::Result<FieldRules> loadFieldRules(const assets::VirtualFileSystem& vfs) {
    return loadDocument<FieldRules>(vfs, "field/rules", &parseFieldRules);
}

core::Result<FieldWarps> loadFieldWarps(const assets::VirtualFileSystem& vfs) {
    return loadDocument<FieldWarps>(vfs, "field/warps", &parseFieldWarps);
}

} // namespace dwe::game::field
