#include "battle_trigger.hpp"

#include <dwe/core/message.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/evolution/tables.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::app {
namespace {

constexpr char NpcSeparator = ',';

bool canFight(const assets::MapNpc& npc, const assets::DigimonTable& species) {
    return npc.digimon && species.find(*npc.digimon) != nullptr && npc.hp > 0;
}

} // namespace

core::Result<assets::DigimonTable> speciesTable(const assets::VirtualFileSystem& files, const ruleset::Ruleset& rules) {
    const assets::AssetId* table = rules.table(game::evolution::DigimonTableRole);
    if (table == nullptr) {
        return core::fail(std::string{messages::NoSpeciesTable});
    }
    return assets::loadDigimonTable(files, *table);
}

core::Result<std::vector<std::size_t>> npcsOf(std::string_view argument) {
    std::vector<std::size_t> npcs;
    while (!argument.empty()) {
        const std::size_t end = std::min(argument.find(NpcSeparator), argument.size());
        const std::string_view number = argument.substr(0, end);
        std::size_t npc = 0;
        const auto [rest, error] = std::from_chars(number.data(), number.data() + number.size(), npc);
        if (error != std::errc{} || rest != number.data() + number.size()) {
            return core::fail(core::formatMessage(messages::BadNpcList, argument));
        }
        npcs.push_back(npc);
        argument.remove_prefix(std::min(end + 1, argument.size()));
    }
    return npcs;
}

core::Result<session::BattleRequest> mapBattleRequest(const game::field::Field& field, const assets::MapDocument& map,
                                                      const assets::DigimonTable& species,
                                                      const std::vector<std::size_t>& npcs) {
    std::vector<std::size_t> chosen = npcs;
    if (chosen.empty()) {
        for (std::size_t index = 0; index < map.npcs.size(); ++index) {
            if (canFight(map.npcs[index], species)) {
                chosen.push_back(index);
                break;
            }
        }
        if (chosen.empty()) {
            return core::fail(core::formatMessage(messages::NoMapFighter, field.mapId()));
        }
    }
    session::BattleRequest request{.enemies = {},
                                   .scripted = false,
                                   .tamerless = false,
                                   .map = field.mapId(),
                                   .tamer = {.x = static_cast<std::int32_t>(std::lround(field.player().position.x)),
                                             .z = static_cast<std::int32_t>(std::lround(field.player().position.z))},
                                   .kind = game::battle::BattleKind::Normal,
                                   .recruitOnVictory = std::nullopt};
    for (const std::size_t index : chosen) {
        if (index >= map.npcs.size()) {
            return core::fail(core::formatMessage(messages::NoMapNpc, field.mapId(), index));
        }
        const assets::MapNpc& npc = map.npcs[index];
        const assets::DigimonRow* row = npc.digimon ? species.find(*npc.digimon) : nullptr;
        if (row == nullptr || !canFight(npc, species)) {
            return core::fail(core::formatMessage(messages::NpcNotAFighter, index, field.mapId()));
        }
        auto enemy = game::battle::enemyFromMap(*row, npc);
        if (!enemy) {
            return std::unexpected{enemy.error()};
        }
        request.enemies.push_back(std::move(*enemy));
    }
    return request;
}

} // namespace dwe::app
