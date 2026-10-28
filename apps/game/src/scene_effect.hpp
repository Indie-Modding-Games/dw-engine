#pragma once

#include <dwe/game/battle/effect_runner.hpp>

#include <string>
#include <variant>

namespace dwe::app {

// A thing a technique's effect drew this battle frame (game::battle::EffectDraw), with the move
// whose effect/<move>/ texture and model it uses (BattleScreen to FieldScene::setEffects).
struct SceneEffect {
  std::string move;
  std::variant<game::battle::EffectSprite, game::battle::EffectModel> shape;
};

} // namespace dwe::app
