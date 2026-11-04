dwe_add_module(battle
    LAYER game
    SOURCES
        src/rules.cpp
        src/combatant.cpp
        src/moves.cpp
        src/state.cpp
        src/strategies.cpp
        src/original_damage.cpp
        src/original_obedience.cpp
        src/original_partner_ai.cpp
        src/original_enemy_ai.cpp
        src/usability.cpp
        src/targeting.cpp
        src/ai_ticks.cpp
        src/fighter_actions.cpp
        src/attack_hits.cpp
        src/flattened_shots.cpp
        src/particle_rolls.cpp
        src/status_effects.cpp
        src/status_visuals.cpp
        src/status_visuals_effects.cpp
        src/movement.cpp
        src/shown_clips.cpp
        src/aftermath.cpp
        src/battle.cpp
        src/thrown_item.cpp
        src/player_retreat.cpp
        src/bone_matrix.cpp
        src/effect_math.cpp
        src/effect_runner.cpp
        src/effect_runner_subops.cpp
        src/effect_runner_fx.cpp
        src/effect_runner_prims.cpp
        src/effect_runner_quads.cpp
        src/flights.cpp
        src/joining.cpp
        src/simulation.cpp
        src/text_keys.cpp
    DEPS core assets ruleset)

# The tests read the game pack from the source tree.
if(TARGET dwe_battle_tests)
    target_compile_definitions(dwe_battle_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
