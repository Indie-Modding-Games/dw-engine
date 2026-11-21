dwe_add_module(localization
    LAYER systems
    SOURCES
        src/catalog.cpp
        src/text_case.cpp
    DEPS core assets)

# The tests read the game pack's texts from the source tree.
if(TARGET dwe_localization_tests)
    target_compile_definitions(dwe_localization_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
