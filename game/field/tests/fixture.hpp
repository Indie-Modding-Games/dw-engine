#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/game/field/rules.hpp>

#include <string>
#include <string_view>

namespace dwe::game::field::testing {

// A map for the tests: a room of free cells with blocked walls around it, a camera that looks toward world -Z, as
// File City's does, and ten unused exits.
inline assets::MapDocument roomMap(std::string id) {
    assets::MapDocument map;
    map.id = std::move(id);
    map.camera = {.viewpoint = {0, -4301, -5973}, .reference = {0, 0, -256}, .projection = 1813};
    for (std::int32_t exit = 0; exit < 10; ++exit) {
        map.warps.push_back(
            {.exit = exit, .spawn = {}, .rotation = 0, .targetMap = -1, .targetMapId = std::nullopt, .targetExit = -1});
    }
    for (std::int32_t row = 0; row < assets::MapCollision::Size; ++row) {
        for (std::int32_t column = 0; column < assets::MapCollision::Size; ++column) {
            const bool inside = column >= 30 && column < 70 && row >= 30 && row < 70;
            map.collision.setValue({.column = column, .row = row}, inside ? 0 : assets::MapCollision::BlockedBit);
        }
    }
    return map;
}

// Fills a rectangle of cells (inclusive) with a value.
inline void fill(assets::MapDocument& map, assets::CollisionCell from, assets::CollisionCell to, std::uint8_t value) {
    for (std::int32_t row = from.row; row <= to.row; ++row) {
        for (std::int32_t column = from.column; column <= to.column; ++column) {
            map.collision.setValue({.column = column, .row = row}, value);
        }
    }
}

// The original's values, as in content/packs/game/field/rules.yaml, with instant turns.
inline FieldRules originalRules() {
    FieldRules rules;
    rules.player = {
        .model = "digimon/boys/model", .radius = 100.0F, .walkSpeed = 400.0F, .runSpeed = 1000.0F, .turnSpeed = 0.0F};
    rules.partner.mover = {
        .model = "digimon/agum/model", .radius = 120.0F, .walkSpeed = 452.0F, .runSpeed = 870.0F, .turnSpeed = 0.0F};
    return rules;
}

} // namespace dwe::game::field::testing
