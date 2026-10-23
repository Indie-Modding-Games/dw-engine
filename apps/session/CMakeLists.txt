# The game session: the app-layer glue of the game systems (ruleset, clock, field, partner, save, and the systems that
# plug in as adapters), which the game and dwe-sim play the same way. Game modules never use each other (rule 4 of
# docs/architecture/dependency-rules.md), so this is the one place that joins them.
dwe_add_module(session
    LAYER apps
    SOURCES
        src/battle_adapter.cpp
        src/evolution_adapter.cpp
        src/field_facts.cpp
        src/game_session.cpp
        src/game_systems.cpp
        src/inventory_adapter.cpp
        src/item_use_rules.cpp
        src/partner_moves.cpp
        src/partner_view.cpp
        src/save_sections.cpp
        src/session_rules.cpp
        src/session_sound.cpp
        src/slot_names.cpp
        src/story_adapter.cpp
        src/story_lua.cpp
        src/story_dialogue.cpp
    DEPS core assets audio scene ruleset field partner save evolution inventory battle story script
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
