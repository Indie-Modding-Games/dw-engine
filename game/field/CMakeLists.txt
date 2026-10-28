dwe_add_module(field
    LAYER game
    SOURCES
        src/rules.cpp
        src/script_warps.cpp
        src/grid.cpp
        src/field.cpp
        src/npcs.cpp
        src/field_script.cpp
        src/field_edits.cpp
        src/map_objects.cpp
        src/field_items.cpp
        src/field_partner_call.cpp
        src/field_positions.cpp
    DEPS core assets scene
    TEST_LIBS dwe::ruleset)

# The tests read the game pack from the source tree.
if(TARGET dwe_field_tests)
    target_compile_definitions(dwe_field_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
