#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/effect_runner.hpp>
#include <dwe/session/battle_effects.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "fighter_figure.hpp"

namespace dwe::render {
class MapCamera;
} // namespace dwe::render

namespace dwe::app {

class FieldScene;

// How a battle shows on the field's scene, shared by the battles of the story (battle_screen.hpp)
// and of the versus (versus_arena.hpp): the fighters' places and clips, the techniques' effects,
// the statuses over the fighters, the hit sparks and the flattened fighters' shots. The fighters
// are the partner (fighter 0, the field's partner) and, for fighter k, the field's NPC or stage
// fighter enemyNpcs[k - 1].

// What the battle's camera shows, for the effects: the window's size in original pixels (a screen
// layer covers what it shows past the original's 320 x 240), the camera's world-to-view transform
// in the console's axes, F V F and F t with F = diag(1, -1, -1) (GsWSMATRIX), its projection
// distance and where its projection's centre is on screen.
[[nodiscard]] game::battle::EffectView effectViewOf(const render::MapCamera& camera);

// data/battle (dwx: BTL_AURA_PROJECTILE_TYPES): each original type's flattened shot sprite. None,
// with a warning, without it.
using ShotColumns = std::map<std::string, std::int32_t, std::less<>>;
// It lives in battle_screen.cpp, where the battles of the story first read it.
[[nodiscard]] ShotColumns readShotColumns(const assets::VirtualFileSystem* files);

// Each fighter's place, facing and clip are the battle's (battle_main.c: the fighters are the
// partner's and the NPCs' entities, which the battle moves, turns and animates itself); fighter k
// plays its species' sounds in slot firstSoundSlot + k.
[[nodiscard]] std::vector<FighterFigure> fighterFiguresOf(const game::battle::BattleState& state,
                                                          std::int64_t firstSoundSlot);

// battle_effect.c: BTL_renderEFEEngine: what the effects drew on the battle's last frame (none
// without effects), and whether a technique hid the map's layer (only while `fighting`). `frames`
// counts the battle frames the effects played.
void showBattleEffects(FieldScene& scene, const session::BattleEffects* effects, bool fighting,
                       std::uint64_t frames);

// battle_effect.c: the status visuals over the fighters (game::battle::StatusVisuals), with each
// fighter's actor and its species' height and radius, the hit sparks, and the flattened fighters'
// shots (BTL_renderAuraProjectile, each with the sprite of its shooter's original type); none
// without a battle.
void showBattleStatus(FieldScene& scene, const game::battle::Battle* battle,
                      const assets::DigimonTable* species, std::span<const std::int32_t> enemyNpcs,
                      const ShotColumns* shotColumns);

} // namespace dwe::app
