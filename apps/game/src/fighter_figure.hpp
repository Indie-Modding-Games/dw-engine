#pragma once

#include <cstdint>

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
};

} // namespace dwe::app
