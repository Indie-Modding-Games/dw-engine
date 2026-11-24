#include <dwe/game/randomizer/map_enemies.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::randomizer {

namespace {

// overworld.c: loadMapDigimon: an equipped move is the first move animation plus its index in the
// species' list.
constexpr std::int32_t FirstMoveAnimation = 0x2E;
constexpr std::int32_t EmptyMoveAnimation = 0xFF;

void giveMoves(const Randomizer& randomizer, std::string_view map, std::size_t slot,
               const assets::DigimonRow& row, assets::MapNpc& npc) {
  std::vector<std::int32_t> known;
  for (std::size_t index = 0; index < row.moves.size(); ++index) {
    if (row.moves.at(index)) {
      known.push_back(FirstMoveAnimation + static_cast<std::int32_t>(index));
    }
  }
  if (known.empty()) {
    return;
  }
  const std::size_t start =
      randomizer.hash("enemy_moves", {map, std::to_string(slot)}) % known.size();
  std::size_t used = 0;
  for (std::int32_t& move : npc.moves) {
    if (move == EmptyMoveAnimation) {
      continue;
    }
    move = used < known.size() ? known[(start + used) % known.size()] : EmptyMoveAnimation;
    ++used;
  }
}

} // namespace

StagePools stagePools(const assets::DigimonTable& species, std::span<const std::string> roster) {
  StagePools pools;
  for (const std::string& id : roster) {
    const assets::DigimonRow* row = species.find(id);
    if (row != nullptr && row->level > 0 && row->boneCount > 0) {
      pools[row->level].push_back(id);
    }
  }
  for (auto& [level, ids] : pools) {
    std::ranges::sort(ids);
  }
  return pools;
}

void randomizeMapEnemies(const Randomizer& randomizer, std::string_view map,
                         std::span<assets::MapNpc> npcs, const assets::DigimonTable& species,
                         const StagePools& pools) {
  if (!randomizer.settings().enemies) {
    return;
  }
  for (std::size_t slot = 0; slot < npcs.size(); ++slot) {
    assets::MapNpc& npc = npcs[slot];
    const assets::DigimonRow* original = npc.digimon ? species.find(*npc.digimon) : nullptr;
    const auto pool = original != nullptr ? pools.find(original->level) : pools.end();
    if (pool == pools.end()) {
      continue;
    }
    const auto picked = randomizer.enemy(map, slot, pool->second);
    const assets::DigimonRow* row = picked ? species.find(*picked) : nullptr;
    if (row == nullptr) {
      continue;
    }
    npc.digimon = row->id;
    giveMoves(randomizer, map, slot, *row, npc);
  }
}

} // namespace dwe::game::randomizer
