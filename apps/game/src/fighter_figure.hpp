#pragma once

#include <dwe/game/battle/state.hpp>

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>

namespace dwe::app {

// A battle's fighter as the field draws it (BattleScreen to FieldScene::holdFighter): where it
// stands and faces, in world axes, and the clip it plays (the original's animation id), started
// again whenever `starts` changes and standing on its frame while frozen. `soundSlot` is the slot
// that holds its species' bank, where its clips' own sounds play (stats.current.vabId: 4 for the
// partner, 4 + i for enemy i, btl.c: loadBattleData).
struct FighterFigure {
  float x = 0.0F;
  float z = 0.0F;
  float yaw = 0.0F;
  std::uint32_t clip = 0;
  std::uint32_t starts = 0;
  bool frozen = false;
  std::int64_t soundSlot = 4;
  // How fast its clip runs: 2 for an NPC that runs off (animFlag 4: battle_tick.c:
  // NPCEntityTickBattle ticks its animation twice a frame, so its root motion goes twice as far).
  float pace = 1.0F;
  // The flattening's body scale (x, y, z) and the paper sprite drawn instead of the model
  // (battle_main.c: BTL_applyFlattenScale; model.c: renderFlatDigimon).
  glm::vec3 scale{1.0F};
  // How far above where it stands it is drawn (std_effect.c: STD_tickChampionScene lifts the
  // partner on the podium); its screen points (fighterNodeOnWindow) stay where it stands.
  float rise = 0.0F;
  std::optional<game::battle::FlatSprite> flatSprite = std::nullopt;
};

} // namespace dwe::app
