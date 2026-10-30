#pragma once

#include <dwe/game/battle/effect_runner.hpp>

#include <string>

namespace dwe::app {

// A thing a technique's effect drew this battle frame (game::battle::EffectDraw), with the move
// whose effect/<move>/ texture and model it uses (BattleScreen to FieldScene::setEffects).
struct SceneEffect {
  std::string move;
  game::battle::EffectShape shape;
};

} // namespace dwe::app
