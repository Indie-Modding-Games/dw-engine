# The game session: the app-layer glue of the game systems (ruleset, clock, field, partner, save, and the systems that
# plug in as adapters), which the game and dwe-sim play the same way. Game modules never use each other (rule 4 of
# docs/architecture/dependency-rules.md), so this is the one place that joins them.
dwe_add_module(session
    LAYER apps
    SOURCES
        src/battle_adapter.cpp
        src/battle_registry.cpp
        src/calendar_adapter.cpp
        src/city_adapter.cpp
        src/medals_adapter.cpp
        src/evolution_adapter.cpp
        src/field_facts.cpp
        src/field_items_adapter.cpp
        src/fishing_adapter.cpp
        src/tournament_adapter.cpp
        src/curling_adapter.cpp
        src/poop_adapter.cpp
        src/cards_adapter.cpp
        src/game_session.cpp
        src/game_systems.cpp
        src/gym_adapter.cpp
        src/inventory_adapter.cpp
        src/item_use_rules.cpp
        src/map_head_lines.cpp
        src/partner_moves.cpp
        src/partner_view.cpp
        src/save_sections.cpp
        src/session_rules.cpp
        src/session_music.cpp
        src/session_sound.cpp
        src/battle_effects.cpp
        src/discard_menu.cpp
        src/item_trader.cpp
        src/item_keeper.cpp
        src/jukebox.cpp
        src/card_shop.cpp
        src/merit_shop.cpp
        src/shop_counter.cpp
        src/slot_names.cpp
        src/story_adapter.cpp
        src/story_adapter_flow.cpp
        src/story_adapter_npc.cpp
        src/story_lua.cpp
        src/story_dialogue.cpp
        src/story_lua_flow.cpp
        src/story_lua_npc.cpp
        src/story_npcs.cpp
        src/story_stage_link.cpp
        src/story_music_api.cpp
        src/story_motion_lua.cpp
        src/story_game_orders.cpp
        src/story_lock.cpp
        src/story_orders_bag.cpp
        src/story_orders_cards.cpp
        src/story_orders_date.cpp
        src/story_orders_field_items.cpp
        src/story_orders_map.cpp
        src/story_orders_partner.cpp
        src/story_props.cpp
        src/story_scene_api.cpp
        src/story_scenes.cpp
        src/story_scenes_rules.cpp
        src/story_routines/opening_routines.cpp
        src/story_routines/routine_angemon_pedestal.cpp
        src/story_routines/routine_animation_loop.cpp
        src/story_routines/routine_card_shop.cpp
        src/story_routines/routine_close_text_box.cpp
        src/story_routines/routine_dirt_cart.cpp
        src/story_routines/routine_discard_item.cpp
        src/story_routines/routine_egg_box.cpp
        src/story_routines/routine_forget_moves.cpp
        src/story_routines/routine_gym.cpp
        src/story_routines/routine_item_keeper.cpp
        src/story_routines/routine_jukebox.cpp
        src/story_routines/routine_merit_shop.cpp
        src/story_routines/routine_money_box.cpp
        src/story_routines/routine_new_game_menu.cpp
        src/story_routines/routine_object_animation.cpp
        src/story_routines/routine_remove_item.cpp
        src/story_routines/routine_save_menu.cpp
        src/story_routines/routine_screen_shake.cpp
        src/story_routines/routine_shop.cpp
        src/story_routines/routine_shop_overlay.cpp
        src/story_routines/routine_shop_tally.cpp
        src/story_routines/routine_smoke_effect.cpp
        src/story_routines/routine_tournament.cpp
        src/story_routines/routine_tournament_medal.cpp
        src/story_routines/routine_tournament_schedule.cpp
        src/story_routines/routine_trade_shop.cpp
        src/story_routines/routine_variable_at_start.cpp
        src/story_routines/screen_routines.cpp
    DEPS core assets audio scene ruleset field partner save evolution inventory battle story gym script fishing tournament curling
    # The story tests check the API against the Lua emitter and run its listings' reference.
    TEST_LIBS dwe::formats ${LUA_LIBRARIES})

# The tests read the game pack from the source tree.
if(TARGET dwe_session_tests)
    target_compile_definitions(dwe_session_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game"
                                                         DWE_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/content/schemas"
                                                         DWE_EXAMPLE_MOD="${PROJECT_SOURCE_DIR}/tests/mods/example_mod"
                                                         DWE_SESSION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    # The field's test maps and rules.
    target_include_directories(dwe_session_tests PRIVATE "${PROJECT_SOURCE_DIR}/game/field/tests")
    # The made-up tables of the systems' tests, as "game/<system>/tests/fixture.hpp".
    target_include_directories(dwe_session_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    # The reference interpreter of the story listings (story_lua_harness.hpp).
    target_include_directories(dwe_session_tests PRIVATE "${PROJECT_SOURCE_DIR}/formats/tests")
    target_include_directories(dwe_session_tests SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
endif()
